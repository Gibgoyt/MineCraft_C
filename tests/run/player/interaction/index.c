/* ctest run_player_interaction. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "main/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- A body, a target by hand, and the buttons ---------------------------------------------------------------------- */

enum : uint32_t {
    PI_TOOLS = 7u,
    PI_TIERS = 5u,
    PI_ROLLS = 20000u,
    PI_FILE_BYTES = 4096u,
    PI_FILE_ROWS = 16u,
};

typedef struct pi_rig_t {
    test_world_t world;         /* 0 */
    player_movement_t movement; /* 24 */
    player_target_t target;     /* 136 */
    player_interaction_t hands; /* 232 */
} pi_rig_t;
static_assert(sizeof(pi_rig_t) == 1384);

/* An empty world and a body at rest well away from where the tests work, with its hands empty. */
static bool pi_begin(test_context_t *t, pi_rig_t *rig) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    player_movement_start(&rig->movement, 40.5f64, 40.5f64, 0.0f64);
    rig->movement.on_ground = 1u;
    memset(&rig->target, 0, sizeof(rig->target));
    player_interaction_start(&rig->hands, 1234u);
    return true;
}

static void pi_end(pi_rig_t *rig) {
    test_world_end(&rig->world);
}

static world_edit_t *pi_edit(pi_rig_t *rig) {
    return &rig->world.world->edit;
}

static uint16_t pi_block(pi_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    uint16_t block = UINT16_MAX;
    (void)world_map_block_get(rig->world.map, x, y, z, &block);
    return block;
}

/* What the raycast would say of a block's top face, looked at from above, or of whichever face. */
static void pi_aim_face(pi_rig_t *rig, int32_t x, int32_t y, int32_t z, uint8_t axis, int8_t sign) {
    memset(&rig->target, 0, sizeof(rig->target));
    rig->target.hit.result = RAYCAST_RESULT_HIT;
    rig->target.hit.cell[0] = x;
    rig->target.hit.cell[1] = y;
    rig->target.hit.cell[2] = z;
    rig->target.hit.block = pi_block(rig, x, y, z);
    rig->target.hit.axis = axis;
    rig->target.hit.normal_sign = sign;
}

static void pi_aim(pi_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    pi_aim_face(rig, x, y, z, COLLISION_AXIS_Z, 1);
}

static void pi_tick(pi_rig_t *rig, bool attack, bool place) {
    const player_interaction_input_t input = {.borrowed_target = &rig->target,
                                              .borrowed_movement = &rig->movement,
                                              .attack = attack ? 1u : 0u,
                                              .place = place ? 1u : 0u,
                                              ._pad0 = {0}};
    player_interaction_tick(&rig->hands, pi_edit(rig), &input);
}

/* Puts a block in by the edit, on the world's own books. */
static void pi_put(test_context_t *t, pi_rig_t *rig, int32_t x, int32_t y, int32_t z, uint16_t block) {
    CHECK_EQ_U64(world_edit_block_set(pi_edit(rig), x, y, z, block), WORLD_EDIT_STATUS_CHANGED);
}

/* Holds the left button until the cell changes, up to a limit, and says how many ticks that took. */
static uint32_t pi_mine(pi_rig_t *rig, int32_t x, int32_t y, int32_t z, uint32_t limit) {
    const uint16_t block = pi_block(rig, x, y, z);
    pi_aim(rig, x, y, z);
    for (uint32_t tick = 1u; tick <= limit; tick++) {
        pi_tick(rig, true, false);
        if (pi_block(rig, x, y, z) != block) {
            return tick;
        }
    }
    return UINT32_MAX;
}

static uint16_t pi_block_named(const char *name) {
    for (uint16_t b = 0u; b < BLOCK_COUNT; b++) {
        if (strcmp(block_defs[b].name, name) == 0) {
            return b;
        }
    }
    return BLOCK_NO_ID;
}

/* ---- Timed mining ---------------------------------------------------------------------------------------------------- */

TEST(the_time_to_mine_is_what_the_rules_say) {
    /* By hand on paper: hardness * 30 or * 100, over the speed, rounded up. */
    const uint8_t hand = BLOCK_TOOL_NONE, none = BLOCK_TIER_NONE;
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_DIRT, hand, none, true, false), 15u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_DIRT, BLOCK_TOOL_SHOVEL, BLOCK_TIER_WOOD, true, false), 8u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_DIRT, BLOCK_TOOL_SHOVEL, BLOCK_TIER_DIAMOND, true, false), 2u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_GRASS_BLOCK, hand, none, true, false),
                 18u); /* not 19: 0.6 is inexact */
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_STONE, hand, none, true, false), 150u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_STONE, BLOCK_TOOL_PICKAXE, BLOCK_TIER_WOOD, true, false), 23u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_STONE, BLOCK_TOOL_PICKAXE, BLOCK_TIER_STONE, true, false), 12u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_STONE, BLOCK_TOOL_PICKAXE, BLOCK_TIER_IRON, true, false), 8u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_STONE, BLOCK_TOOL_PICKAXE, BLOCK_TIER_DIAMOND, true, false), 6u);
    /* The wrong tool is a hand. */
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_DIRT, BLOCK_TOOL_PICKAXE, BLOCK_TIER_DIAMOND, true, false), 15u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_STONE, BLOCK_TOOL_SHOVEL, BLOCK_TIER_DIAMOND, true, false), 150u);
    /* Too low a tier is fast, as it is the right tool, and gives nothing. */
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_IRON_ORE, BLOCK_TOOL_PICKAXE, BLOCK_TIER_WOOD, true, false), 150u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_IRON_ORE, BLOCK_TOOL_PICKAXE, BLOCK_TIER_STONE, true, false), 23u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_OBSIDIAN, BLOCK_TOOL_PICKAXE, BLOCK_TIER_DIAMOND, true, false),
                 188u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_OBSIDIAN, hand, none, true, false), 5000u);
    /* Leaves, and what shears do. */
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_OAK_LEAVES, hand, none, true, false), 6u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_OAK_LEAVES, BLOCK_TOOL_SHEARS, none, true, false), 1u);
    /* A touch, and never. */
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_TALL_GRASS, hand, none, true, false), 1u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_BEDROCK, BLOCK_TOOL_PICKAXE, BLOCK_TIER_DIAMOND, true, false),
                 PLAYER_INTERACTION_TICKS_NEVER);
    /* Off the ground, under water, both. */
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_DIRT, hand, none, false, false), 75u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_DIRT, hand, none, true, true), 75u);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_DIRT, hand, none, false, true), 375u);
}

/* Reads the hardness a block's file writes as an exact count of millionths, by whole numbers and with none of the program's code. */
static bool pi_file_read(test_context_t *t, const char *name, char *out) {
    char path[256];
    snprintf(path, sizeof(path), "config/blocks/%s/index.yaml", name);
    uint64_t length = 0u;
    if (!CHECK_EQ_U64(file_read_whole(path, out, PI_FILE_BYTES - 1u, &length), FILE_OK)) {
        return false;
    }
    out[length] = '\0';
    return true;
}

static bool pi_file_hardness(const char *text, uint64_t *out_millionths) {
    const char *const field = strstr(text, "hardness: ");
    if (field == nullptr || strncmp(field + 10, "unbreakable", 11) == 0) {
        return false;
    }
    const char *at = field + 10;
    uint64_t whole = 0u, fraction = 0u, scale = 1000000u;
    for (; *at >= '0' && *at <= '9'; at++) {
        whole = whole * 10u + (uint64_t)(*at - '0');
    }
    if (*at == '.') {
        for (at++; *at >= '0' && *at <= '9'; at++) {
            scale /= 10u;
            fraction += (uint64_t)(*at - '0') * scale;
        }
    }
    *out_millionths = whole * 1000000u + fraction;
    return true;
}

