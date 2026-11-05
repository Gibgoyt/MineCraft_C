/*
 * A crunch is many small bursts of noise, so that is what this makes: grains of decaying noise thrown at random times, crowded
 * towards the start, then filtered to the band the material lives in, with a decaying tone under it for a knock or a ring.
 * See README.md. Nobody has heard these while writing them, so what can be measured is held by ctest compile_sound_pack.
 */
#include "sound_pack/synth/index.h"

#include "types/sound_blob/index.h"

#define SYNTH_RATE ((float64_t)SOUND_BLOB_SAMPLE_RATE)
#define SYNTH_TWO_PI 6.283185307179586f64
/* A grain or tone this quiet adds nothing, so stop working on it. */
#define SYNTH_SILENT 0.001f64
#define SYNTH_FULL_SCALE 32767.0f64
#define SYNTH_FADE_IN_SAMPLES 48u
#define SYNTH_MIN_FADE_OUT_SAMPLES 144u
/* Keeps a seed away from zero, which xorshift would never leave. Nothing else about the value matters. */
#define SYNTH_SEED_MIX 0x9E3779B97F4A7C15ull

/* xorshift64*: ours, small, and the same everywhere. */
static uint64_t synth_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

/* From 0 up to, but not including, 1. The top 53 bits fit a float64_t exactly. */
static float64_t synth_unit(uint64_t *state) {
    return (float64_t)(synth_next(state) >> 11) * (1.0f64 / 9007199254740992.0f64);
}

/* What a one-pole filter moves by each sample for this corner, from the backward difference: no libm. */
static float64_t synth_pole(uint32_t hz) {
    const float64_t w = SYNTH_TWO_PI * (float64_t)hz / SYNTH_RATE;
    return w / (1.0f64 + w);
}

/* What an envelope is multiplied by each sample, to fall to about a third over ms. */
static float64_t synth_decay(uint32_t ms) {
    return 1.0f64 - 1000.0f64 / ((float64_t)ms * SYNTH_RATE);
}

uint32_t sound_synth_sample_count(const sound_synth_recipe_t *recipe) {
    return recipe->ms * (SOUND_BLOB_SAMPLE_RATE / 1000u);
}

const char *sound_synth_recipe_problem(const sound_synth_recipe_t *recipe) {
    if (recipe->ms < SOUND_SYNTH_MIN_MS || recipe->ms > SOUND_SYNTH_MAX_MS) {
        return "ms must be 20 to 2000";
    }
    if (recipe->grains == 0u || recipe->grains > SOUND_SYNTH_MAX_GRAINS) {
        return "grains must be 1 to 512";
    }
    if (recipe->grain_ms == 0u || recipe->grain_ms > SOUND_SYNTH_MAX_GRAIN_MS) {
        return "grain_ms must be 1 to 500";
    }
    if (recipe->low_hz < SOUND_SYNTH_MIN_HZ || recipe->high_hz > SOUND_SYNTH_MAX_HZ ||
        recipe->low_hz >= recipe->high_hz) {
        return "low_hz must be below high_hz, and both 20 to 20000";
    }
    if (recipe->level == 0u || recipe->level > SOUND_SYNTH_MAX_LEVEL) {
        return "level must be 1 to 100";
    }
    if (recipe->tone_level > SOUND_SYNTH_MAX_LEVEL) {
        return "tone_level must be 0 to 100";
    }
    if (recipe->tone_hz > SOUND_SYNTH_MAX_HZ || recipe->overtone_hz > SOUND_SYNTH_MAX_HZ) {
        return "tone_hz and overtone_hz must be at most 20000";
    }
    const bool any_tone = recipe->tone_hz != 0u || recipe->overtone_hz != 0u;
    if (any_tone != (recipe->tone_level != 0u)) {
        return "tone_level must be above 0 exactly when there is a tone";
    }
    if (any_tone && (recipe->tone_ms == 0u || recipe->tone_ms > SOUND_SYNTH_MAX_GRAIN_MS)) {
        return "tone_ms must be 1 to 500 when there is a tone";
    }
    return nullptr;
}

/* Adds one grain's noise from start on. */
static void synth_add_grain(float64_t *work, uint32_t count, uint32_t start, float64_t strength, float64_t decay,
                            uint64_t *state) {
    float64_t envelope = strength;
    for (uint32_t i = start; i < count && envelope > SYNTH_SILENT; i++) {
        work[i] += envelope * (2.0f64 * synth_unit(state) - 1.0f64);
        envelope *= decay;
    }
}

