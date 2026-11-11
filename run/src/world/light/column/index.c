/* See README.md. */
#include "world/light/column/index.h"

#include <string.h>

enum : uint32_t {
    ALONE_NONE = UINT32_MAX,
    ALONE_LEVELS = SECTION_LIGHT_MAX + 1u,
    ALONE_LAYER = SECTION_SIZE * SECTION_SIZE,
    ALONE_TOP_Z = COLUMN_SECTIONS * SECTION_SIZE - 1u,
    ALONE_DIRECTIONS = 6u,
    ALONE_DOWN = 4u,
    ALONE_UP = 5u,
    /* What a section is once sky has fallen on it. */
    ALONE_KIND_LOOK = 0u, /* each voxel must be looked at */
    ALONE_KIND_OPEN =
        1u, /* all one block that costs nothing, under open sky: 15 everywhere, so only its bottom gives anything new */
    ALONE_KIND_DARK = 2u, /* no sky anywhere in it, so it gives no sky */
};
static_assert(WORLD_LIGHT_COLUMN_VOXELS < (uint64_t)ALONE_NONE);

/* Two lists by level, through the voxels: seeds, put in before a channel's spread starts, and raises, put in as it goes. A voxel
 * is in each at most once, as the README shows, so one next per voxel is enough for each. */
typedef struct alone_t {
    const uint16_t *borrowed_states;  /* 0 */
    uint8_t *borrowed_packed;         /* 8 */
    uint32_t *borrowed_next_seed;     /* 16 */
    uint32_t *borrowed_next_raise;    /* 24 */
    uint32_t seed[ALONE_LEVELS];      /* 32: the first voxel of each level's list, or ALONE_NONE */
    uint32_t raise[ALONE_LEVELS];     /* 96 */
    uint8_t kinds[COLUMN_SECTIONS];   /* 160 */
    uint8_t uniform[COLUMN_SECTIONS]; /* 160 + COLUMN_SECTIONS: every voxel of the section is one state */
} alone_t;
static_assert(sizeof(alone_t) == 160u + 2u * COLUMN_SECTIONS);

static inline uint32_t alone_get(const alone_t *alone, uint32_t voxel, uint32_t channel) {
    const uint32_t packed = alone->borrowed_packed[voxel];
    return channel == WORLD_LIGHT_SKY ? packed >> 4 : packed & SECTION_LIGHT_MAX;
}

static inline void alone_set(alone_t *alone, uint32_t voxel, uint32_t channel, uint32_t value) {
    uint8_t *const packed = &alone->borrowed_packed[voxel];
    *packed = channel == WORLD_LIGHT_SKY ? section_light_pack(value, section_light_block(*packed))
                                         : section_light_pack(section_light_sky(*packed), value);
}

static inline const block_def_t *alone_def(const alone_t *alone, uint32_t voxel) {
    return world_light_def(alone->borrowed_states[voxel]);
}

/* The voxel next to this one, if it is in the column. */
static inline bool alone_neighbour(uint32_t voxel, uint32_t direction, uint32_t *out_voxel) {
    switch (direction) {
    case 0u: /* -x */
        *out_voxel = voxel - 1u;
        return (voxel & (SECTION_SIZE - 1u)) != 0u;
    case 1u: /* +x */
        *out_voxel = voxel + 1u;
        return (voxel & (SECTION_SIZE - 1u)) != SECTION_SIZE - 1u;
    case 2u: /* -y */
        *out_voxel = voxel - SECTION_SIZE;
        return ((voxel >> 4) & (SECTION_SIZE - 1u)) != 0u;
    case 3u: /* +y */
        *out_voxel = voxel + SECTION_SIZE;
        return ((voxel >> 4) & (SECTION_SIZE - 1u)) != SECTION_SIZE - 1u;
    case ALONE_DOWN:
        *out_voxel = voxel - ALONE_LAYER;
        return voxel >= ALONE_LAYER;
    default:
        *out_voxel = voxel + ALONE_LAYER;
        return (voxel >> 8) != ALONE_TOP_Z;
    }
}

static void alone_lists_clear(alone_t *alone) {
    for (uint32_t level = 0u; level < ALONE_LEVELS; level++) {
        alone->seed[level] = ALONE_NONE;
        alone->raise[level] = ALONE_NONE;
    }
}