TEST(every_block_in_every_hand_takes_the_time_its_file_says) {
    char *const text = test_alloc(t, PI_FILE_BYTES);
    uint32_t checked = 0u;
    for (uint16_t b = 0u; b < BLOCK_COUNT; b++) {
        REQUIRE(pi_file_read(t, block_defs[b].name, text));
        uint64_t millionths = 0u;
        const bool breakable = pi_file_hardness(text, &millionths);
        CHECK_EQ_U64((block_defs[b].flags & BLOCK_FLAG_UNBREAKABLE) != 0u ? 1u : 0u, breakable ? 0u : 1u);
        for (uint8_t tool = 0u; tool < PI_TOOLS; tool++) {
            for (uint8_t tier = 0u; tier < PI_TIERS; tier++) {
                const uint32_t ticks = player_interaction_ticks(b, tool, tier, true, false);
                if (!breakable) {
                    CHECK_EQ_U64(ticks, PLAYER_INTERACTION_TICKS_NEVER);
                    continue;
                }
                const bool own = tool != BLOCK_TOOL_NONE && tool == block_defs[b].tool;
                const bool harvest = block_defs[b].tier == BLOCK_TIER_NONE || (own && tier >= block_defs[b].tier);
                uint64_t speed_milli = 1000u;
                if (own) {
                    static const uint64_t by_tier[PI_TIERS] = {1000u, 2000u, 4000u, 6000u, 8000u};
                    speed_milli = by_tier[tier];
                    speed_milli = tool == BLOCK_TOOL_SWORD ? 1500u : speed_milli;
                    if (tool == BLOCK_TOOL_SHEARS) {
                        speed_milli = (block_defs[b].flags & BLOCK_FLAG_DECAYS) != 0u ? 15000u : 2000u;
                    }
                }
                const uint64_t work = speed_milli * 1000u;
                const uint64_t need = millionths * (harvest ? 30u : 100u);
                const uint64_t expected = need == 0u ? 1u : (need + work - 1u) / work;
                CHECK_EQ_U64(ticks, expected);
                checked++;
            }
        }
    }
    CHECK(checked > (uint32_t)BLOCK_COUNT * PI_TOOLS);
}

TEST(a_block_really_breaks_on_the_tick_it_should_and_not_before) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    for (uint16_t b = 1u; b < BLOCK_COUNT; b++) {
        if ((block_defs[b].flags & BLOCK_FLAG_UNBREAKABLE) != 0u || block_defs[b].shape == BLOCK_SHAPE_FLUID) {
            continue;
        }
        player_interaction_start(&rig.hands, 99u);
        pi_put(t, &rig, 3, 3, 5, b);
        const uint32_t expected = player_interaction_ticks(b, BLOCK_TOOL_NONE, BLOCK_TIER_NONE, true, false);
        CHECK_EQ_U64(pi_mine(&rig, 3, 3, 5, expected + 1u), expected);
        CHECK_EQ_U64(pi_block(&rig, 3, 3, 5), BLOCK_ID_AIR);
        CHECK_EQ_U64(rig.hands.mined_count, 1u);
        CHECK_EQ_U64(rig.hands.last_mined, b);
    }
    pi_end(&rig);
}

TEST(progress_goes_up_with_the_ticks_and_says_nothing_of_a_block_not_being_mined) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_DIRT);
    pi_aim(&rig, 3, 3, 5);
    CHECK_NEAR(player_interaction_progress(&rig.hands), 0.0f32, 0.0f32);
    for (uint32_t tick = 1u; tick < 15u; tick++) {
        pi_tick(&rig, true, false);
        CHECK_NEAR((float64_t)player_interaction_progress(&rig.hands), (float64_t)tick / 15.0f64, 1.0e-6f64);
    }
    pi_tick(&rig, true, false);
    CHECK_NEAR(player_interaction_progress(&rig.hands), 0.0f32, 0.0f32);
    CHECK_EQ_U64(pi_block(&rig, 3, 3, 5), BLOCK_ID_AIR);
    pi_end(&rig);
}

TEST(letting_go_or_aiming_elsewhere_or_the_block_changing_starts_the_count_again) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_DIRT);
    pi_put(t, &rig, 4, 3, 5, BLOCK_ID_DIRT);
    /* Let go one tick short: the whole 15 again. */
    pi_aim(&rig, 3, 3, 5);
    for (uint32_t i = 0u; i < 14u; i++) {
        pi_tick(&rig, true, false);
    }
    pi_tick(&rig, false, false);
    for (uint32_t i = 0u; i < 14u; i++) {
        pi_tick(&rig, true, false);
    }
    CHECK_EQ_U64(pi_block(&rig, 3, 3, 5), BLOCK_ID_DIRT);
    /* Another block one tick short: the whole 15 there too, and back again. */
    pi_aim(&rig, 4, 3, 5);
    for (uint32_t i = 0u; i < 14u; i++) {
        pi_tick(&rig, true, false);
    }
    CHECK_EQ_U64(pi_block(&rig, 4, 3, 5), BLOCK_ID_DIRT);
    pi_aim(&rig, 3, 3, 5);
    for (uint32_t i = 0u; i < 14u; i++) {
        pi_tick(&rig, true, false);
    }
    CHECK_EQ_U64(pi_block(&rig, 3, 3, 5), BLOCK_ID_DIRT);
    /* Aiming at nothing lets go as well. */
    rig.target.hit.result = RAYCAST_RESULT_MISS;
    pi_tick(&rig, true, false);
    CHECK_EQ_U64(rig.hands.mining, 0u);
    pi_aim(&rig, 3, 3, 5);
    for (uint32_t i = 0u; i < 14u; i++) {
        pi_tick(&rig, true, false);
    }
    CHECK_EQ_U64(pi_block(&rig, 3, 3, 5), BLOCK_ID_DIRT);
    /* The block changes under the aim: the old aim touches nothing. */
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_STONE);
    pi_tick(&rig, true, false);
    CHECK_EQ_U64(pi_block(&rig, 3, 3, 5), BLOCK_ID_STONE);
    CHECK_EQ_U64(rig.hands.mining, 0u);
    /* And aimed at again, it is a new block from nothing, at stone's time. */
    CHECK_EQ_U64(pi_mine(&rig, 3, 3, 5, 151u), 150u);
    pi_end(&rig);
}

TEST(what_cannot_be_mined_stays_for_ever_and_a_target_not_loaded_is_nothing) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_BEDROCK);
    CHECK_EQ_U64(pi_mine(&rig, 3, 3, 5, 20000u), UINT32_MAX);
    CHECK_EQ_U64(rig.hands.mined_count, 0u);
    /* A hit out past what is loaded. */
    pi_aim(&rig, 3, 3, 5);
    rig.target.hit.cell[0] = 4096;
    pi_tick(&rig, true, false);
    CHECK_EQ_U64(rig.hands.mining, 0u);
    pi_end(&rig);
}

TEST(a_tool_makes_it_quicker_and_the_body_being_off_the_ground_slower) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_STONE);
    player_interaction_hold_tool(&rig.hands, BLOCK_TOOL_PICKAXE, BLOCK_TIER_WOOD);
    CHECK_EQ_U64(pi_mine(&rig, 3, 3, 5, 24u), 23u);
    /* Past the delay. */
    for (uint32_t i = 0u; i < PLAYER_INTERACTION_MINE_DELAY_TICKS; i++) {
        pi_tick(&rig, false, false);
    }
    player_interaction_hold_tool(&rig.hands, BLOCK_TOOL_NONE, BLOCK_TIER_NONE);
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_DIRT);
    rig.movement.on_ground = 0u;
    CHECK_EQ_U64(pi_mine(&rig, 3, 3, 5, 76u), 75u);
    for (uint32_t i = 0u; i < PLAYER_INTERACTION_MINE_DELAY_TICKS; i++) {
        pi_tick(&rig, false, false);
    }
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_DIRT);
    rig.movement.eyes_under_water = 1u;
    CHECK_EQ_U64(pi_mine(&rig, 3, 3, 5, 376u), 375u);
    pi_end(&rig);
}

TEST(a_break_that_took_time_waits_and_one_that_took_none_does_not) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_DIRT);
    pi_put(t, &rig, 4, 3, 5, BLOCK_ID_DIRT);
    CHECK_EQ_U64(pi_mine(&rig, 3, 3, 5, 16u), 15u);
    /* 5 ticks of nothing, and then the 15. */
    CHECK_EQ_U64(pi_mine(&rig, 4, 3, 5, 21u), PLAYER_INTERACTION_MINE_DELAY_TICKS + 15u);
    /* Plants go one a tick, however many. */
    for (uint32_t i = 0u; i < PLAYER_INTERACTION_MINE_DELAY_TICKS; i++) {
        pi_tick(&rig, false, false);
    }
    for (int32_t x = 0; x < 5; x++) {
        pi_put(t, &rig, x, 5, 5, BLOCK_ID_TALL_GRASS);
    }
    for (int32_t x = 0; x < 5; x++) {
        CHECK_EQ_U64(pi_mine(&rig, x, 5, 5, 2u), 1u);
    }
    pi_end(&rig);
}

