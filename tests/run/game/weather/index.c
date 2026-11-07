/* ctest run_game_weather. See README.md. */
#include "harness/index.h"

#include "climate/systems/field/index.h"
#include "game/index.h"
#include "game/weather/index.h"
#include "gpu_bridge/index.h"
#include "job/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- A rig: everything a real run has, on memory of its own ---------------------------------------------------------------- */

typedef struct gw_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
} gw_rig_t;
static_assert(sizeof(gw_rig_t) == 64);

static void *gw_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool gw_rig_begin(test_context_t *t, gw_rig_t *rig) {
    rig->bridge = gw_aligned(t, sizeof(gpu_bridge_t));
    rig->job = gw_aligned(t, sizeof(job_system_t));
    rig->world = gw_aligned(t, sizeof(world_t));
    rig->terrain = gw_aligned(t, sizeof(terrain_t));
    rig->render = gw_aligned(t, sizeof(render_t));
    rig->texture = gw_aligned(t, sizeof(texture_t));
    rig->game = gw_aligned(t, sizeof(game_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    return true;
}

static void gw_rig_end(test_context_t *t, gw_rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

enum : uint32_t {
    GW_CELLS = 8u,
    GW_SLOTS = 400u, /* game seconds from the first cell's birth */
};

/* ---- Lightning, pure ------------------------------------------------------------------------------------------------------ */

/* Thunderstorm cells as phase 06 makes them: 300 blocks across, living four clock hours, drifting east. */
static void gw_cells(climate_thunder_t out[GW_CELLS]) {
    for (uint32_t c = 0u; c < GW_CELLS; c++) {
        memset(&out[c], 0, sizeof(out[c]));
        out[c].spawn_day = 10.0f64 + 0.01f64 * (float64_t)c;
        out[c].start_x = 500.0f64 * (float64_t)c;
        out[c].start_y = -300.0f64;
        out[c].id = 0x9E3779B97F4A7C15ull * (uint64_t)(c + 1u);
        out[c].velocity[0] = 3000.0f32;
        out[c].radius = 150.0f32 + 30.0f32 * (float32_t)c;
        out[c].peak_mm_hour = 40.0f32;
        out[c].peak_hpa = -2.0f32;
        out[c].life_days = 4.0f32 / 24.0f32;
    }
}

typedef struct gw_strike_t {
    float64_t place[2]; /* 0 */
    uint64_t slot;      /* 16 */
    uint32_t cell;      /* 24 */
    uint8_t _pad0[4];   /* 28 */
} gw_strike_t;
static_assert(sizeof(gw_strike_t) == 32);

/* Every strike of the cells over the slots, and what the chances add up to. */
static uint32_t gw_strikes(test_context_t *t, uint64_t seed, const climate_thunder_t cells[GW_CELLS], gw_strike_t *out,
                           uint32_t capacity, float64_t *out_expected) {
    uint32_t count = 0u;
    float64_t expected = 0.0f64;
    const uint64_t first = (uint64_t)(10.0f64 * GAME_DAY_SECONDS);
    for (uint64_t slot = first; slot < first + GW_SLOTS; slot++) {
        const float64_t when = (float64_t)slot / GAME_DAY_SECONDS;
        for (uint32_t c = 0u; c < GW_CELLS; c++) {
            float64_t centre[2], share = 0.0f64;
            if (climate_systems_thunder_at(&cells[c], when, centre, &share)) {
                expected += GAME_LIGHTNING_RATE * share;
            }
            float64_t place[2];
            if (game_weather_cell_strikes(seed, &cells[c], slot, when, place) && count < capacity) {
                out[count] = (gw_strike_t){.place = {place[0], place[1]}, .slot = slot, .cell = c, ._pad0 = {0}};
                const float64_t dx = place[0] - centre[0], dy = place[1] - centre[1];
                CHECK(sqrt(dx * dx + dy * dy) <= (float64_t)cells[c].radius + 1e-6f64); /* within the cell */
                count++;
            }
        }
    }
    *out_expected = expected;
    return count;
}

TEST(lightning_is_the_same_on_replay) {
    climate_thunder_t cells[GW_CELLS];
    gw_cells(cells);
    gw_strike_t *const first = test_alloc(t, 4096u * sizeof(gw_strike_t));
    gw_strike_t *const again = test_alloc(t, 4096u * sizeof(gw_strike_t));
    float64_t expected = 0.0f64, expected_again = 0.0f64;
    const uint32_t count = gw_strikes(t, 42u, cells, first, 4096u, &expected);
    const uint32_t count_again = gw_strikes(t, 42u, cells, again, 4096u, &expected_again);
    /* The same seed, cells and seconds: the same strikes at the same seconds and places. */
    CHECK_EQ_U64(count_again, count);
    CHECK_MEM_EQ(again, first, (uint64_t)count * sizeof(gw_strike_t));
    /* As often as 0.05 a second times the strength says: within four standard deviations of a count of that mean. */
    printf("  %" PRIu32 " strikes over %u seconds of %u cells, %.1f expected\n", count, GW_SLOTS, GW_CELLS,
           (libc_double_t)expected);
    CHECK(expected > 10.0f64);
    CHECK(fabs((float64_t)count - expected) < 4.0f64 * sqrt(expected));
    /* Another seed, other strikes. */
    float64_t other_expected = 0.0f64;
    gw_strike_t *const other = test_alloc(t, 4096u * sizeof(gw_strike_t));
    const uint32_t other_count = gw_strikes(t, 43u, cells, other, 4096u, &other_expected);
    CHECK(other_count != count || memcmp(other, first, (uint64_t)count * sizeof(gw_strike_t)) != 0);
    /* A strike's bolt: the same joints twice; from 180 above the ground down to the ground exactly; no joint more than 8 out. */
    REQUIRE(count > 0u);
    const float64_t eye[3] = {first[0].place[0] + 300.0f64, first[0].place[1], 64.0f64};
    game_weather_strike_t bolt, bolt_again;
    game_weather_bolt_make(42u, first[0].slot, first[0].place, 62.0f64, 1250.0f64, eye, &bolt);
    game_weather_bolt_make(42u, first[0].slot, first[0].place, 62.0f64, 1250.0f64, eye, &bolt_again);
    CHECK_MEM_EQ(&bolt_again, &bolt, sizeof(bolt));
    CHECK_EQ_I64(bolt.origin[2], 62);
    CHECK_NEAR(bolt.bolt[0][2], 180.0f32, 1e-4f32);
    const float32_t ground[3] = {(float32_t)(first[0].place[0] - floor(first[0].place[0])),
                                 (float32_t)(first[0].place[1] - floor(first[0].place[1])), 0.0f32};
    CHECK_NEAR(bolt.bolt[POST_BOLT_SEGMENTS][0], ground[0], 1e-5f32);
    CHECK_NEAR(bolt.bolt[POST_BOLT_SEGMENTS][1], ground[1], 1e-5f32);
    CHECK_NEAR(bolt.bolt[POST_BOLT_SEGMENTS][2], 0.0f32, 1e-5f32);
    for (uint32_t j = 0u; j <= POST_BOLT_SEGMENTS; j++) {
        CHECK(fabsf(bolt.bolt[j][0] - ground[0]) <= (float32_t)GAME_LIGHTNING_JITTER);
        CHECK(fabsf(bolt.bolt[j][1] - ground[1]) <= (float32_t)GAME_LIGHTNING_JITTER);
    }
    CHECK_NEAR(bolt.distance, sqrtf(300.0f32 * 300.0f32 + 4.0f32), 1e-2f32);
    /* Near the world's top the cloud base is the top. */
    game_weather_bolt_make(42u, first[0].slot, first[0].place, 1200.0f64, 1250.0f64, eye, &bolt);
    CHECK_NEAR(bolt.bolt[0][2], 50.0f32, 1e-4f32);
}

TEST(the_forced_kinds_are_what_they_say) {
    game_weather_kind_t kind = GAME_WEATHER_SAMPLED;
    CHECK(game_weather_kind_parse("hurricane", &kind));
    CHECK_EQ_U64(kind, GAME_WEATHER_HURRICANE);
    CHECK(!game_weather_kind_parse("sampled", &kind));
    CHECK(!game_weather_kind_parse("drizzle", &kind));
    for (uint32_t k = GAME_WEATHER_CLEAR; k < GAME_WEATHER_KIND_COUNT; k++) {
        game_weather_kind_t back = GAME_WEATHER_SAMPLED;
        CHECK(game_weather_kind_parse(game_weather_kind_name((game_weather_kind_t)k), &back));
        CHECK_EQ_U64(back, k);
    }
    climate_now_t now;
    game_weather_forced_now(GAME_WEATHER_HURRICANE, &now);
    CHECK(sqrtf(now.wind[0] * now.wind[0] + now.wind[1] * now.wind[1]) > 50.0f32);
    CHECK(now.rain_mm_h >= 30.0f32 && now.cloud == 1.0f32);
    game_weather_forced_now(GAME_WEATHER_SNOW, &now);
    CHECK(now.snow_share == 1.0f32 && now.rain_mm_h > 0.0f32);
    game_weather_forced_now(GAME_WEATHER_FOG, &now);
    CHECK(now.fog > 0.5f32 && now.rain_mm_h == 0.0f32);
    game_weather_forced_now(GAME_WEATHER_CLEAR, &now);
    CHECK(now.rain_mm_h == 0.0f32 && now.fog == 0.0f32 && now.dust == 0.0f32);
}

/* ---- In a game, on the card --------------------------------------------------------------------------------------------- */

static game_config_t gw_config(void) {
    return (game_config_t){.render_distance = 3u,
                           .width = 160u,
                           .height = 90u,
                           .features_off = SHADE_FEATURE_TAA | SHADE_FEATURE_AO | SHADE_FEATURE_DAY_CYCLE,
                           .show_date = 0u};
}

/* One tick of the sim, and the slot it sent, taken as the thread that draws takes it. */
static const render_weather_slot_t *gw_tick(test_context_t *t, game_t *game) {
    game_sim_tick(game, (int64_t)(game->tick_count + 1u) * 50000000);
    game_weather_t *const weather = game_weather_of(game);
    uint32_t index = 0u;
    if (!CHECK(weather != nullptr) || !CHECK(render_exchange_take(&weather->exchange, &index))) {
        return nullptr;
    }
    return &weather->slots[index];
}

/* The map entry of a block column in a slot, or POST_WEATHER_MAP_COVERED off it. */
static int16_t gw_entry(const render_weather_slot_t *slot, int32_t x, int32_t y) {
    const int32_t across = x - slot->map_origin[0], row = y - slot->map_origin[1];
    if (across < 0 || row < 0 || across >= (int32_t)POST_WEATHER_MAP_SIDE || row >= (int32_t)POST_WEATHER_MAP_SIDE) {
        return POST_WEATHER_MAP_COVERED;
    }
    return slot->map[(uint32_t)row * POST_WEATHER_MAP_SIDE + (uint32_t)across];
}

TEST(a_world_with_no_climate_sends_zeros) {
    gw_rig_t rig;
    REQUIRE(gw_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = gw_config();
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    const render_weather_slot_t *const slot = gw_tick(t, game);
    REQUIRE(slot != nullptr);
    CHECK_EQ_U64(slot->sent, 0u);
    CHECK(slot->rain_mm_h == 0.0f32 && slot->cloud == 0.0f32 && slot->wind[0] == 0.0f32 && slot->flash == 0.0f32);
    CHECK(slot->bolt_age < 0.0f32);
    float32_t wind[2] = {1.0f32, 1.0f32};
    game_weather_wind(game, wind);
    CHECK(wind[0] == 0.0f32 && wind[1] == 0.0f32);
    /* And the picture is the clear day's: what render_frame_draw makes with no weather at all. */
    REQUIRE(CHECK_EQ_U64(render_frame_draw(&rig.render->frame, &rig.render->buffers, &game->camera), GPU_STATUS_OK));
    const uint8_t *picture = render_frame_take(&rig.render->frame);
    REQUIRE(picture != nullptr);
    uint8_t *const clear = test_alloc(t, 160u * 90u * 4u);
    memcpy(clear, picture, 160u * 90u * 4u);
    REQUIRE(CHECK_EQ_U64(game_weather_draw(game, &game->camera, 0), GPU_STATUS_OK));
    picture = render_frame_take(&rig.render->frame);
    REQUIRE(picture != nullptr);
    CHECK_MEM_EQ(picture, clear, 160u * 90u * 4u);
    game_close(game);
    gw_rig_end(t, &rig);
}

TEST(the_rain_top_follows_edits) {
    gw_rig_t rig;
    REQUIRE(gw_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = gw_config();
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    game_weather_force(game, GAME_WEATHER_RAIN);
    const render_weather_slot_t *slot = gw_tick(t, game);
    REQUIRE(slot != nullptr);
    CHECK_EQ_U64(slot->sent, 1u);
    CHECK_EQ_U64(slot->forced, 1u);
    CHECK(slot->rain_mm_h > 0.0f32);
    /* The map is round the eye's column: the middle of the chunk the map is centred on, with no body. */
    const world_map_t *const map = &game->borrowed_world->map;
    const int32_t x = map->center_x * (int32_t)COLUMN_SIZE + 8, y = map->center_y * (int32_t)COLUMN_SIZE + 8;
    CHECK_EQ_I64(slot->map_origin[0], x - 32);
    CHECK_EQ_I64(slot->map_origin[1], y - 32);
    const int16_t top = gw_entry(slot, x, y);
    REQUIRE(top != POST_WEATHER_MAP_COVERED);
    CHECK_EQ_I64(top, game_weather_column_top(game->borrowed_world, x, y));
    const uint32_t revision = slot->map_revision;
    /* A block put over a column beside the eye: the next slot has it as the column's rain top. */
    const int32_t bx = x + 3, by = y - 2;
    const int16_t beside = game_weather_column_top(game->borrowed_world, bx, by);
    REQUIRE(beside != POST_WEATHER_MAP_COVERED);
    CHECK_EQ_I64(gw_entry(slot, bx, by), beside);
    REQUIRE(world_edit_block_set(&game->borrowed_world->edit, bx, by, beside + 3, BLOCK_ID_OAK_PLANKS) ==
            WORLD_EDIT_STATUS_CHANGED);
    slot = gw_tick(t, game);
    REQUIRE(slot != nullptr);
    CHECK_EQ_I64(gw_entry(slot, bx, by), beside + 3);
    CHECK(slot->map_revision != revision);
    /* Taken away again: back to what it was, and the revision moves on again. */
    const uint32_t placed = slot->map_revision;
    REQUIRE(world_edit_block_set(&game->borrowed_world->edit, bx, by, beside + 3, BLOCK_ID_AIR) ==
            WORLD_EDIT_STATUS_CHANGED);
    slot = gw_tick(t, game);
    REQUIRE(slot != nullptr);
    CHECK_EQ_I64(gw_entry(slot, bx, by), beside);
    CHECK(slot->map_revision != placed);
    /* A tick with no change sends the same revision. */
    const uint32_t still = slot->map_revision;
    slot = gw_tick(t, game);
    REQUIRE(slot != nullptr);
    CHECK_EQ_U64(slot->map_revision, still);
    game_close(game);
    gw_rig_end(t, &rig);
}

TEST(forced_thunder_strikes_every_two_seconds_and_a_hurricane_blows) {
    gw_rig_t rig;
    REQUIRE(gw_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = gw_config();
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    game_weather_force(game, GAME_WEATHER_THUNDER);
    uint32_t seen = 0u, struck_ticks = 0u;
    for (uint32_t i = 0u; i < 80u; i++) {
        const uint64_t tick = game->tick_count;
        const render_weather_slot_t *const slot = gw_tick(t, game);
        REQUIRE(slot != nullptr);
        const bool bolt = slot->bolt_age >= 0.0f32;
        struck_ticks += bolt ? 1u : 0u;
        CHECK_EQ_U64(bolt ? 1u : 0u, tick % GAME_LIGHTNING_FORCED_TICKS < GAME_LIGHTNING_BOLT_TICKS ? 1u : 0u);
        if (bolt) {
            CHECK_NEAR(slot->bolt_age, (float32_t)(tick % GAME_LIGHTNING_FORCED_TICKS) / 20.0f32, 1e-6f32);
            CHECK_NEAR(slot->bolt_distance, 300.0f32, 30.0f32);
            CHECK_NEAR(slot->flash, game_weather_flash(slot->bolt_distance), 1e-6f32);
            seen += tick % GAME_LIGHTNING_FORCED_TICKS == 0u ? 1u : 0u;
        }
    }
    CHECK_EQ_U64(seen, 2u);
    CHECK_EQ_U64(struck_ticks, 8u);
    /* A hurricane's wind on the body, at once when forced. */
    game_weather_force(game, GAME_WEATHER_HURRICANE);
    (void)gw_tick(t, game);
    float32_t wind[2];
    game_weather_wind(game, wind);
    climate_now_t now;
    game_weather_forced_now(GAME_WEATHER_HURRICANE, &now);
    CHECK(wind[0] == now.wind[0] && wind[1] == now.wind[1]);
    game_close(game);
    gw_rig_end(t, &rig);
}

TEST_MAIN()
