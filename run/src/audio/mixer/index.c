/* See README.md. Scalar C, in whole numbers past the gains, so that the same plays give the same samples anywhere. */
#include "audio/mixer/index.h"

#include <math.h>
#include <string.h>

/* The fade: nothing lost out to where a body can reach and a little past, and gone at the distance Minecraft's published sounds reach. */
static const float64_t MIXER_NEAR_BLOCKS = 6.0f64;
static const float64_t MIXER_FAR_BLOCKS = 32.0f64;
/* How much of the far ear is taken at most. A sound right beside the head still reaches both, or a block on the left would be gone from one speaker. */
static const float64_t MIXER_PAN_DEPTH = 0.5f64;

static uint64_t mixer_random_next(uint64_t *state) {
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

void audio_mixer_start(audio_mixer_t *mixer, const sound_blob_view_t *view, uint64_t seed) {
    memset(mixer, 0, sizeof(*mixer));
    mixer->view = *view;
    mixer->random = seed;
}

uint32_t audio_mixer_playing(const audio_mixer_t *mixer) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < AUDIO_MIXER_VOICES; i++) {
        count += mixer->voices[i].borrowed_samples != nullptr ? 1u : 0u;
    }
    return count;
}

/* A free voice, or the one with the least left to play, which is the one that will be missed least. */
static audio_voice_t *mixer_voice_take(audio_mixer_t *mixer) {
    audio_voice_t *best = &mixer->voices[0];
    uint32_t least = UINT32_MAX;
    for (uint32_t i = 0u; i < AUDIO_MIXER_VOICES; i++) {
        audio_voice_t *const voice = &mixer->voices[i];
        if (voice->borrowed_samples == nullptr) {
            return voice;
        }
        const uint32_t left = voice->sample_count - voice->at;
        if (left < least) {
            least = left;
            best = voice;
        }
    }
    mixer->stolen_count++;
    return best;
}

bool audio_mixer_play(audio_mixer_t *mixer, uint32_t family, uint32_t kind, uint16_t gain_left, uint16_t gain_right) {
    if (family >= SOUND_FAMILY_COUNT || kind >= SOUND_KIND_COUNT) {
        return false;
    }
    uint32_t first = 0u;
    const uint32_t count = sound_blob_find(&mixer->view, family, kind, &first);
    if (count == 0u) {
        return false;
    }
    /* Any but the last one: with one to choose from, that one. */
    const uint8_t last = mixer->last_variant[family][kind];
    uint32_t pick = (uint32_t)(mixer_random_next(&mixer->random) % count);
    if (count > 1u && last != 0u && mixer->view.borrowed_entries[first + pick].variant == last) {
        pick = (pick + 1u) % count;
    }
    const sound_blob_entry_t *const entry = &mixer->view.borrowed_entries[first + pick];
    mixer->last_variant[family][kind] = entry->variant;
    audio_voice_t *const voice = mixer_voice_take(mixer);
    *voice = (audio_voice_t){.borrowed_samples = sound_blob_samples(&mixer->view, entry),
                             .sample_count = entry->sample_count,
                             .at = 0u,
                             .gain_left = gain_left,
                             .gain_right = gain_right,
                             ._pad0 = {0}};
    mixer->played_count++;
    return true;
}

/* An ear hears loudness roughly as the square of the slider's way along, so half way is a quarter of the amplitude. */
static int64_t mixer_volume_gain(uint32_t volume_percent) {
    const uint32_t percent = volume_percent < AUDIO_MIXER_VOLUME_MAX ? volume_percent : AUDIO_MIXER_VOLUME_MAX;
    return (int64_t)((uint64_t)percent * percent * AUDIO_MIXER_GAIN_ONE /
                     ((uint64_t)AUDIO_MIXER_VOLUME_MAX * AUDIO_MIXER_VOLUME_MAX));
}

static int16_t mixer_clamp(int64_t value, uint64_t *clipped_count) {
    if (value > INT16_MAX) {
        (*clipped_count)++;
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        (*clipped_count)++;
        return INT16_MIN;
    }
    return (int16_t)value;
}

void audio_mixer_render(audio_mixer_t *mixer, int16_t *out, uint32_t frames, uint32_t volume_percent) {
    const int64_t volume = mixer_volume_gain(volume_percent);
    for (uint32_t frame = 0u; frame < frames; frame++) {
        int64_t left = 0, right = 0;
        for (uint32_t i = 0u; i < AUDIO_MIXER_VOICES; i++) {
            audio_voice_t *const voice = &mixer->voices[i];
            if (voice->borrowed_samples == nullptr) {
                continue;
            }
            const int64_t sample = voice->borrowed_samples[voice->at];
            left += sample * voice->gain_left;
            right += sample * voice->gain_right;
            voice->at++;
            if (voice->at == voice->sample_count) {
                voice->borrowed_samples = nullptr;
            }
        }
        /* Two shifts, so that a voice at gain one and full volume comes out as exactly the sample it was. */
        out[frame * 2u] = mixer_clamp(((left >> 15) * volume) >> 15, &mixer->clipped_count);
        out[frame * 2u + 1u] = mixer_clamp(((right >> 15) * volume) >> 15, &mixer->clipped_count);
    }
}

void audio_mixer_gains(const float64_t listener[3], const float64_t forward[3], const float64_t source[3],
                       uint16_t *out_left, uint16_t *out_right) {
    const float64_t x = source[0] - listener[0], y = source[1] - listener[1], z = source[2] - listener[2];
    const float64_t distance = sqrt(x * x + y * y + z * z);
    float64_t fade = 1.0f64;
    if (distance >= MIXER_FAR_BLOCKS) {
        fade = 0.0f64;
    } else if (distance > MIXER_NEAR_BLOCKS) {
        fade = (MIXER_FAR_BLOCKS - distance) / (MIXER_FAR_BLOCKS - MIXER_NEAR_BLOCKS);
    }
    /* With z up, to the right of forward is forward turned a quarter clockwise as seen from above: (y, -x). Looking straight up or down there is no right to speak of. */
    const float64_t flat = sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
    float64_t pan = 0.0f64;
    if (distance > 1e-6f64 && flat > 1e-6f64) {
        pan = (x * forward[1] - y * forward[0]) / (flat * distance);
    }
    const float64_t left = fade * (pan > 0.0f64 ? 1.0f64 - MIXER_PAN_DEPTH * pan : 1.0f64);
    const float64_t right = fade * (pan < 0.0f64 ? 1.0f64 + MIXER_PAN_DEPTH * pan : 1.0f64);
    *out_left = (uint16_t)lround(left * (float64_t)AUDIO_MIXER_GAIN_ONE);
    *out_right = (uint16_t)lround(right * (float64_t)AUDIO_MIXER_GAIN_ONE);
}