TEST(what_stands_on_a_broken_block_goes_with_it_and_gives_its_drops) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    const uint16_t cane = pi_block_named("sugar_cane");
    REQUIRE(cane != BLOCK_NO_ID);
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_DIRT);
    pi_put(t, &rig, 3, 3, 6, cane);
    pi_put(t, &rig, 3, 3, 7, cane);
    pi_put(t, &rig, 3, 3, 8, cane);
    pi_put(t, &rig, 3, 3, 10, BLOCK_ID_STONE); /* past a gap: stays */
    CHECK_EQ_U64(pi_mine(&rig, 3, 3, 5, 16u), 15u);
    for (int32_t z = 5; z <= 8; z++) {
        CHECK_EQ_U64(pi_block(&rig, 3, 3, z), BLOCK_ID_AIR);
    }
    CHECK_EQ_U64(pi_block(&rig, 3, 3, 10), BLOCK_ID_STONE);
    player_drop_t drop;
    REQUIRE(player_interaction_drop_next(&rig.hands, &drop));
    CHECK_STR_EQ(block_drop_items[drop.item].name, "dirt");
    for (int32_t z = 6; z <= 8; z++) {
        REQUIRE(player_interaction_drop_next(&rig.hands, &drop));
        CHECK_STR_EQ(block_drop_items[drop.item].name, "sugar_cane");
        CHECK_NEAR(drop.position[2], (float64_t)z + 0.5f64, 0.0f64);
    }
    CHECK(!player_interaction_drop_next(&rig.hands, &drop));
    pi_end(&rig);
}

/* ---- Drop tables ----------------------------------------------------------------------------------------------------- */

/* One row as a block's file writes it. */
typedef struct pi_file_row_t {
    char item[48];    /* 0 */
    float64_t chance; /* 48 */
    uint32_t count;   /* 56 */
    uint32_t max;     /* 60 */
} pi_file_row_t;
static_assert(sizeof(pi_file_row_t) == 64);

/* A number after a field's name, inside one row's text only. */
static bool pi_row_number(const char *row, const char *end, const char *field, float64_t *out) {
    const char *const at = strstr(row, field);
    if (at == nullptr || at >= end) {
        return false;
    }
    *out = strtod(at + strlen(field), nullptr);
    return true;
}

/* Every "{ item: ..." of the drops field: all the files write their rows in that one form. */
static uint32_t pi_file_rows(const char *text, pi_file_row_t *out) {
    const char *const field = strstr(text, "drops:");
    if (field == nullptr) {
        return UINT32_MAX;
    }
    uint32_t count = 0u;
    for (const char *row = strstr(field, "{ item: "); row != nullptr; row = strstr(row + 1, "{ item: ")) {
        const char *const end = strchr(row, '}');
        if (end == nullptr || count == PI_FILE_ROWS) {
            return UINT32_MAX;
        }
        const char *const name = row + 8;
        const uint64_t length = strcspn(name, ",");
        if (length >= sizeof(out[count].item)) {
            return UINT32_MAX;
        }
        memcpy(out[count].item, name, length);
        out[count].item[length] = '\0';
        float64_t value = 0.0f64;
        if (!pi_row_number(row, end, "count: ", &value)) {
            return UINT32_MAX;
        }
        out[count].count = (uint32_t)value;
        out[count].max = pi_row_number(row, end, "max: ", &value) ? (uint32_t)value : out[count].count;
        out[count].chance = pi_row_number(row, end, "chance: ", &value) ? value : 1.0f64;
        count++;
    }
    return count;
}

TEST(the_table_of_every_block_is_what_its_file_says) {
    char *const text = test_alloc(t, PI_FILE_BYTES);
    pi_file_row_t *const rows = test_alloc(t, PI_FILE_ROWS * sizeof(pi_file_row_t));
    uint32_t total = 0u;
    for (uint16_t b = 0u; b < BLOCK_COUNT; b++) {
        REQUIRE(pi_file_read(t, block_defs[b].name, text));
        const uint32_t count = pi_file_rows(text, rows);
        REQUIRE(count != UINT32_MAX);
        CHECK_EQ_U64(block_defs[b].drop_count, count);
        for (uint32_t r = 0u; r < count && r < block_defs[b].drop_count; r++) {
            const block_drop_t *const drop = &block_drops[block_defs[b].first_drop + r];
            CHECK_STR_EQ(block_drop_items[drop->item].name, rows[r].item);
            CHECK_EQ_U64(drop->count_min, rows[r].count);
            CHECK_EQ_U64(drop->count_max, rows[r].max);
            CHECK_NEAR((float64_t)drop->chance, rows[r].chance, 1.0e-6f64);
            total++;
        }
    }
    /* All the rows there are, so the scan above missed none of them. */
    CHECK_EQ_U64(total, BLOCK_DROP_COUNT);
}

/* The right tool of the right tier for a block. */
static void pi_tool_for(uint16_t block, uint8_t *out_tool, uint8_t *out_tier) {
    *out_tool = block_defs[block].tool;
    *out_tier = block_defs[block].tier;
}

/* Each row of chance 1 once, with its count in range, and nothing that is in no row. */
static void pi_check_drops(test_context_t *t, pi_rig_t *rig, uint16_t block) {
    const block_def_t *const def = &block_defs[block];
    uint32_t seen[PI_FILE_ROWS];
    memset(seen, 0, sizeof(seen));
    player_drop_t drop;
    while (player_interaction_drop_next(&rig->hands, &drop)) {
        bool known = false;
        for (uint32_t r = 0u; r < def->drop_count; r++) {
            const block_drop_t *const row = &block_drops[def->first_drop + r];
            if (row->item == drop.item && seen[r] == 0u) {
                known = true;
                seen[r]++;
                CHECK(drop.count >= row->count_min && drop.count <= row->count_max);
                break;
            }
        }
        CHECK(known);
    }
    for (uint32_t r = 0u; r < def->drop_count; r++) {
        if (block_drops[def->first_drop + r].chance >= 1.0f32) {
            CHECK_EQ_U64(seen[r], 1u);
        }
    }
}

TEST(every_block_mined_gives_what_its_table_says_and_too_low_a_tool_gives_nothing) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    uint32_t mined = 0u, for_nothing = 0u;
    for (uint16_t b = 1u; b < BLOCK_COUNT; b++) {
        if ((block_defs[b].flags & BLOCK_FLAG_UNBREAKABLE) != 0u || block_defs[b].shape == BLOCK_SHAPE_FLUID) {
            continue;
        }
        uint8_t tool = 0u, tier = 0u;
        pi_tool_for(b, &tool, &tier);
        /* A tier asked for is a tool held, and a shears or sword block can be had by a hand too. */
        player_interaction_start(&rig.hands, 7u + b);
        player_interaction_hold_tool(&rig.hands, tool, tier);
        pi_put(t, &rig, 3, 3, 5, b);
        const uint32_t ticks = player_interaction_ticks(b, tool, tier, true, false);
        CHECK_EQ_U64(pi_mine(&rig, 3, 3, 5, ticks), ticks);
        pi_check_drops(t, &rig, b);
        mined++;
        if (tier == BLOCK_TIER_NONE) {
            continue;
        }
        /* Bare hands, then the same tool one tier short, then another tool of the best tier: it goes, and gives nothing. */
        const uint8_t tools[3] = {BLOCK_TOOL_NONE, tool,
                                  tool == BLOCK_TOOL_PICKAXE ? BLOCK_TOOL_SHOVEL : BLOCK_TOOL_PICKAXE};
        const uint8_t tiers[3] = {BLOCK_TIER_NONE, (uint8_t)(tier - 1u), BLOCK_TIER_DIAMOND};
        for (uint32_t i = 0u; i < 3u; i++) {
            player_interaction_start(&rig.hands, 7u + b);
            player_interaction_hold_tool(&rig.hands, tools[i], tiers[i]);
            CHECK(!player_interaction_harvests(b, tools[i], tiers[i]));
            pi_put(t, &rig, 3, 3, 5, b);
            const uint32_t slow = player_interaction_ticks(b, tools[i], tiers[i], true, false);
            CHECK_EQ_U64(pi_mine(&rig, 3, 3, 5, slow), slow);
            player_drop_t drop;
            CHECK(!player_interaction_drop_next(&rig.hands, &drop));
            for_nothing++;
        }
    }
    CHECK(mined > 100u);
    CHECK(for_nothing > 20u);
    pi_end(&rig);
}