static void alone_seed_push(alone_t *alone, uint32_t voxel, uint32_t level) {
    alone->borrowed_next_seed[voxel] = alone->seed[level];
    alone->seed[level] = voxel;
}

static void alone_raise_push(alone_t *alone, uint32_t voxel, uint32_t level) {
    alone->borrowed_next_raise[voxel] = alone->raise[level];
    alone->raise[level] = voxel;
}

/* ---- Sky ---------------------------------------------------------------------------------------------------------------- */

static bool alone_states_uniform(const uint16_t *states) {
    return memcmp(states, states + 1, (SECTION_VOXELS - 1u) * sizeof(uint16_t)) == 0;
}

/* Sky straight down, one section at a time from the top, with each section's kind. Every voxel gets what falls on it. */
static void alone_fall(alone_t *alone) {
    uint8_t layer[ALONE_LAYER];
    memset(layer, SECTION_LIGHT_MAX, sizeof(layer));
    bool all_sky = true;
    bool all_dark = false;
    for (uint32_t i = COLUMN_SECTIONS; i > 0u; i--) {
        const uint32_t s = i - 1u;
        const uint16_t *const states = alone->borrowed_states + (uint64_t)s * SECTION_VOXELS;
        uint8_t *const packed = alone->borrowed_packed + (uint64_t)s * SECTION_VOXELS;
        alone->uniform[s] = alone_states_uniform(states) ? 1u : 0u;
        if (all_dark) {
            memset(packed, 0, SECTION_VOXELS);
            alone->kinds[s] = ALONE_KIND_DARK;
            continue;
        }
        if (alone->uniform[s] != 0u) {
            const block_def_t *const def = world_light_def(states[0]);
            if (all_sky && def->opacity == 0u) {
                memset(packed, section_light_pack(SECTION_LIGHT_MAX, 0u), SECTION_VOXELS);
                alone->kinds[s] = ALONE_KIND_OPEN;
                continue;
            }
            if (world_light_is_opaque(def)) {
                memset(packed, 0, SECTION_VOXELS);
                memset(layer, 0, sizeof(layer));
                alone->kinds[s] = ALONE_KIND_DARK;
                all_sky = false;
                all_dark = true;
                continue;
            }
        }
        alone->kinds[s] = ALONE_KIND_LOOK;
        for (uint32_t z = SECTION_SIZE; z > 0u; z--) {
            for (uint32_t v = 0u; v < ALONE_LAYER; v++) {
                const uint32_t index = ((z - 1u) << 8) | v;
                const uint32_t sky = world_light_into(layer[v], world_light_def(states[index])->opacity, true);
                layer[v] = (uint8_t)sky;
                packed[index] = section_light_pack(sky, 0u);
            }
        }
        all_sky = true;
        all_dark = true;
        for (uint32_t v = 0u; v < ALONE_LAYER; v++) {
            all_sky = all_sky && layer[v] == SECTION_LIGHT_MAX;
            all_dark = all_dark && layer[v] == 0u;
        }
    }
    /* Past the top of storage counts as a neighbour holding 15 of sky, for an opaque block's faces. */
    for (uint32_t v = 0u; v < ALONE_LAYER; v++) {
        const uint32_t voxel = ALONE_TOP_Z * ALONE_LAYER + v;
        if (world_light_is_opaque(alone_def(alone, voxel))) {
            alone_set(alone, voxel, WORLD_LIGHT_SKY, SECTION_LIGHT_MAX);
        }
    }
}

/* Each voxel lit by the fall that could give a neighbour more than the fall did is a seed. And an opaque block's faces see what
 * falls beside them. An open section's voxels are all 15 beside each other, so only its bottom layer is looked at. */
