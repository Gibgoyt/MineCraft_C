/* Reads and checks a whole sounds/ tree, and has every sound's samples ready. See compile/src/sound_pack/tree/README.md. */
#ifndef SOUND_PACK_TREE_INDEX_H
#define SOUND_PACK_TREE_INDEX_H

#include "texture_pack/support/index.h"

#include <stdint.h>

/* One sound, loaded or rendered. Borrows from the arena. */
typedef struct sound_tree_sound_t {
    const int16_t *borrowed_samples; /* 0 */
    uint32_t sample_count;           /* 8 */
    uint8_t family;                  /* 12: a sound_family_t */
    uint8_t kind;                    /* 13: a sound_kind_t */
    uint8_t variant;                 /* 14: from 1 */
    uint8_t recorded;                /* 15: 1 if from index.mcsnd, 0 if made from its recipe */
} sound_tree_sound_t;
static_assert(sizeof(sound_tree_sound_t) == 16);

/* The whole tree, ascending by family, kind and variant. */
typedef struct sound_tree_t {
    sound_tree_sound_t *borrowed_sounds; /* 0 */
    uint32_t sound_count;                /* 8 */
    uint32_t recorded_count;             /* 12 */
} sound_tree_t;
static_assert(sizeof(sound_tree_t) == 16);

/*
 * Every family needs every kind, in variants from 1 with no gap. Every problem found is printed as path:line:column: message,
 * or path: message where there is no line, and false is returned. The same tree gives the same samples.
 */
bool sound_tree_load(tp_arena_t *arena, const char *sounds, sound_tree_t *out);

/* Whether this is a name a sound's directory may have: <kind>_<variant>, such as hit_1. */
bool sound_tree_parse_name(const char *name, uint8_t *out_kind, uint8_t *out_variant);

#endif