TEST(rolled_many_times_every_row_comes_out_as_often_as_its_file_says) {
    pi_file_row_t *const rows = test_alloc(t, PI_FILE_ROWS * sizeof(pi_file_row_t));
    char *const text = test_alloc(t, PI_FILE_BYTES);
    player_interaction_t *const hands = test_alloc(t, sizeof(player_interaction_t));
    player_interaction_start(hands, 55u);
    uint32_t rows_rolled = 0u;
    for (uint16_t b = 0u; b < BLOCK_COUNT; b++) {
        REQUIRE(pi_file_read(t, block_defs[b].name, text));
        const uint32_t count = pi_file_rows(text, rows);
        REQUIRE(count != UINT32_MAX);
        uint32_t seen[PI_FILE_ROWS];
        uint64_t items[PI_FILE_ROWS];
        memset(seen, 0, sizeof(seen));
        memset(items, 0, sizeof(items));
        for (uint32_t roll = 0u; roll < PI_ROLLS; roll++) {
            player_interaction_drops_spawn(hands, b, -3, 2, 7);
            player_drop_t drop;
            while (player_interaction_drop_next(hands, &drop)) {
                bool known = false;
                for (uint32_t r = 0u; r < count; r++) {
                    if (strcmp(block_drop_items[drop.item].name, rows[r].item) == 0) {
                        known = true;
                        seen[r]++;
                        items[r] += drop.count;
                        CHECK(drop.count >= rows[r].count && drop.count <= rows[r].max);
                        break;
                    }
                }
                CHECK(known);
                CHECK_NEAR(drop.position[0], -2.5f64, 0.0f64);
                CHECK_NEAR(drop.position[1], 2.5f64, 0.0f64);
                CHECK_NEAR(drop.position[2], 7.5f64, 0.0f64);
            }
        }
        for (uint32_t r = 0u; r < count; r++) {
            const float64_t chance = rows[r].chance;
            const float64_t expected = chance * (float64_t)PI_ROLLS;
            /* Six standard deviations: over about 150 rows, a false alarm is one in a few million runs. */
            const float64_t spread = 6.0f64 * sqrt(expected * (1.0f64 - chance)) + 1.0f64;
            CHECK_NEAR((float64_t)seen[r], expected, spread);
            /* The counts' average is the middle of their range, within what that many rolls can stray. */
            if (seen[r] > 0u && rows[r].max > rows[r].count) {
                const float64_t middle = ((float64_t)rows[r].count + (float64_t)rows[r].max) / 2.0f64;
                CHECK_NEAR((float64_t)items[r] / (float64_t)seen[r], middle,
                           6.0f64 * ((float64_t)rows[r].max - (float64_t)rows[r].count) / sqrt((float64_t)seen[r]));
            }
            rows_rolled++;
        }
    }
    CHECK_EQ_U64(rows_rolled, BLOCK_DROP_COUNT);
    CHECK_EQ_U64(hands->drops_lost_count, 0u);
}

/* The block a row of the table names must be what the row says, for the ones that tell a player the most. */
static void pi_check_only_drop(test_context_t *t, const char *block, const char *item) {
    const uint16_t b = pi_block_named(block);
    REQUIRE(b != BLOCK_NO_ID);
    REQUIRE(block_defs[b].drop_count == 1u);
    const block_drop_t *const row = &block_drops[block_defs[b].first_drop];
    CHECK_STR_EQ(block_drop_items[row->item].name, item);
    CHECK_NEAR((float64_t)row->chance, 1.0f64, 0.0f64);
    CHECK_EQ_U64(row->count_min, 1u);
    CHECK_EQ_U64(row->count_max, 1u);
}

TEST(dirt_drops_dirt_and_so_on_as_written_here_by_hand) {
    pi_check_only_drop(t, "dirt", "dirt");
    pi_check_only_drop(t, "grass_block", "dirt");
    pi_check_only_drop(t, "stone", "cobblestone");
    pi_check_only_drop(t, "coal_ore", "coal");
    pi_check_only_drop(t, "iron_ore", "raw_iron");
    CHECK_EQ_U64(block_defs[BLOCK_ID_GLASS].drop_count, 0u);
    CHECK_EQ_U64(block_defs[BLOCK_ID_BEDROCK].drop_count, 0u);
    CHECK_EQ_U64(block_defs[BLOCK_ID_AIR].drop_count, 0u);

    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_DIRT);
    CHECK_EQ_U64(pi_mine(&rig, 3, 3, 5, 15u), 15u);
    player_drop_t drop;
    REQUIRE(player_interaction_drop_next(&rig.hands, &drop));
    CHECK_STR_EQ(block_drop_items[drop.item].name, "dirt");
    CHECK_EQ_U64(block_drop_items[drop.item].block, BLOCK_ID_DIRT);
    CHECK_EQ_U64(drop.count, 1u);
    CHECK_NEAR(drop.position[0], 3.5f64, 0.0f64);
    CHECK_NEAR(drop.position[1], 3.5f64, 0.0f64);
    CHECK_NEAR(drop.position[2], 5.5f64, 0.0f64);
    CHECK(!player_interaction_drop_next(&rig.hands, &drop));
    pi_end(&rig);
}

/* ---- The ring and the hand -------------------------------------------------------------------------------------------- */

TEST(a_full_ring_loses_the_oldest_and_counts_them) {
    player_interaction_t *const hands = test_alloc(t, sizeof(player_interaction_t));
    player_interaction_start(hands, 1u);
    const uint32_t extra = 5u;
    for (uint32_t i = 0u; i < PLAYER_INTERACTION_DROPS + extra; i++) {
        player_interaction_drops_spawn(hands, BLOCK_ID_DIRT, (int32_t)i, 0, 0);
    }
    CHECK_EQ_U64(hands->drops_lost_count, extra);
    CHECK_EQ_U64(hands->drop_count, PLAYER_INTERACTION_DROPS);
    player_drop_t drop;
    for (uint32_t i = 0u; i < PLAYER_INTERACTION_DROPS; i++) {
        REQUIRE(player_interaction_drop_next(hands, &drop));
        CHECK_NEAR(drop.position[0], (float64_t)(i + extra) + 0.5f64, 0.0f64);
    }
    CHECK(!player_interaction_drop_next(hands, &drop));
}

TEST(the_hand_is_one_slot_and_only_blocks_go_in_it) {
    player_interaction_t *const hands = test_alloc(t, sizeof(player_interaction_t));
    player_interaction_start(hands, 1u);
    player_interaction_hand_give(hands, BLOCK_ID_DIRT, 60u);
    player_interaction_hand_give(hands, BLOCK_ID_DIRT, 60u);
    CHECK_EQ_U64(hands->hand_count, PLAYER_INTERACTION_HAND_MAX);
    player_interaction_hand_give(hands, BLOCK_ID_STONE, 2u);
    CHECK_EQ_U64(hands->hand_block, BLOCK_ID_STONE);
    CHECK_EQ_U64(hands->hand_count, 2u);
    /* Coal is no block, so the hand keeps what it has. */
    player_interaction_drops_spawn(hands, BLOCK_ID_COAL_ORE, 0, 0, 0);
    player_interaction_drops_to_hand(hands);
    CHECK_EQ_U64(hands->hand_block, BLOCK_ID_STONE);
    CHECK_EQ_U64(hands->hand_count, 2u);
    CHECK_EQ_U64(hands->drops_unheld_count, 1u);
    /* Dirt from grass goes in, as dirt. */
    player_interaction_drops_spawn(hands, BLOCK_ID_GRASS_BLOCK, 0, 0, 0);
    player_interaction_drops_to_hand(hands);
    CHECK_EQ_U64(hands->hand_block, BLOCK_ID_DIRT);
    CHECK_EQ_U64(hands->hand_count, 1u);
    CHECK_EQ_U64(hands->drop_count, 0u);
}

TEST(the_same_seed_gives_the_same_bytes_and_nothing_comes_from_the_heap) {
    pi_rig_t *const rig = test_alloc(t, sizeof(pi_rig_t));
    REQUIRE(pi_begin(t, rig));
    const uint16_t gravel = pi_block_named("gravel");
    REQUIRE(gravel != BLOCK_NO_ID);
    player_interaction_t *const copy = test_alloc(t, sizeof(player_interaction_t));
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        player_interaction_start(&rig->hands, 4242u);
        pi_put(t, rig, 3, 3, 5, BLOCK_ID_STONE);
        heap_counters_t before, after;
        heap_counters_read(&before);
        for (uint32_t i = 0u; i < 400u; i++) {
            const uint16_t block = pi_block(rig, 3, 3, 5);
            if (block == BLOCK_ID_AIR) {
                CHECK_EQ_U64(world_edit_block_set(pi_edit(rig), 3, 3, 5, gravel), WORLD_EDIT_STATUS_CHANGED);
            }
            pi_aim(rig, 3, 3, 5);
            pi_tick(rig, true, false);
        }
        heap_counters_read(&after);
        CHECK_MEM_EQ(&before, &after, sizeof(before));
        CHECK(rig->hands.mined_count > 2u);
        if (pass == 0u) {
            memcpy(copy, &rig->hands, sizeof(*copy));
            /* Back to how it started, as the block it ended on may differ. */
            (void)world_edit_block_set(pi_edit(rig), 3, 3, 5, BLOCK_ID_AIR);
        } else {
            CHECK_MEM_EQ(copy, &rig->hands, sizeof(*copy));
        }
    }
    pi_end(rig);
}