/* One pole of high pass and two of low pass: enough to say which material it is, and it needs no memory. */
static void synth_filter(float64_t *work, uint32_t count, uint32_t low_hz, uint32_t high_hz) {
    const float64_t low_pole = synth_pole(low_hz);
    const float64_t high_pole = synth_pole(high_hz);
    float64_t floor = 0.0f64;
    float64_t first = 0.0f64;
    float64_t second = 0.0f64;
    for (uint32_t i = 0; i < count; i++) {
        floor += low_pole * (work[i] - floor);
        first += high_pole * (work[i] - floor - first);
        second += high_pole * (first - second);
        work[i] = second;
    }
}

/* Scales so that the loudest sample is 1. All silence stays silence. */
static void synth_normalize(float64_t *work, uint32_t count) {
    float64_t peak = 0.0f64;
    for (uint32_t i = 0; i < count; i++) {
        const float64_t size = work[i] < 0.0f64 ? -work[i] : work[i];
        peak = size > peak ? size : peak;
    }
    if (peak > 0.0f64) {
        const float64_t scale = 1.0f64 / peak;
        for (uint32_t i = 0; i < count; i++) {
            work[i] *= scale;
        }
    }
}

/* A decaying sine by the coupled form, which needs no sin() and stays stable at these rates. It starts from zero. */
static void synth_add_tone(float64_t *work, uint32_t count, uint32_t hz, float64_t strength, float64_t decay) {
    const float64_t w = SYNTH_TWO_PI * (float64_t)hz / SYNTH_RATE;
    float64_t sine = 0.0f64;
    float64_t cosine = 1.0f64;
    float64_t envelope = strength;
    for (uint32_t i = 0; i < count && envelope > SYNTH_SILENT; i++) {
        work[i] += envelope * sine;
        sine += w * cosine;
        cosine -= w * sine;
        envelope *= decay;
    }
}

/* Straight ramps at each end, reaching exactly zero, so a voice has no click. */
static void synth_fade(float64_t *work, uint32_t count) {
    const uint32_t fade_in = count / 8u < SYNTH_FADE_IN_SAMPLES ? count / 8u : SYNTH_FADE_IN_SAMPLES;
    const uint32_t tail = count / 4u;
    const uint32_t fade_out = tail > SYNTH_MIN_FADE_OUT_SAMPLES ? tail : SYNTH_MIN_FADE_OUT_SAMPLES;
    for (uint32_t i = 0; i < fade_in; i++) {
        work[i] *= (float64_t)i / (float64_t)fade_in;
    }
    for (uint32_t i = 0; i < fade_out && i < count; i++) {
        work[count - 1u - i] *= (float64_t)i / (float64_t)fade_out;
    }
}

void sound_synth_render(const sound_synth_recipe_t *recipe, int16_t *samples, float64_t *work) {
    const uint32_t count = sound_synth_sample_count(recipe);
    uint64_t state = recipe->seed ^ SYNTH_SEED_MIX;
    state = state == 0u ? SYNTH_SEED_MIX : state;
    for (uint32_t i = 0; i < count; i++) {
        work[i] = 0.0f64;
    }
    const float64_t decay = synth_decay(recipe->grain_ms);
    for (uint32_t g = 0; g < recipe->grains; g++) {
        /* Squared, so most fall early and the sound dies away. The first is the blow itself, at full strength. */
        const float64_t place = synth_unit(&state);
        const uint32_t start = g == 0u ? 0u : (uint32_t)(place * place * (float64_t)count * 0.8f64);
        const float64_t strength = g == 0u ? 1.0f64 : 0.3f64 + 0.7f64 * synth_unit(&state) * (1.0f64 - place);
        synth_add_grain(work, count, start, strength, decay, &state);
    }
    synth_filter(work, count, recipe->low_hz, recipe->high_hz);
    synth_normalize(work, count);
    const float64_t tone_strength = (float64_t)recipe->tone_level / 100.0f64;
    if (recipe->tone_hz != 0u) {
        synth_add_tone(work, count, recipe->tone_hz, tone_strength, synth_decay(recipe->tone_ms));
    }
    if (recipe->overtone_hz != 0u) {
        synth_add_tone(work, count, recipe->overtone_hz, tone_strength * 0.5f64, synth_decay(recipe->tone_ms));
    }
    synth_fade(work, count);
    synth_normalize(work, count);
    const float64_t full = SYNTH_FULL_SCALE * (float64_t)recipe->level / 100.0f64;
    for (uint32_t i = 0; i < count; i++) {
        const float64_t scaled = work[i] * full;
        samples[i] = (int16_t)(scaled < 0.0f64 ? scaled - 0.5f64 : scaled + 0.5f64);
    }
}
