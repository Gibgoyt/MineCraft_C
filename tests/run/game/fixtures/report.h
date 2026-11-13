/* The program's own report, read back by tests that play a generated world: what the player mined and holds, and what the
 * block it last mined drops. Plan 006 phase 03: the ground under a test world's spawn is whatever its biome lays (grass, podzol,
 * sand, ...), and the land moves with each plan that changes it, so a test that digs there names no block: it reads which one
 * the report says, and holds the rest to that. */
#ifndef MC_TESTS_RUN_GAME_FIXTURES_REPORT_H
#define MC_TESTS_RUN_GAME_FIXTURES_REPORT_H

#include "harness/index.h"

#include "tables/blocks/index.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t { REPORT_NAME_BYTES = 64u };

/* "the player mined M blocks and placed P; the last mined was B, and the hand holds N of I". */
typedef struct report_mined_t {
    uint64_t mined;                    /* 0 */
    uint64_t placed;                   /* 8 */
    uint64_t hand_count;               /* 16 */
    char last[REPORT_NAME_BYTES];      /* 24: a block's name, or "air" */
    char hand_item[REPORT_NAME_BYTES]; /* 88: an item's name, or "nothing" */
} report_mined_t;
static_assert(sizeof(report_mined_t) == 152);

/* The report's mined line out of output, or false if there is none. */
[[maybe_unused]] static bool report_mined_read(const char *output, report_mined_t *out) {
    memset(out, 0, sizeof(*out));
    const char *const at = output != nullptr ? strstr(output, "the player mined ") : nullptr;
    if (at == nullptr) {
        return false;
    }
    return sscanf(at,
                  "the player mined %" SCNu64 " blocks and placed %" SCNu64
                  "; the last mined was %63[^,], and the hand holds %" SCNu64 " of %63s",
                  &out->mined, &out->placed, out->last, &out->hand_count, out->hand_item) == 5;
}

/* The name of the first item a block of that name drops, or "" for a block that drops nothing or a name that is no block. */
[[maybe_unused]] static const char *report_drop_of(const char *block) {
    for (uint32_t b = 0u; b < BLOCK_COUNT; b++) {
        if (strcmp(block_defs[b].name, block) == 0) {
            if (block_defs[b].drop_count == 0u) {
                return "";
            }
            return block_drop_items[block_drops[block_defs[b].first_drop].item].name;
        }
    }
    return "";
}

#endif