/* ---- Placing ---------------------------------------------------------------------------------------------------------- */

/* A face aimed at, and the cell that puts a block in. */
typedef struct pi_face_t {
    uint8_t axis;    /* 0 */
    int8_t sign;     /* 1 */
    int8_t _pad0[2]; /* 2 */
    int32_t x;       /* 4 */
    int32_t y;       /* 8 */
    int32_t z;       /* 12 */
} pi_face_t;
static_assert(sizeof(pi_face_t) == 16);

TEST(a_block_goes_on_whichever_of_the_six_faces_was_aimed_at) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_STONE);
    static const pi_face_t faces[] = {
        {COLLISION_AXIS_X, 1, {0}, 4, 3, 5}, {COLLISION_AXIS_X, -1, {0}, 2, 3, 5},
        {COLLISION_AXIS_Y, 1, {0}, 3, 4, 5}, {COLLISION_AXIS_Y, -1, {0}, 3, 2, 5},
        {COLLISION_AXIS_Z, 1, {0}, 3, 3, 6}, {COLLISION_AXIS_Z, -1, {0}, 3, 3, 4},
    };
    for (uint32_t i = 0u; i < sizeof(faces) / sizeof(faces[0]); i++) {
        player_interaction_start(&rig.hands, 1u);
        player_interaction_hand_give(&rig.hands, BLOCK_ID_DIRT, 2u);
        pi_aim_face(&rig, 3, 3, 5, faces[i].axis, faces[i].sign);
        pi_tick(&rig, false, true);
        CHECK_EQ_U64(pi_block(&rig, faces[i].x, faces[i].y, faces[i].z), BLOCK_ID_DIRT);
        CHECK_EQ_U64(rig.hands.hand_count, 1u);
        CHECK_EQ_U64(rig.hands.placed_count, 1u);
        pi_put(t, &rig, faces[i].x, faces[i].y, faces[i].z, BLOCK_ID_AIR);
    }
    pi_end(&rig);
}

TEST(what_can_be_overwritten_is_and_what_cannot_stops_it) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    player_interaction_hand_give(&rig.hands, BLOCK_ID_DIRT, 8u);
    /* Aiming at tall grass replaces it, not the cell over it. */
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_TALL_GRASS);
    pi_aim(&rig, 3, 3, 5);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 3, 3, 5), BLOCK_ID_DIRT);
    CHECK_EQ_U64(pi_block(&rig, 3, 3, 6), BLOCK_ID_AIR);
    /* In front of a face: water gives way. */
    rig.hands.place_delay = 0u;
    pi_put(t, &rig, 5, 3, 5, BLOCK_ID_STONE);
    pi_put(t, &rig, 5, 3, 6, BLOCK_ID_WATER);
    pi_aim(&rig, 5, 3, 5);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 5, 3, 6), BLOCK_ID_DIRT);
    /* A flower in front of a face stops it: it is not replaceable, and neither is what the ray hit. */
    rig.hands.place_delay = 0u;
    const uint8_t left = rig.hands.hand_count;
    pi_put(t, &rig, 7, 3, 5, BLOCK_ID_STONE);
    const uint16_t dandelion = pi_block_named("dandelion");
    REQUIRE(dandelion != BLOCK_NO_ID && (block_defs[dandelion].flags & BLOCK_FLAG_REPLACEABLE) == 0u);
    pi_put(t, &rig, 7, 3, 6, dandelion);
    pi_aim(&rig, 7, 3, 5);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 7, 3, 6), dandelion);
    CHECK_EQ_U64(pi_block(&rig, 7, 3, 5), BLOCK_ID_STONE);
    CHECK_EQ_U64(rig.hands.hand_count, left);
    pi_end(&rig);
}

TEST(nothing_goes_where_the_body_is_but_it_may_touch) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    test_world_fill(t, &rig.world, -8, -8, -1, 8, 8, -1, BLOCK_ID_STONE);
    player_movement_start(&rig.movement, 0.5f64, 0.5f64, 0.0f64);
    rig.movement.on_ground = 1u;
    player_interaction_hand_give(&rig.hands, BLOCK_ID_DIRT, 16u);
    const uint8_t start = rig.hands.hand_count;
    /* Feet, and head. Both refused, and nothing spent. */
    pi_aim(&rig, 0, 0, -1);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 0, 0, 0), BLOCK_ID_AIR);
    pi_put(t, &rig, 0, 0, 2, BLOCK_ID_STONE);
    pi_aim_face(&rig, 0, 0, 2, COLLISION_AXIS_Z, -1);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 0, 0, 1), BLOCK_ID_AIR);
    CHECK_EQ_U64(rig.hands.hand_count, start);
    /* Right next to it, touching nothing: goes. */
    pi_aim(&rig, 1, 0, -1);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 1, 0, 0), BLOCK_ID_DIRT);
    /* The body at a corner of its cell reaches into the next one: refused there. */
    rig.hands.place_delay = 0u;
    rig.movement.position[0] = 0.9f64;
    pi_put(t, &rig, 1, 0, 0, BLOCK_ID_AIR);
    pi_aim(&rig, 1, 0, -1);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 1, 0, 0), BLOCK_ID_AIR);
    /* Exactly touching: the body's side at x = 1.0 and the block's at 1.0. */
    rig.movement.position[0] = 1.0f64 - PLAYER_MOVEMENT_WIDTH / 2.0f64;
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 1, 0, 0), BLOCK_ID_DIRT);
    /* Crouched is shorter. The cell from z = 1 to 2 is in the way of a body standing at -0.5, up to 1.3, and not crouched, up to 1. */
    rig.hands.place_delay = 0u;
    rig.movement.position[0] = 0.5f64;
    rig.movement.position[2] = -0.5f64;
    pi_aim_face(&rig, 0, 0, 2, COLLISION_AXIS_Z, -1);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 0, 0, 1), BLOCK_ID_AIR);
    rig.movement.crouched = 1u;
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 0, 0, 1), BLOCK_ID_DIRT);
    pi_end(&rig);
}

TEST(a_slab_goes_under_the_feet_of_a_body_above_it) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    test_world_fill(t, &rig.world, -8, -8, -1, 8, 8, -1, BLOCK_ID_STONE);
    uint16_t slab = BLOCK_NO_ID;
    for (uint16_t b = 0u; b < BLOCK_COUNT; b++) {
        if (block_defs[b].shape == BLOCK_SHAPE_SLAB) {
            slab = b;
            break;
        }
    }
    REQUIRE(slab != BLOCK_NO_ID);
    player_interaction_hand_give(&rig.hands, slab, 2u);
    /* A body hanging at 0.6 over this cell clears half a cell's slab, and not a whole block. */
    player_movement_start(&rig.movement, 0.5f64, 0.5f64, 0.6f64);
    pi_aim(&rig, 0, 0, -1);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 0, 0, 0), slab);
    pi_put(t, &rig, 0, 0, 0, BLOCK_ID_AIR);
    player_interaction_hand_give(&rig.hands, BLOCK_ID_DIRT, 2u);
    rig.hands.place_delay = 0u;
    pi_aim(&rig, 0, 0, -1);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 0, 0, 0), BLOCK_ID_AIR);
    pi_end(&rig);
}

