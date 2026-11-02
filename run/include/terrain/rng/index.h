/* See run/src/terrain/rng/README.md. */
#ifndef MC_RUN_TERRAIN_RNG_INDEX_H
#define MC_RUN_TERRAIN_RNG_INDEX_H

#include "types/index.h"

#include <stdint.h>

/* One stream of random numbers: SplitMix64. Eight bytes and no setup cost, because terrain starts one per column or per
 * feature, from a position hash, and throws it away. Changing any function here changes every world. */
typedef struct terrain_rng_t {
    uint64_t state;
} terrain_rng_t;
static_assert(sizeof(terrain_rng_t) == 8);

/* A bijection on 64 bits that scrambles every input bit into every output bit: SplitMix64's finalizer. */
uint64_t terrain_rng_mix(uint64_t value);

/* A salt from a name, so that what picks a stream reads as what it is for, and two systems cannot pick the same number by
 * hand: terrain_rng_salt("climate/erosion"). */
uint64_t terrain_rng_salt(const char *name);

/* The seed of a child stream. Different salts give unrelated seeds, as do different parents with one salt. */
uint64_t terrain_rng_derive(uint64_t seed, uint64_t salt);

/* The hash of a position, a pure function of its arguments. This is what lets a worker generate any column in any order
 * and get the bytes every other worker would: nothing about the position depends on what ran before. A hash2 and a hash3
 * with z = 0 are unrelated. Use terrain_rng_derive first to give each purpose its own numbers. */
uint64_t terrain_rng_hash2(uint64_t seed, int32_t x, int32_t y);
uint64_t terrain_rng_hash3(uint64_t seed, int32_t x, int32_t y, int32_t z);

terrain_rng_t terrain_rng_start(uint64_t seed);

uint64_t terrain_rng_next_u64(terrain_rng_t *rng);
uint32_t terrain_rng_next_u32(terrain_rng_t *rng);
/* Uniform from 0 to bound - 1, with no bias. A bound of 0 asserts. Takes more than one draw now and then, so a stream that
 * is compared with another must make the same calls in the same order. */
uint32_t terrain_rng_next_below(terrain_rng_t *rng, uint32_t bound);
/* Uniform in [0, 1), on steps of 2^-24 and 2^-53. Whole-number arithmetic, then an exact conversion, so no rounding mode
 * can change them. */
float32_t terrain_rng_next_f32(terrain_rng_t *rng);
float64_t terrain_rng_next_f64(terrain_rng_t *rng);

#endif
