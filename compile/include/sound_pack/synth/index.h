/*
 * Makes a crunch from a recipe, with our own code and no recording. See compile/src/sound_pack/synth/README.md.
 * The arithmetic is all + - * / on float64_t and no libm, so one recipe gives the same bytes on any machine.
 */
#ifndef SOUND_PACK_SYNTH_INDEX_H
#define SOUND_PACK_SYNTH_INDEX_H

#include "types/index.h"

#include <stdint.h>

/* The limits of a recipe's fields, which sound_synth_recipe_problem holds a file to. */
#define SOUND_SYNTH_MIN_MS 20u
#define SOUND_SYNTH_MAX_MS 2000u
#define SOUND_SYNTH_MAX_GRAINS 512u
#define SOUND_SYNTH_MAX_GRAIN_MS 500u
#define SOUND_SYNTH_MIN_HZ 20u
/* Below half the rate, which is as far as these filters mean anything. */
#define SOUND_SYNTH_MAX_HZ 20000u
#define SOUND_SYNTH_MAX_LEVEL 100u

typedef struct sound_synth_recipe_t {
    uint64_t seed;        /* 0: which noise, and where its grains fall */
    uint32_t ms;          /* 8: how long the sound is */
    uint32_t grains;      /* 12: bursts of noise, the first at the very start and the rest crowding towards it */
    uint32_t grain_ms;    /* 16: how fast each burst dies: its time constant */
    uint32_t low_hz;      /* 20: the noise has little below this */
    uint32_t high_hz;     /* 24: and little above this */
    uint32_t tone_hz;     /* 28: the ring or the body under the noise, 0 for none */
    uint32_t overtone_hz; /* 32: a second tone at half its strength, 0 for none */
    uint32_t tone_ms;     /* 36: the tone's time constant */
    uint32_t tone_level;  /* 40: its strength against the noise, 0 to 100 */
    uint32_t level;       /* 44: the loudest sample, as a percent of full scale, 1 to 100 */
} sound_synth_recipe_t;
static_assert(sizeof(sound_synth_recipe_t) == 48);

/* How many samples a recipe makes. */
uint32_t sound_synth_sample_count(const sound_synth_recipe_t *recipe);

/* A sentence about what is wrong with a recipe, or nullptr if nothing is. */
const char *sound_synth_recipe_problem(const sound_synth_recipe_t *recipe);

/*
 * Fills samples, and uses work as its scratch space: each holds sound_synth_sample_count(recipe) values. The recipe must have
 * no problem. The first and last samples are zero, so a voice starts and ends with no click.
 */
void sound_synth_render(const sound_synth_recipe_t *recipe, int16_t *samples, float64_t *work);

#endif