TEST(it_places_nothing_at_the_top_of_the_world_or_in_a_column_not_loaded_or_from_an_empty_hand) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    /* An empty hand. */
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_STONE);
    pi_aim(&rig, 3, 3, 5);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 3, 3, 6), BLOCK_ID_AIR);
    CHECK_EQ_U64(rig.hands.placed_count, 0u);
    /* The top: one may go at the last height, and none past it. */
    player_interaction_hand_give(&rig.hands, BLOCK_ID_DIRT, 8u);
    pi_put(t, &rig, 3, 3, COLUMN_Z_GAMEPLAY_MAX - 1, BLOCK_ID_STONE);
    pi_aim(&rig, 3, 3, COLUMN_Z_GAMEPLAY_MAX - 1);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 3, 3, COLUMN_Z_GAMEPLAY_MAX), BLOCK_ID_DIRT);
    CHECK_EQ_U64(rig.hands.placed_count, 1u);
    rig.hands.place_delay = 0u;
    pi_aim(&rig, 3, 3, COLUMN_Z_GAMEPLAY_MAX);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(rig.hands.placed_count, 1u);
    /* A face on the last loaded column, looking at one that has not come. Radius 3 reaches 63 at y = 0 and no further. */
    pi_put(t, &rig, 63, 0, 5, BLOCK_ID_STONE);
    pi_aim_face(&rig, 63, 0, 5, COLLISION_AXIS_X, 1);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(rig.hands.placed_count, 1u);
    /* From inside what it aims at, no face was hit. */
    pi_aim_face(&rig, 3, 3, 5, RAYCAST_AXIS_NONE, 0);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(rig.hands.placed_count, 1u);
    pi_end(&rig);
}

TEST(a_plant_needs_ground_and_will_not_wash_a_fluid_away) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    const uint16_t dandelion = pi_block_named("dandelion");
    REQUIRE(dandelion != BLOCK_NO_ID);
    player_interaction_hand_give(&rig.hands, dandelion, 8u);
    /* On the side of a wall, with air below it. */
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_STONE);
    pi_aim_face(&rig, 3, 3, 5, COLLISION_AXIS_X, 1);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 4, 3, 5), BLOCK_ID_AIR);
    /* On top. */
    pi_aim(&rig, 3, 3, 5);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 3, 3, 6), dandelion);
    rig.hands.place_delay = 0u;
    /* In water, on top of stone. */
    pi_put(t, &rig, 5, 3, 5, BLOCK_ID_STONE);
    pi_put(t, &rig, 5, 3, 6, BLOCK_ID_WATER);
    pi_aim(&rig, 5, 3, 5);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(pi_block(&rig, 5, 3, 6), BLOCK_ID_WATER);
    CHECK_EQ_U64(rig.hands.placed_count, 1u);
    pi_end(&rig);
}

TEST(held_it_places_every_four_ticks_and_mining_takes_the_turn_of_placing) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    player_interaction_hand_give(&rig.hands, BLOCK_ID_DIRT, 8u);
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_STONE);
    uint32_t last = 0u;
    int32_t top = 5;
    for (uint32_t tick = 1u; tick <= 17u; tick++) {
        pi_aim(&rig, 3, 3, top);
        pi_tick(&rig, false, true);
        if (rig.hands.placed_count != last) {
            CHECK_EQ_U64((tick - 1u) % PLAYER_INTERACTION_PLACE_EVERY_TICKS, 0u);
            last = (uint32_t)rig.hands.placed_count;
            top++;
        }
    }
    CHECK_EQ_U64(rig.hands.placed_count, 5u);
    CHECK_EQ_U64(rig.hands.hand_count, 3u);
    /* It runs out and stops. */
    for (uint32_t tick = 0u; tick < 40u; tick++) {
        pi_aim(&rig, 3, 3, top);
        pi_tick(&rig, false, true);
        top = pi_block(&rig, 3, 3, top + 1) != BLOCK_ID_AIR ? top + 1 : top;
    }
    CHECK_EQ_U64(rig.hands.placed_count, 8u);
    CHECK_EQ_U64(rig.hands.hand_count, 0u);
    /* Both buttons: only the left counts. */
    player_interaction_hand_give(&rig.hands, BLOCK_ID_DIRT, 8u);
    pi_put(t, &rig, 8, 3, 5, BLOCK_ID_DIRT);
    pi_aim(&rig, 8, 3, 5);
    const uint64_t placed = rig.hands.placed_count;
    for (uint32_t tick = 0u; tick < 15u; tick++) {
        pi_tick(&rig, true, true);
    }
    CHECK_EQ_U64(rig.hands.placed_count, placed);
    CHECK_EQ_U64(pi_block(&rig, 8, 3, 5), BLOCK_ID_AIR);
    pi_end(&rig);
}

TEST(mined_and_placed_blocks_go_on_the_books_for_everyone_who_reads_them) {
    pi_rig_t rig;
    REQUIRE(pi_begin(t, &rig));
    world_edit_t *const edit = pi_edit(&rig);
    pi_put(t, &rig, 3, 3, 5, BLOCK_ID_DIRT);
    uint64_t cursor = world_edit_log_position(edit);
    for (uint32_t consumer = 0u; consumer < WORLD_EDIT_CONSUMER_COUNT; consumer++) {
        world_edit_dirty_t dirty;
        while (world_edit_dirty_next(edit, (world_edit_consumer_t)consumer, &dirty)) {
        }
    }
    CHECK_EQ_U64(pi_mine(&rig, 3, 3, 5, 15u), 15u);
    world_edit_record_t records[4];
    uint64_t lost = 0u;
    CHECK_EQ_U64(world_edit_log_read(edit, &cursor, records, 4u, &lost), 1u);
    CHECK_EQ_U64(lost, 0u);
    CHECK_EQ_I64(records[0].x, 3);
    CHECK_EQ_I64(records[0].y, 3);
    CHECK_EQ_I64(records[0].z, 5);
    CHECK_EQ_U64(records[0].before, BLOCK_ID_DIRT);
    CHECK_EQ_U64(records[0].after, BLOCK_ID_AIR);
    for (uint32_t consumer = 0u; consumer < WORLD_EDIT_CONSUMER_COUNT; consumer++) {
        world_edit_dirty_t dirty;
        REQUIRE(world_edit_dirty_next(edit, (world_edit_consumer_t)consumer, &dirty));
        CHECK_EQ_U64(dirty.section_mask, 1ull << column_section_of(5));
    }
    /* And placing it back is one more. */
    player_interaction_drops_to_hand(&rig.hands);
    pi_put(t, &rig, 3, 3, 4, BLOCK_ID_STONE);
    cursor = world_edit_log_position(edit);
    pi_aim(&rig, 3, 3, 4);
    pi_tick(&rig, false, true);
    CHECK_EQ_U64(world_edit_log_read(edit, &cursor, records, 4u, &lost), 1u);
    CHECK_EQ_U64(records[0].before, BLOCK_ID_AIR);
    CHECK_EQ_U64(records[0].after, BLOCK_ID_DIRT);
    pi_end(&rig);
}

/* ---- Through the player ----------------------------------------------------------------------------------------------- */

typedef struct pi_player_rig_t {
    test_world_t world; /* 0 */
    player_t *player;   /* 24 */
    int64_t now_ns;     /* 32 */
} pi_player_rig_t;
static_assert(sizeof(pi_player_rig_t) == 40);

/* Stone with its top at z = 0, with a body standing on it. */
static bool pi_player_begin(test_context_t *t, pi_player_rig_t *rig) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    test_world_fill(t, &rig->world, -16, -16, -1, 32, 32, -1, BLOCK_ID_STONE);
    rig->player = test_world_aligned(t, sizeof(player_t));
    player_init(rig->player, rig->world.physics);
    player_spawn(rig->player, 0.5f64, 0.5f64, 0.0f64);
    rig->now_ns = 1000000000;
    return true;
}

static void pi_player_end(pi_player_rig_t *rig) {
    player_deinit(rig->player);
    test_world_end(&rig->world);
}

/* The two ticks in the order game_sim_tick has them, with these keys and buttons sent first. */
static void pi_player_tick(pi_player_rig_t *rig, int8_t forward, float32_t yaw, float32_t pitch, bool attack,
                           bool place) {
    const player_intent_t intent = {
        .yaw = yaw, .forward = forward, .right = 0, .jump = 0u, .sneak = 0u, .sprint = 0u, ._pad0 = {0}};
    rig->now_ns += PLAYER_TICK_NS;
    player_send(rig->player, &intent, rig->now_ns);
    player_look_send(rig->player, yaw, pitch);
    player_hands_send(rig->player, attack, place);
    player_interact_tick(rig->player, &rig->world.world->edit, rig->now_ns);
    player_tick(rig->player, rig->world.map, rig->now_ns);
}

