/* Even ground for a scripted dig, found by reading the land, and the keys of a /tp there: for the program tests whose script
 * digs the top, what is under it, and the top beside the hole, and wants all three to drop the same item. What lies at a
 * seed's start is the land's, and it moves with each plan that changes the land (plan 007 phase 04 put seed 11's start on
 * the edge between grass and sand), so the tests take the nearest place where it holds and go there by /tp, as
 * tests/run/player/survival/ goes to its run. */
#ifndef MC_TESTS_RUN_GAME_FIXTURES_GROUND_H
#define MC_TESTS_RUN_GAME_FIXTURES_GROUND_H

#include "game/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    GROUND_SIDE = 9u,           /* columns a side, round the place, that must be the same */
    GROUND_TALL = 32u,          /* from 8 under the ground up */
    GROUND_UNDER = 8u,          /* blocks of the box under the ground the hydrology gives */
    GROUND_ROOM = 12u,          /* blocks of air the top must have over it in the box, for a tree to show */
    GROUND_SEARCH_STEP = 32u,   /* blocks between the places tried, ring by ring round the world's middle */
    GROUND_SEARCH_RINGS = 96u,  /* 3 km out */
    GROUND_LOADED_TICKS = 500u, /* a far jump's columns are drawn within 15 s (plan 006 phase 01) */
};

/* Whether the GROUND_SIDE square round x, y is level dry ground, every column with the same top block, the block under it
 * dropping what the top drops, and no log or leaves above it, from the very blocks the test world makes (it has no climate,
 * so the fallback's). blocks and opened hold GROUND_SIDE^2 * GROUND_TALL each. */
[[maybe_unused]] static bool ground_even_at(const terrain_t *terrain, int32_t x, int32_t y, uint8_t *opened,
                                            uint16_t *blocks) {
    const terrain_hydrology_column_t middle = terrain_hydrology_column_at(terrain->borrowed_hydrology, x, y);
    if (middle.kind != TERRAIN_HYDROLOGY_KIND_LAND || middle.ground < (float32_t)MC_WORLD_Z_SEA + 2.0f32) {
        return false;
    }
    const int32_t half = (int32_t)(GROUND_SIDE / 2u);
    const terrain_density_box_t box = {.origin_x = x - half,
                                       .origin_y = y - half,
                                       .origin_z = (int32_t)middle.ground - (int32_t)GROUND_UNDER,
                                       .count_x = GROUND_SIDE,
                                       .count_y = GROUND_SIDE,
                                       .count_z = GROUND_TALL};
    terrain_generate_box_fallback(terrain, &box, opened, blocks);
    const uint32_t layer = GROUND_SIDE * GROUND_SIDE;
    uint32_t top_z = GROUND_TALL;
    uint16_t top = 0u;
    for (uint32_t c = 0u; c < layer; c++) {
        uint32_t z = GROUND_TALL;
        for (uint32_t k = GROUND_TALL; k-- > 0u;) {
            const uint16_t id = blocks[c + layer * k];
            if ((block_defs[id].flags & (BLOCK_FLAG_LOG | BLOCK_FLAG_DECAYS)) != 0u) {
                return false;
            }
            if (z == GROUND_TALL && (block_defs[id].flags & BLOCK_FLAG_SOLID) != 0u) {
                z = k;
            }
        }
        if (z == GROUND_TALL || z == 0u || z + GROUND_ROOM >= GROUND_TALL) {
            return false;
        }
        const uint16_t here = blocks[c + layer * z];
        const uint16_t under = blocks[c + layer * (z - 1u)];
        if (c == 0u) {
            top_z = z;
            top = here;
        }
        if (z != top_z || here != top || block_defs[here].drop_count == 0u || block_defs[under].drop_count == 0u ||
            block_drops[block_defs[here].first_drop].item != block_drops[block_defs[under].first_drop].item) {
            return false;
        }
    }
    return true;
}

/* The nearest such place to the world's middle, ring by ring. */
[[maybe_unused]] static bool ground_even_find(const terrain_t *terrain, int32_t *out_x, int32_t *out_y) {
    static uint8_t opened[GROUND_SIDE * GROUND_SIDE * GROUND_TALL];
    static uint16_t blocks[GROUND_SIDE * GROUND_SIDE * GROUND_TALL];
    const int32_t step = (int32_t)GROUND_SEARCH_STEP;
    for (int32_t ring = 0; ring < (int32_t)GROUND_SEARCH_RINGS; ring++) {
        for (int32_t dy = -ring; dy <= ring; dy++) {
            for (int32_t dx = -ring; dx <= ring; dx++) {
                if (dx != -ring && dx != ring && dy != -ring && dy != ring) {
                    continue;
                }
                const int32_t x = GAME_WORLD_MIDDLE + dx * step, y = GAME_WORLD_MIDDLE + dy * step;
                if (ground_even_at(terrain, x, y, opened, blocks)) {
                    *out_x = x;
                    *out_y = y;
                    return true;
                }
            }
        }
    }
    return false;
}

/* Writes into text, from *length, the keys of "/tp x y" and Enter from tick 20, and a wait for the columns there; *tick is
 * then the tick the script may go on from. */
[[maybe_unused]] static void ground_tp_keys(char *text, uint64_t capacity, uint64_t *length, int32_t x, int32_t y,
                                            uint32_t *tick) {
    char command[64];
    snprintf(command, sizeof(command), "/tp %" PRId32 " %" PRId32, x, y);
    uint32_t at = 20u;
    for (const char *c = command; *c != '\0'; c++) {
        char name[16];
        if (*c == ' ') {
            snprintf(name, sizeof(name), "space");
        } else if (*c == '/') {
            snprintf(name, sizeof(name), "slash");
        } else if (*c == '-') {
            snprintf(name, sizeof(name), "minus");
        } else {
            snprintf(name, sizeof(name), "%c", *c);
        }
        at += *c == '/' ? 1u : 2u;
        *length += (uint64_t)snprintf(text + *length, capacity - *length,
                                      "%" PRIu32 " key %s down\n%" PRIu32 " key %s up\n", at, name, at, name);
    }
    at += 2u;
    *length += (uint64_t)snprintf(text + *length, capacity - *length,
                                  "%" PRIu32 " key enter down\n%" PRIu32 " key enter up\n", at, at);
    *tick = at + GROUND_LOADED_TICKS;
}

/* Appends script, a line a command each beginning with its tick, with every tick moved on by from. */
[[maybe_unused]] static void ground_script_after(char *text, uint64_t capacity, uint64_t *length, const char *script,
                                                 uint32_t from) {
    for (const char *line = script; *line != '\0';) {
        const char *const end = strchr(line, '\n');
        const uint64_t bytes = end != nullptr ? (uint64_t)(end - line) : strlen(line);
        uint32_t tick = 0u;
        int32_t used = 0;
        if (sscanf(line, "%" SCNu32 "%n", &tick, &used) == 1) {
            *length += (uint64_t)snprintf(text + *length, capacity - *length, "%" PRIu32 "%.*s\n", tick + from,
                                          (int32_t)(bytes - (uint64_t)used), line + used);
        }
        line = end != nullptr ? end + 1 : line + bytes;
    }
}

#endif