static void alone_sky_seed(alone_t *alone) {
    for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
        if (alone->kinds[s] == ALONE_KIND_DARK) {
            continue;
        }
        const uint32_t first = s * SECTION_VOXELS;
        const uint32_t end = first + (alone->kinds[s] == ALONE_KIND_OPEN ? ALONE_LAYER : SECTION_VOXELS);
        for (uint32_t voxel = first; voxel < end; voxel++) {
            const uint32_t value = alone_get(alone, voxel, WORLD_LIGHT_SKY);
            if (value == 0u || world_light_is_opaque(alone_def(alone, voxel))) {
                continue;
            }
            bool seed = false;
            for (uint32_t direction = 0u; direction < ALONE_DIRECTIONS; direction++) {
                uint32_t near = 0u;
                if (!alone_neighbour(voxel, direction, &near)) {
                    continue;
                }
                const block_def_t *const near_def = alone_def(alone, near);
                const uint32_t held = alone_get(alone, near, WORLD_LIGHT_SKY);
                if (world_light_is_opaque(near_def)) {
                    if (value > held) {
                        alone_set(alone, near, WORLD_LIGHT_SKY, value);
                    }
                } else if (direction != ALONE_UP &&
                           world_light_into(value, near_def->opacity, direction == ALONE_DOWN) > held) {
                    seed = true; /* never upward: what is above got at least this much from the fall */
                }
            }
            if (seed) {
                alone_seed_push(alone, voxel, value);
            }
        }
    }
}

/* ---- Block light ---------------------------------------------------------------------------------------------------------- */

/* Every voxel whose block gives off light holds that much, and is a seed. */
static void alone_block_seed(alone_t *alone) {
    for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
        const uint32_t first = s * SECTION_VOXELS;
        if (alone->uniform[s] != 0u && alone_def(alone, first)->light == 0u) {
            continue;
        }
        for (uint32_t voxel = first; voxel < first + SECTION_VOXELS; voxel++) {
            const uint32_t gives = alone_def(alone, voxel)->light;
            if (gives != 0u) {
                alone_set(alone, voxel, WORLD_LIGHT_BLOCK, gives);
                alone_seed_push(alone, voxel, gives);
            }
        }
    }
}

/* ---- The spread, brightest first ------------------------------------------------------------------------------------------ */

static void alone_bright(alone_t *alone, uint32_t channel) {
    uint64_t taken = 0u;
    for (uint32_t level = SECTION_LIGHT_MAX; level > 0u; level--) {
        for (;;) {
            uint32_t voxel = alone->raise[level];
            if (voxel != ALONE_NONE) {
                alone->raise[level] = alone->borrowed_next_raise[voxel];
            } else if (alone->seed[level] != ALONE_NONE) {
                voxel = alone->seed[level];
                alone->seed[level] = alone->borrowed_next_seed[voxel];
            } else {
                break;
            }
            /* Each voxel is taken at most twice, once from each list: a list gone round in a ring would never end. */
            taken++;
            ASSERT_ALWAYS(taken <= 2u * WORLD_LIGHT_COLUMN_VOXELS);
            if (alone_get(alone, voxel, channel) != level) {
                continue; /* got brighter since, and is in a list for that too */
            }
            const bool from_opaque = world_light_is_opaque(alone_def(alone, voxel));
            for (uint32_t direction = 0u; direction < ALONE_DIRECTIONS; direction++) {
                uint32_t near = 0u;
                if (!alone_neighbour(voxel, direction, &near)) {
                    continue;
                }
                const block_def_t *const near_def = alone_def(alone, near);
                const uint32_t held = alone_get(alone, near, channel);
                if (world_light_is_opaque(near_def)) {
                    /* Its faces see this, and it goes no further. */
                    if (!from_opaque && !world_light_keeps_own(near_def, channel) && level > held) {
                        alone_set(alone, near, channel, level);
                    }
                    continue;
                }
                const uint32_t offered =
                    world_light_into(level, near_def->opacity, channel == WORLD_LIGHT_SKY && direction == ALONE_DOWN);
                if (offered > held) {
                    alone_set(alone, near, channel, offered);
                    alone_raise_push(alone, near, offered);
                }
            }
        }
    }
}

/* ---- The API -------------------------------------------------------------------------------------------------------------- */

void world_light_column_alone(const uint16_t *states, uint8_t *out_packed, uint32_t *scratch) {
    alone_t alone = {
        .borrowed_states = states,
        .borrowed_packed = out_packed,
        .borrowed_next_seed = scratch,
        .borrowed_next_raise = scratch + WORLD_LIGHT_COLUMN_VOXELS,
    };
    alone_lists_clear(&alone);
    alone_fall(&alone);
    alone_sky_seed(&alone);
    alone_bright(&alone, WORLD_LIGHT_SKY);
    alone_lists_clear(&alone);
    alone_block_seed(&alone);
    alone_bright(&alone, WORLD_LIGHT_BLOCK);
}