TEST(a_click_between_two_ticks_counts_and_no_one_at_the_controls_lets_go) {
    pi_player_rig_t rig;
    REQUIRE(pi_player_begin(t, &rig));
    player_t *const player = rig.player;
    /* A plant at the feet's front, looked down at: breaks at a touch. */
    test_world_fill(t, &rig.world, 2, 0, 0, 2, 0, 0, BLOCK_ID_TALL_GRASS);
    const float32_t pitch = -0.6f32;
    pi_player_tick(&rig, 0, 0.0f32, pitch, false, false);
    REQUIRE(player->target.hit.result == RAYCAST_RESULT_HIT);
    CHECK_EQ_U64(player->target.hit.block, BLOCK_ID_TALL_GRASS);

    /* Pressed and let go between two ticks. */
    player_hands_send(player, true, false);
    rig.now_ns += PLAYER_TICK_NS;
    const player_intent_t intent = {
        .yaw = 0.0f32, .forward = 0, .right = 0, .jump = 0u, .sneak = 0u, .sprint = 0u, ._pad0 = {0}};
    player_send(player, &intent, rig.now_ns);
    player_hands_send(player, false, false);
    player_interact_tick(player, &rig.world.world->edit, rig.now_ns);
    player_tick(player, rig.world.map, rig.now_ns);
    uint16_t block = UINT16_MAX;
    REQUIRE(world_map_block_get(rig.world.map, 2, 0, 0, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    CHECK_EQ_U64(player->interaction.mined_count, 1u);

    /* The latch is one tick's: dirt is not worked on any further. */
    test_world_fill(t, &rig.world, 2, 0, 0, 2, 0, 0, BLOCK_ID_DIRT);
    pi_player_tick(&rig, 0, 0.0f32, pitch, false, false);
    REQUIRE(player->target.hit.block == BLOCK_ID_DIRT);
    pi_player_tick(&rig, 0, 0.0f32, pitch, true, false);
    CHECK_EQ_U64(player->interaction.mining, 1u);
    /* Main stops, as on another VT, with the button held: the tick after the limit it has let go. */
    player_hands_send(player, true, false);
    for (int64_t waited = 0; waited <= PLAYER_INTENT_STALE_NS; waited += PLAYER_TICK_NS) {
        rig.now_ns += PLAYER_TICK_NS;
        player_interact_tick(player, &rig.world.world->edit, rig.now_ns);
        player_tick(player, rig.world.map, rig.now_ns);
    }
    CHECK_EQ_U64(player->interaction.mining, 0u);
    pi_player_end(&rig);
}

TEST(a_body_walks_to_a_wall_of_dirt_mines_it_walks_through_and_puts_it_back) {
    pi_player_rig_t rig;
    REQUIRE(pi_player_begin(t, &rig));
    player_t *const player = rig.player;
    /* A wall of dirt 3 high across its way east, at x = 6. */
    test_world_fill(t, &rig.world, 6, -8, 0, 6, 8, 2, BLOCK_ID_DIRT);
    /* Walks east until the wall stops it. */
    for (uint32_t i = 0u; i < 60u; i++) {
        pi_player_tick(&rig, 1, 0.0f32, 0.0f32, false, false);
    }
    CHECK_NEAR(player->movement.position[0], 6.0f64 - PLAYER_MOVEMENT_WIDTH / 2.0f64, 1.0e-6f64);
    const float64_t stopped_x = player->movement.position[0];
    /* The eye is at 1.62: it aims at the upper of the two cells a body needs. */
    REQUIRE(player->target.hit.result == RAYCAST_RESULT_HIT);
    CHECK_EQ_I64(player->target.hit.cell[0], 6);
    CHECK_EQ_I64(player->target.hit.cell[2], 1);
    CHECK_EQ_U64(player->target.hit.block, BLOCK_ID_DIRT);
    /* 15 ticks by hand, and the tick that aims has passed already. */
    for (uint32_t i = 0u; i < 14u; i++) {
        pi_player_tick(&rig, 0, 0.0f32, 0.0f32, true, false);
    }
    uint16_t block = UINT16_MAX;
    REQUIRE(world_map_block_get(rig.world.map, 6, 0, 1, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_DIRT);
    pi_player_tick(&rig, 0, 0.0f32, 0.0f32, true, false);
    REQUIRE(world_map_block_get(rig.world.map, 6, 0, 1, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    CHECK_EQ_U64(player->interaction.hand_block, BLOCK_ID_DIRT);
    CHECK_EQ_U64(player->interaction.hand_count, 1u);
    /* The next aim is already at what it uncovered, or past it. */
    CHECK(player->target.hit.result != RAYCAST_RESULT_HIT || player->target.hit.cell[0] > 6 ||
          player->target.hit.cell[2] != 1);
    /* Down at the lower cell: the delay, then the 15. */
    const float32_t down = -0.75f32;
    pi_player_tick(&rig, 0, 0.0f32, down, false, false);
    REQUIRE(player->target.hit.result == RAYCAST_RESULT_HIT);
    CHECK_EQ_I64(player->target.hit.cell[0], 6);
    CHECK_EQ_I64(player->target.hit.cell[2], 0);
    for (uint32_t i = 0u; i < 20u; i++) {
        pi_player_tick(&rig, 0, 0.0f32, down, true, false);
    }
    REQUIRE(world_map_block_get(rig.world.map, 6, 0, 0, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    CHECK_EQ_U64(player->interaction.hand_count, 2u);
    /* It walks through the gap, which it could not before, and stops two blocks past it. */
    while (player->movement.position[0] < 8.0f64) {
        const uint64_t ticks = player->tick_count;
        pi_player_tick(&rig, 1, 0.0f32, 0.0f32, false, false);
        REQUIRE(ticks < 400u);
    }
    for (uint32_t i = 0u; i < 20u; i++) {
        pi_player_tick(&rig, 0, 0.0f32, 0.0f32, false, false);
    }
    CHECK(player->movement.position[0] > stopped_x + 2.0f64);
    /* Turns back, and aims at the floor of the gap, at its middle, which is 2 or so blocks off and in reach. */
    const float32_t west = 3.14159265f32;
    const float64_t eye_z = player->movement.position[2] + player->movement.eye_height;
    const float32_t to_gap = -(float32_t)atan2(eye_z, player->movement.position[0] - 6.5f64);
    pi_player_tick(&rig, 0, west, to_gap, false, false);
    REQUIRE(player->target.hit.result == RAYCAST_RESULT_HIT);
    CHECK_EQ_I64(player->target.hit.cell[0], 6);
    CHECK_EQ_I64(player->target.hit.cell[1], 0);
    CHECK_EQ_I64(player->target.hit.cell[2], -1);
    pi_player_tick(&rig, 0, west, to_gap, false, true);
    REQUIRE(world_map_block_get(rig.world.map, 6, 0, 0, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_DIRT);
    CHECK_EQ_U64(player->interaction.hand_count, 1u);
    CHECK_EQ_U64(player->interaction.placed_count, 1u);
    pi_player_end(&rig);
}

/* ---- The real game ---------------------------------------------------------------------------------------------------- */

enum : uint32_t {
    PI_WIDTH = 320u,
    PI_HEIGHT = 180u,
    PI_PROGRAM_TIMEOUT_MS = 120000u,
};

/* A rig: everything a real run has, on memory of its own. */
typedef struct pi_game_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
    physics_t *physics;          /* 64 */
    player_t *player;            /* 72 */
} pi_game_rig_t;
static_assert(sizeof(pi_game_rig_t) == 80);

static bool pi_game_begin(test_context_t *t, pi_game_rig_t *rig) {
    rig->bridge = test_world_aligned(t, sizeof(gpu_bridge_t));
    rig->job = test_world_aligned(t, sizeof(job_system_t));
    rig->world = test_world_aligned(t, sizeof(world_t));
    rig->terrain = test_world_aligned(t, sizeof(terrain_t));
    rig->render = test_world_aligned(t, sizeof(render_t));
    rig->texture = test_world_aligned(t, sizeof(texture_t));
    rig->game = test_world_aligned(t, sizeof(game_t));
    rig->physics = test_world_aligned(t, sizeof(physics_t));
    rig->player = test_world_aligned(t, sizeof(player_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) || !CHECK(physics_init(rig->physics)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    player_init(rig->player, rig->physics);
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    game_player_set(rig->game, rig->player);
    return true;
}

static void pi_game_end(test_context_t *t, pi_game_rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    player_deinit(rig->player);
    physics_deinit(rig->physics);
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

static const game_config_t pi_small_game = {
    .render_distance = 5u, .width = PI_WIDTH, .height = PI_HEIGHT, ._pad0 = {0}};

/* Frames with these buttons, until the condition holds or the seconds have gone. */
static bool pi_frames(pi_game_rig_t *rig, display_buffer_t *target, bool attack, bool place, bool jump,
                      float64_t seconds, uint64_t mined_at_least, uint64_t placed_at_least) {
    render_fly_input_t none;
    memset(&none, 0, sizeof(none));
    const int64_t give_up = time_now_ns() + (int64_t)(seconds * 1.0e9f64);
    const player_interaction_t *const hands = &rig->player->interaction;
    bool ok = true;
    while (time_now_ns() < give_up && (hands->mined_count < mined_at_least || hands->placed_count < placed_at_least)) {
        game_walk_send(rig->game, &none, jump, false, time_now_ns());
        game_hands_send(rig->game, attack, place);
        ok = game_loop_frame(rig->game, &none, 0.0f32, target) && ok;
        time_sleep_ns(4000000);
    }
    return ok && hands->mined_count >= mined_at_least && hands->placed_count >= placed_at_least;
}

TEST(the_real_threads_mine_and_place_a_real_world_and_the_picture_changes) {
    pi_game_rig_t rig;
    REQUIRE(pi_game_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &pi_small_game));
    const uint64_t bytes = post_xrgb_bytes(PI_WIDTH, PI_HEIGHT);
    display_buffer_t target = {.owned_map = test_world_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = PI_WIDTH * 4u,
                               .width = PI_WIDTH,
                               .height = PI_HEIGHT};
    uint8_t *const before = test_alloc(t, bytes);
    uint8_t *const after = test_alloc(t, bytes);
    /* Straight down from where it starts: it stands on what it will mine. */
    game->camera.pitch = -1.55f32;
    REQUIRE(game_loop_start(game, nullptr));
    /* Long enough for the ground to come, the body to land and the picture to be of it. */
    render_fly_input_t none;
    memset(&none, 0, sizeof(none));
    const int64_t landed_by = time_now_ns() + 30000000000;
    while (time_now_ns() < landed_by &&
           (rig.player->movement.on_ground == 0u || rig.player->target.hit.result != RAYCAST_RESULT_HIT)) {
        game_walk_send(game, &none, false, false, time_now_ns());
        CHECK(game_loop_frame(game, &none, 0.0f32, &target));
        time_sleep_ns(4000000);
    }
    REQUIRE(rig.player->target.hit.result == RAYCAST_RESULT_HIT);
    const int64_t settled_by = time_now_ns() + 1000000000;
    while (time_now_ns() < settled_by) {
        game_walk_send(game, &none, false, false, time_now_ns());
        CHECK(game_loop_frame(game, &none, 0.0f32, &target));
        time_sleep_ns(4000000);
    }
    memcpy(before, target.owned_map, bytes);
    const float64_t feet = rig.player->movement.position[2];
    /* Down through what it stands on. It falls a little each time, so it goes on mining what is next. */
    CHECK(pi_frames(&rig, &target, true, false, false, 40.0f64, 1u, 0u));
    CHECK(rig.player->interaction.mined_count >= 1u);
    const int64_t show_by = time_now_ns() + 1500000000;
    while (time_now_ns() < show_by) {
        game_walk_send(game, &none, false, false, time_now_ns());
        game_hands_send(game, false, false);
        CHECK(game_loop_frame(game, &none, 0.0f32, &target));
        time_sleep_ns(4000000);
    }
    memcpy(after, target.owned_map, bytes);
    CHECK(memcmp(before, after, bytes) != 0);
    CHECK(rig.player->movement.position[2] < feet); /* it fell into the hole */
    /* Jumps, and with its feet clear of the hole, the block it got goes back under them, still looking straight down. */
    CHECK(rig.player->interaction.hand_count >= 1u);
    CHECK(pi_frames(&rig, &target, false, true, true, 30.0f64, 1u, 1u));
    /* Let go, and it lands on what it placed. */
    const int64_t landed_again_by = time_now_ns() + 1500000000;
    while (time_now_ns() < landed_again_by) {
        game_walk_send(game, &none, false, false, time_now_ns());
        game_hands_send(game, false, false);
        CHECK(game_loop_frame(game, &none, 0.0f32, &target));
        time_sleep_ns(4000000);
    }
    CHECK(game_loop_stop(game));
    CHECK_EQ_U64(rig.player->interaction.placed_count, 1u);
    CHECK_NEAR(rig.player->movement.position[2], feet, 1.0e-9f64);
    /* The threads have stopped: all is whole to read. */
    CHECK(rig.player->interaction.mined_count >= 1u);
    CHECK_EQ_U64(rig.player->interaction.no_memory_count, 0u);
    game_close(game);
    pi_game_end(t, &rig);
}

/* ---- The built program ------------------------------------------------------------------------------------------------ */

typedef struct pi_temp_dir_t {
    char path[64]; /* 0 */
} pi_temp_dir_t;
static_assert(sizeof(pi_temp_dir_t) == 64);

static bool pi_temp_dir_make(pi_temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_player_interaction_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void pi_temp_dir_remove(const pi_temp_dir_t *dir) {
    DIR *const handle = opendir(dir->path);
    if (handle != nullptr) {
        char path[512];
        for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
            if (entry->d_name[0] != '.') {
                snprintf(path, sizeof(path), "%s/%s", dir->path, entry->d_name);
                unlink(path);
            }
        }
        closedir(handle);
    }
    rmdir(dir->path);
}

/* Walks down the hill the default seed starts on, looks straight down, and mines through the grass under its feet and the dirt under
 * that, falling as each goes. Then it turns uphill, looks level at the wall of its hole, mines that, and puts one back. */
static const char pi_mine_script[] = "1 key w down\n"
                                     "21 key w up\n"
                                     "22 move 0 500\n"
                                     "30 screenshot before.png\n"
                                     "31 button left down\n"
                                     "52 screenshot grass_gone.png\n"
                                     "75 button left up\n"
                                     "85 screenshot dirt_gone.png\n"
                                     "90 move 1256 -620\n"
                                     "92 button left down\n"
                                     "115 button left up\n"
                                     "116 screenshot notch.png\n"
                                     "118 button right down\n"
                                     "119 button right up\n"
                                     "121 screenshot placed.png\n"
                                     "125 end\n";

static void pi_program_run(test_context_t *t, const pi_temp_dir_t *dir, const char *script, test_process_t *out) {
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--test-world",
                          (char *)"--walk",
                          (char *)"--render-distance",
                          (char *)"4",
                          (char *)"--script",
                          (char *)script,
                          (char *)"--screenshot-dir",
                          (char *)dir->path,
                          (char *)"--width",
                          (char *)"160",
                          (char *)"--height",
                          (char *)"90",
                          nullptr};
    test_process_run(t, argv, PI_PROGRAM_TIMEOUT_MS, out);
}

static uint8_t *pi_png_read(test_context_t *t, const pi_temp_dir_t *dir, const char *name, uint64_t *out_length) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir->path, name);
    const uint64_t capacity = 16ull << 20;
    uint8_t *const bytes = test_alloc(t, capacity);
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, capacity, out_length), FILE_OK)) {
        *out_length = 0u;
    }
    return bytes;
}

TEST(the_program_walks_mines_to_dirt_and_places_it_back_the_same_each_time) {
    pi_temp_dir_t first, second;
    REQUIRE(pi_temp_dir_make(&first));
    REQUIRE(pi_temp_dir_make(&second));
    char script[128];
    snprintf(script, sizeof(script), "%s/mine.script", first.path);
    REQUIRE(file_write_atomic(script, pi_mine_script, sizeof(pi_mine_script) - 1u) == FILE_OK);
    test_process_t run;
    pi_program_run(t, &first, script, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    /* Grass and two of dirt, each giving dirt, and one of them placed back. */
    CHECK_CONTAINS(run.output,
                   "the player mined 3 blocks and placed 1; the last mined was dirt, and the hand holds 2 of dirt");
    CHECK_CONTAINS(run.output, "0 bytes live");
    pi_program_run(t, &second, script, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    static const char *const names[] = {"before.png", "grass_gone.png", "dirt_gone.png", "notch.png", "placed.png"};
    const uint8_t *first_bytes[5];
    uint64_t first_length[5];
    for (uint32_t i = 0u; i < 5u; i++) {
        uint64_t second_length = 0u;
        first_bytes[i] = pi_png_read(t, &first, names[i], &first_length[i]);
        const uint8_t *const second_bytes = pi_png_read(t, &second, names[i], &second_length);
        REQUIRE(first_length[i] != 0u && first_length[i] == second_length);
        CHECK_MEM_EQ(first_bytes[i], second_bytes, first_length[i]);
    }
    /* Each step changed the picture. */
    for (uint32_t i = 0u; i + 1u < 5u; i++) {
        CHECK(first_length[i] != first_length[i + 1u] ||
              memcmp(first_bytes[i], first_bytes[i + 1u], first_length[i]) != 0);
    }
    pi_temp_dir_remove(&first);
    pi_temp_dir_remove(&second);
}

TEST_MAIN()
