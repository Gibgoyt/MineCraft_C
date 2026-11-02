/* See README.md. */
#include "terrain/rng/index.h"

#include "assert/index.h"

enum : uint64_t {
    RNG_GAMMA = 0x9E3779B97F4A7C15ull,
    RNG_MIX_1 = 0xBF58476D1CE4E5B9ull,
    RNG_MIX_2 = 0x94D049BB133111EBull,
    RNG_FNV_OFFSET = 0xCBF29CE484222325ull,
    RNG_FNV_PRIME = 0x00000100000001B3ull,
};
enum : uint32_t {
    RNG_MIX_SHIFT_1 = 30u,
    RNG_MIX_SHIFT_2 = 27u,
    RNG_MIX_SHIFT_3 = 31u,
    RNG_F32_DROP_BITS = 40u, /* leaves 24, which a float32_t holds exactly */
    RNG_F64_DROP_BITS = 11u, /* leaves 53, which a float64_t holds exactly */
    RNG_HALF_BITS = 32u,
};

uint64_t terrain_rng_mix(uint64_t value) {
    uint64_t mixed = (value ^ (value >> RNG_MIX_SHIFT_1)) * RNG_MIX_1;
    mixed = (mixed ^ (mixed >> RNG_MIX_SHIFT_2)) * RNG_MIX_2;
    return mixed ^ (mixed >> RNG_MIX_SHIFT_3);
}

/* Mixed before any word goes in. An xor of a raw seed with a coordinate would make seed s at x the same as seed s ^ k at
 * x ^ k: worlds that are copies of one another, shuffled. */
static uint64_t rng_hash_begin(uint64_t seed) {
    return terrain_rng_mix(seed + RNG_GAMMA);
}

/* For a fixed hash, different words give different results, since both steps are bijections. */
static uint64_t rng_hash_absorb(uint64_t hash, uint64_t word) {
    return terrain_rng_mix((hash ^ word) + RNG_GAMMA);
}

/* A coordinate as its 32 bits. Through uint32_t, so that -1 is one word and not 64 set bits. */
static uint64_t rng_word_of(int32_t coordinate) {
    return (uint64_t)(uint32_t)coordinate;
}

uint64_t terrain_rng_salt(const char *name) {
    uint64_t hash = RNG_FNV_OFFSET;
    for (const char *c = name; *c != '\0'; c++) {
        hash = (hash ^ (uint64_t)(uint8_t)*c) * RNG_FNV_PRIME;
    }
    return terrain_rng_mix(hash);
}

uint64_t terrain_rng_derive(uint64_t seed, uint64_t salt) {
    return rng_hash_absorb(rng_hash_begin(seed), salt);
}

uint64_t terrain_rng_hash2(uint64_t seed, int32_t x, int32_t y) {
    return rng_hash_absorb(rng_hash_absorb(rng_hash_begin(seed), rng_word_of(x)), rng_word_of(y));
}

uint64_t terrain_rng_hash3(uint64_t seed, int32_t x, int32_t y, int32_t z) {
    return rng_hash_absorb(terrain_rng_hash2(seed, x, y), rng_word_of(z));
}

terrain_rng_t terrain_rng_start(uint64_t seed) {
    return (terrain_rng_t){.state = seed};
}

uint64_t terrain_rng_next_u64(terrain_rng_t *rng) {
    rng->state += RNG_GAMMA;
    return terrain_rng_mix(rng->state);
}

uint32_t terrain_rng_next_u32(terrain_rng_t *rng) {
    return (uint32_t)(terrain_rng_next_u64(rng) >> RNG_HALF_BITS);
}

/* Lemire's method: the top half of a 32 by 32 multiply lands in range, and the rare low halves that would favour some
 * results are drawn again. */
uint32_t terrain_rng_next_below(terrain_rng_t *rng, uint32_t bound) {
    ASSERT_ALWAYS(bound != 0u);
    uint64_t product = (uint64_t)terrain_rng_next_u32(rng) * bound;
    if ((uint32_t)product < bound) {
        const uint32_t threshold = (0u - bound) % bound;
        while ((uint32_t)product < threshold) {
            product = (uint64_t)terrain_rng_next_u32(rng) * bound;
        }
    }
    return (uint32_t)(product >> RNG_HALF_BITS);
}

float32_t terrain_rng_next_f32(terrain_rng_t *rng) {
    return (float32_t)(uint32_t)(terrain_rng_next_u64(rng) >> RNG_F32_DROP_BITS) * 0x1p-24f32;
}

float64_t terrain_rng_next_f64(terrain_rng_t *rng) {
    return (float64_t)(terrain_rng_next_u64(rng) >> RNG_F64_DROP_BITS) * 0x1p-53f64;
}
