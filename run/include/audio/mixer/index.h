/* See run/src/audio/mixer/README.md. */
#ifndef MC_RUN_AUDIO_MIXER_INDEX_H
#define MC_RUN_AUDIO_MIXER_INDEX_H

#include "types/index.h"
#include "types/sound_blob/index.h"

#include <stdint.h>

enum : uint32_t {
    AUDIO_MIXER_VOICES = 8u,
    AUDIO_MIXER_GAIN_ONE = 32768u, /* a gain or volume of 1, in the mixer's whole numbers */
    AUDIO_MIXER_VOLUME_MAX = 100u,
};

/* One sound playing. */
typedef struct audio_voice_t {
    const int16_t *borrowed_samples; /* 0: from the blob, or nullptr for a voice not in use */
    uint32_t sample_count;           /* 8 */
    uint32_t at;                     /* 12: the next to play */
    uint16_t gain_left;              /* 16 */
    uint16_t gain_right;             /* 18 */
    uint8_t _pad0[4];                /* 20 */
} audio_voice_t;
static_assert(sizeof(audio_voice_t) == 24);

/* What is playing. Plain numbers, so that the same plays give the same samples. The audio thread's, or a test's. */
typedef struct audio_mixer_t {
    audio_voice_t voices[AUDIO_MIXER_VOICES]; /* 0 */
    sound_blob_view_t view;                   /* 192 */
    uint64_t random;                          /* 216: splitmix64's state */
    uint64_t played_count;                    /* 224: sounds started */
    uint64_t stolen_count;                    /* 232: of those, how many took the place of another */
    uint64_t clipped_count;                   /* 240: samples that came out past what 16 bits hold */
    /* What each family and kind played last, so that a sound does not repeat straight away. 0 before any. */
    uint8_t last_variant[SOUND_FAMILY_COUNT][SOUND_KIND_COUNT]; /* 248 */
    uint8_t _pad0[7];                                           /* 281 */
} audio_mixer_t;
static_assert(sizeof(audio_mixer_t) == 288);

/* Nothing playing, with sounds from this blob, which must outlive it, and this seed for which variant comes next. */
void audio_mixer_start(audio_mixer_t *mixer, const sound_blob_view_t *view, uint64_t seed);

/* Starts one of this family and kind's sounds at these gains, each up to AUDIO_MIXER_GAIN_ONE. If all the voices are busy it takes the place of the one nearest its end.
 * False if the blob has no such sound, as for SOUND_FAMILY_NONE. Never waits and touches no heap. */
bool audio_mixer_play(audio_mixer_t *mixer, uint32_t family, uint32_t kind, uint16_t gain_left, uint16_t gain_right);

/* How many voices are playing. */
uint32_t audio_mixer_playing(const audio_mixer_t *mixer);

/* The next frames, left then right, at this volume from 0 to AUDIO_MIXER_VOLUME_MAX, added up and held to what 16 bits hold. Silence past the last sound. */
void audio_mixer_render(audio_mixer_t *mixer, int16_t *out, uint32_t frames, uint32_t volume_percent);

/* A sound at source, for an ear at listener facing along forward, in world blocks with z up: it fades with distance, and the ear turned away from it hears less. */
void audio_mixer_gains(const float64_t listener[3], const float64_t forward[3], const float64_t source[3],
                       uint16_t *out_left, uint16_t *out_right);

#endif
