/* See README.md. */
#include "harness/index.h"

#include "png/index.h"

#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "job/index.h"
#include "log/index.h"
#include "main/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    UW_WIDTH = 320u,
    UW_HEIGHT = 180u,
    UW_GOLDEN_TOLERANCE = 2u,
    UW_PROGRAM_TIMEOUT_MS = 200000u,
    UW_COST_FRAMES = 30u,
    UW_COST_WIDTH = 1920u,
    UW_COST_HEIGHT = 1080u,
    /* Under this on all three channels, a pixel reads as black. */
    UW_BLACK = 12u,
    /* The hotbar and the rows over it, which nothing here is about. */
    UW_HUD_FROM_Y = 140u,
};

#define UW_NOON 0.5f32
#define UW_MIDNIGHT 0.0f32
#define UW_LOOK_DOWN (-1.55f32)
#define UW_NEAR 1e-5f32

#include "cuda/sky/fixtures/rig.h"

static bool uw_near(float32_t a, float32_t b) {
    return fabsf(a - b) <= UW_NEAR;
}

/* ---- The arithmetic, on numbers worked out by hand ------------------------------------------------------------------------- */

TEST(how_much_is_water_goes_by_distance_as_on_paper) {
    const post_settings_t settings = {.water_tint = {0.5f32, 0.75f32, 1.0f32},
                                      .water_colour = {0.1f32, 0.2f32, 0.4f32},
                                      .water_fog_end = 28.0f32,
                                      .under_water = 1u};
    CHECK(uw_near(post_water_fog(&settings, 0.0f32), 0.12f32));
    CHECK(uw_near(post_water_fog(&settings, 14.0f32), 0.12f32 + 0.88f32 * 0.75f32));
    CHECK(uw_near(post_water_fog(&settings, 28.0f32), 1.0f32));
    CHECK(uw_near(post_water_fog(&settings, 500.0f32), 1.0f32));
    CHECK(uw_near(post_water_fog(&settings, TRAVERSE_COARSE_SKY), 1.0f32));
    float32_t last = 0.0f32;
    for (uint32_t i = 0u; i < 300u; i++) {
        const float32_t fog = post_water_fog(&settings, (float32_t)i * 0.1f32);
        CHECK(fog >= last);
        last = fog;
    }
    float32_t seen[3] = {1.0f32, 1.0f32, 1.0f32};
    post_under_water(&settings, 0.0f32, seen);
    CHECK(uw_near(seen[0], 0.5f32 * 0.88f32 + 0.1f32 * 0.12f32));
    CHECK(uw_near(seen[1], 0.75f32 * 0.88f32 + 0.2f32 * 0.12f32));
    CHECK(uw_near(seen[2], 0.88f32 + 0.4f32 * 0.12f32));
    /* All black at the eye is still not nothing. That is the void this is for. */
    float32_t black[3] = {0.0f32, 0.0f32, 0.0f32};
    post_under_water(&settings, 0.0f32, black);
    CHECK(uw_near(black[2], 0.4f32 * 0.12f32));
    float32_t far[3] = {3.0f32, 3.0f32, 3.0f32};
    post_under_water(&settings, 40.0f32, far);
    CHECK(uw_near(far[0], 0.1f32));
    CHECK(uw_near(far[1], 0.2f32));
    CHECK(uw_near(far[2], 0.4f32));
    /* The picture comes out blue, whatever it was. */
    CHECK(POST_WATER_TINT_R < POST_WATER_TINT_G && POST_WATER_TINT_G < POST_WATER_TINT_B);
    CHECK(POST_WATER_COLOUR_R < POST_WATER_COLOUR_G && POST_WATER_COLOUR_G < POST_WATER_COLOUR_B);
}

TEST(the_colour_follows_the_day_and_the_flag_lasts) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 1u, UW_WIDTH, UW_HEIGHT, 1000.0f32));
    render_frame_t *const frame = &rig.render->frame;
    CHECK_EQ_U64(frame->scene.features & SHADE_FEATURE_UNDER_WATER, SHADE_FEATURE_UNDER_WATER);
    CHECK_EQ_U64(SHADE_FEATURES_PLAY & SHADE_FEATURE_UNDER_WATER, SHADE_FEATURE_UNDER_WATER);
    CHECK_EQ_U64(frame->post.under_water, 0u);
    render_frame_time_set(frame, UW_NOON);
    CHECK(uw_near(frame->post.water_colour[2], POST_WATER_COLOUR_B));
    CHECK(uw_near(frame->post.water_tint[0], POST_WATER_TINT_R));
    CHECK(uw_near(frame->post.water_fog_end, POST_WATER_FOG_END));
    render_frame_time_set(frame, UW_MIDNIGHT);
    const float32_t night = frame->post.water_colour[2];
    CHECK(night < POST_WATER_COLOUR_B * 0.5f32);
    CHECK(night >= POST_WATER_COLOUR_B * POST_WATER_COLOUR_NIGHT);

    render_frame_under_water_set(frame, true);
    CHECK_EQ_U64(frame->post.under_water, 1u);
    render_frame_time_set(frame, UW_NOON);
    CHECK_EQ_U64(frame->post.under_water, 1u);
    render_frame_fog_set(frame, 500.0f32);
    CHECK_EQ_U64(frame->post.under_water, 1u);
    render_frame_features_set(frame, SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_UNDER_WATER);
    CHECK_EQ_U64(frame->post.under_water, 0u);
    render_frame_features_set(frame, SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_SKY);
    CHECK_EQ_U64(frame->post.under_water, 1u);
    CHECK(uw_near(frame->post.water_fog_end, POST_WATER_FOG_END)); /* a plain sky wipes all of post */
    CHECK(uw_near(frame->post.water_colour[2], POST_WATER_COLOUR_B));
    render_frame_under_water_set(frame, false);
    CHECK_EQ_U64(frame->post.under_water, 0u);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* ---- A sea of our own -------------------------------------------------------------------------------------------------------- */

/* Water from z = -30 to the surface at 0, over stone at -31, all across what a radius of 3 holds around the middle of the map. */
static bool uw_sea_put(test_context_t *t, sk_rig_t *rig) {
    for (int32_t z = -31; z < 0; z++) {
        for (int32_t y = -32; y < 48; y++) {
            for (int32_t x = -32; x < 48; x++) {
                const world_edit_status_t edited =
                    world_edit_block_set(&rig->world->edit, x, y, z, z == -31 ? BLOCK_ID_STONE : BLOCK_ID_WATER);
                if (!CHECK_EQ_U64(edited, WORLD_EDIT_STATUS_CHANGED)) {
                    return false;
                }
            }
        }
    }
    return sk_hand_world_sync(t, rig);
}

static uint32_t uw_gap(uint32_t a, uint32_t b) {
    uint32_t most = 0u;
    for (uint32_t shift = 0u; shift < 24u; shift += 8u) {
        const int32_t gap = (int32_t)((a >> shift) & 255u) - (int32_t)((b >> shift) & 255u);
        const uint32_t size = (uint32_t)abs(gap);
        most = size > most ? size : most;
    }
    return most;
}

static uint32_t uw_middle(const uint8_t *picture) {
    return sk_pixel(picture, UW_WIDTH, UW_WIDTH / 2u, UW_HEIGHT / 2u);
}

TEST(flag_off_or_setting_off_is_the_same_picture_and_on_is_water) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 3u, UW_WIDTH, UW_HEIGHT, 100000.0f32));
    REQUIRE(uw_sea_put(t, &rig));
    render_frame_t *const frame = &rig.render->frame;
    render_frame_time_set(frame, UW_NOON);
    const uint64_t bytes = (uint64_t)UW_WIDTH * UW_HEIGHT * 4u;
    uint8_t *const plain = test_alloc(t, bytes);
    uint8_t *const under = test_alloc(t, bytes);

    /* Level, along the sea: nothing but water for longer than the fog is. */
    render_camera_t camera;
    render_camera_start(&camera, 8.5f64, 8.5f64, -15.5f64, 0.0f32, 0.0f32);
    const uint8_t *picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    memcpy(plain, picture, bytes);

    render_frame_features_set(frame, SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_UNDER_WATER);
    render_frame_under_water_set(frame, true);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    CHECK_MEM_EQ(picture, plain, bytes);

    render_frame_features_set(frame, SHADE_FEATURES_ALL);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    memcpy(under, picture, bytes);
    uint64_t changed = 0u;
    for (uint64_t i = 0u; i < (uint64_t)UW_WIDTH * UW_HEIGHT; i++) {
        changed += memcmp(plain + i * 4u, under + i * 4u, 3u) != 0 ? 1u : 0u;
    }
    printf("  under water, on against off: %" PRIu64 " pixels of %" PRIu64 " change\n", changed,
           (uint64_t)UW_WIDTH * UW_HEIGHT);
    CHECK(changed > (uint64_t)UW_WIDTH * UW_HEIGHT * 9u / 10u);

    /* That far off it is the water's colour, exactly: which holds only if the ray counts by where the water stops, and not by the surface it started in. */
    const post_settings_t *const post = &frame->post;
    const uint32_t water = post_xrgb(post_tone_map(post->water_colour[0]), post_tone_map(post->water_colour[1]),
                                     post_tone_map(post->water_colour[2]));
    printf("  far water: %06x against %06x\n", uw_middle(under), water);
    CHECK(sk_bytes_near(uw_middle(under), water, 1u));

    /* Straight down from further and further up: the bed goes to the water's colour by how far off it is. */
    const float64_t heights[] = {-29.0f64, -24.0f64, -16.0f64, -8.0f64};
    uint32_t last_gap = 256u;
    for (uint32_t i = 0u; i < 4u; i++) {
        render_camera_start(&camera, 8.5f64, 8.5f64, heights[i], 0.0f32, UW_LOOK_DOWN);
        picture = sk_draw(t, &rig, &camera);
        REQUIRE(picture != nullptr);
        const uint32_t gap = uw_gap(uw_middle(picture), water);
        printf("  bed from %.0f: %06x, %" PRIu32 " off the water's colour\n", (libc_double_t)heights[i],
               uw_middle(picture), gap);
        CHECK(gap < last_gap);
        last_gap = gap;
    }

    /* Up from just under the surface: not all water, as it is near, and what is through it is the sky. */
    render_camera_start(&camera, 8.5f64, 8.5f64, -2.5f64, 0.0f32, 1.55f32);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    const uint32_t up = uw_middle(picture);
    printf("  up at the surface: %06x\n", up);
    CHECK(uw_gap(up, water) > 8u);
    CHECK((up & 255u) > (water & 255u));

    /* Kelp at the eye, which takes that cell's water away: nothing there is at no distance, so what is clear of the plant is still far water, and not black. */
    const uint32_t far_water = uw_middle(under);
    for (int32_t z = -31 + 1; z < -2; z++) {
        REQUIRE(
            CHECK_EQ_U64(world_edit_block_set(&rig.world->edit, 8, 8, z, BLOCK_ID_KELP), WORLD_EDIT_STATUS_CHANGED));
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_camera_start(&camera, 8.5f64, 8.5f64, -15.5f64, 0.0f32, 0.0f32);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    uint64_t still_water = 0u;
    for (uint32_t y = 0u; y < UW_HEIGHT; y++) {
        for (uint32_t x = 0u; x < UW_WIDTH; x++) {
            still_water += uw_gap(sk_pixel(picture, UW_WIDTH, x, y), far_water) <= 2u ? 1u : 0u;
        }
    }
    printf("  an eye in kelp: %" PRIu64 " pixels of %" PRIu64 " are far water\n", still_water,
           (uint64_t)UW_WIDTH * UW_HEIGHT);
    CHECK(still_water > (uint64_t)UW_WIDTH * UW_HEIGHT / 2u);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* ---- What it costs, on the card's own clock -------------------------------------------------------------------------------- */

TEST(the_cost_is_measured) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 3u, UW_COST_WIDTH, UW_COST_HEIGHT, 100000.0f32));
    REQUIRE(uw_sea_put(t, &rig));
    render_frame_t *const frame = &rig.render->frame;
    render_frame_time_set(frame, UW_NOON);
    render_camera_t camera;
    render_camera_start(&camera, 8.5f64, 8.5f64, -15.5f64, 0.0f32, -0.2f32);
    render_frame_under_water_set(frame, true);
    const uint32_t settings[] = {SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_UNDER_WATER, SHADE_FEATURES_ALL};
    const char *const names[] = {"off", "on"};
    for (uint32_t s = 0u; s < 2u; s++) {
        render_frame_features_set(frame, settings[s]);
        REQUIRE(sk_draw(t, &rig, &camera) != nullptr); /* a first frame pays for what the others find ready */
        const render_frame_stats_t before = frame->stats;
        for (uint32_t i = 0u; i < UW_COST_FRAMES; i++) {
            REQUIRE(sk_draw(t, &rig, &camera) != nullptr);
        }
        const float64_t frames = (float64_t)UW_COST_FRAMES;
        const float64_t post = (frame->stats.sum_ms[FRAME_PASS_POST] - before.sum_ms[FRAME_PASS_POST]) / frames;
        const float64_t shade = (frame->stats.sum_ms[FRAME_PASS_SHADE] - before.sum_ms[FRAME_PASS_SHADE]) / frames;
        const float64_t total = (frame->stats.sum_total_ms - before.sum_total_ms) / frames;
        printf("  cost, under the sea, %" PRIu32 " x %" PRIu32
               ", %-3s: post %7.3f ms, shade %7.3f ms, frame %7.3f ms\n",
               frame->width, frame->height, names[s], (libc_double_t)post, (libc_double_t)shade, (libc_double_t)total);
        CHECK(total > 0.0f64);
    }
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* ---- The built program, with a real body in a real sea ---------------------------------------------------------------------- */

typedef struct uw_temp_dir_t {
    char path[64]; /* 0 */
} uw_temp_dir_t;
static_assert(sizeof(uw_temp_dir_t) == 64);

static bool uw_temp_dir_make(uw_temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_render_under_water_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void uw_temp_dir_remove(const uw_temp_dir_t *dir) {
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

/* A PNG, as sk_pixel reads a picture. */
static uint8_t *uw_png_read(test_context_t *t, const char *path) {
    const uint64_t capacity = display_png_bytes_needed(UW_WIDTH, UW_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    uint64_t length = 0u;
    if (!CHECK_EQ_U64(file_read_whole(path, file, capacity, &length), FILE_OK)) {
        printf("  cannot read %s\n", path);
        return nullptr;
    }
    png_info_t info;
    png_error_t error;
    if (!CHECK(png_probe(file, length, &info, &error) == PNG_OK) ||
        !CHECK(info.width == UW_WIDTH && info.height == UW_HEIGHT)) {
        return nullptr;
    }
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    if (!CHECK(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK)) {
        return nullptr;
    }
    for (uint64_t i = 0u; i < (uint64_t)UW_WIDTH * UW_HEIGHT; i++) {
        const uint32_t xrgb = post_xrgb(rgba[i * 4u], rgba[i * 4u + 1u], rgba[i * 4u + 2u]);
        memcpy(rgba + i * 4u, &xrgb, 4u);
    }
    return rgba;
}

static void uw_golden_compare(test_context_t *t, const char *dir, const char *name) {
    char written[256], golden[256];
    snprintf(written, sizeof(written), "%s/%s", dir, name);
    snprintf(golden, sizeof(golden), "tests/cuda/under_water/fixtures/%s", name);
    if (getenv("MC_GOLDEN_WRITE") != nullptr) {
        const uint64_t capacity = display_png_bytes_needed(UW_WIDTH, UW_HEIGHT);
        uint8_t *const file = test_alloc(t, capacity);
        uint64_t length = 0u;
        REQUIRE(file_read_whole(written, file, capacity, &length) == FILE_OK);
        REQUIRE(file_write_atomic(golden, file, length) == FILE_OK);
        printf("  wrote %s\n", golden);
        return;
    }
    const uint8_t *const ours = uw_png_read(t, written);
    const uint8_t *const theirs = uw_png_read(t, golden);
    REQUIRE(ours != nullptr && theirs != nullptr);
    uint64_t wrong = 0u, exact = 0u;
    for (uint32_t y = 0u; y < UW_HEIGHT; y++) {
        for (uint32_t x = 0u; x < UW_WIDTH; x++) {
            const uint32_t a = sk_pixel(ours, UW_WIDTH, x, y), b = sk_pixel(theirs, UW_WIDTH, x, y);
            wrong += sk_bytes_near(a, b, UW_GOLDEN_TOLERANCE) ? 0u : 1u;
            exact += a == b ? 1u : 0u;
        }
    }
    printf("  %s: %" PRIu64 " of %" PRIu64 " pixels exact, %" PRIu64 " past the tolerance\n", golden, exact,
           (uint64_t)UW_WIDTH * UW_HEIGHT, wrong);
    CHECK_EQ_U64(wrong, 0u);
}

/* How many pixels over the HUD read as black. */
static uint64_t uw_black_pixels(const uint8_t *picture) {
    uint64_t black = 0u;
    for (uint32_t y = 0u; y < UW_HUD_FROM_Y; y++) {
        for (uint32_t x = 0u; x < UW_WIDTH; x++) {
            black += sk_bytes_near(sk_pixel(picture, UW_WIDTH, x, y), 0u, UW_BLACK) ? 1u : 0u;
        }
    }
    return black;
}

/* Where the dive's script is written, for uw_dive. */
static char uw_dive_script[256];

/* ---- The dive's script, made for the land as it is ------------------------------------------------------------------ */

enum : uint32_t {
    UW_SCRIPT_BYTES = 8192u,
    UW_SEA_SEARCH_STEP = 32u,   /* blocks between the columns looked at, ring by ring round the world's middle */
    UW_SEA_SEARCH_RINGS = 256u, /* 8 km out */
    UW_TP_TICK = 120u,          /* the box opens after land.png */
    UW_LOADED_TICKS =
        500u,            /* at the surface, breathing: a far jump's columns are drawn within 15 s (plan 006 phase 01) */
    UW_SEA_TICKS = 140u, /* then down: as the walked script had it, from diving to sea.png */
    UW_NO_AIR_TICKS = 180u, /* and from sea.png to sea_no_air.png */
};
static const float32_t UW_SEA_DEEP = -30.0f32; /* sea bed this far under the surface, at least */

typedef struct uw_script_t {
    char text[UW_SCRIPT_BYTES]; /* 0 */
    uint64_t length;            /* 8192 */
    uint32_t tick;              /* 8200 */
    uint8_t _pad0[4];           /* 8204 */
} uw_script_t;
static_assert(sizeof(uw_script_t) == 8208);

static void uw_line(uw_script_t *s, const char *line) {
    const int32_t wrote = snprintf(s->text + s->length, UW_SCRIPT_BYTES - s->length, "%s\n", line);
    if (wrote > 0) {
        s->length += (uint64_t)wrote;
    }
}

static void uw_key(uw_script_t *s, uint32_t after, const char *name) {
    char line[64];
    s->tick += after;
    snprintf(line, sizeof(line), "%" PRIu32 " key %s down", s->tick, name);
    uw_line(s, line);
    snprintf(line, sizeof(line), "%" PRIu32 " key %s up", s->tick, name);
    uw_line(s, line);
}

/* Each character as the key a US keyboard types it with, as tests/run/game/command/ types its commands. */
static void uw_type(uw_script_t *s, const char *text) {
    for (const char *c = text; *c != '\0'; c++) {
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
        uw_key(s, *c == '/' ? 1u : 2u, name);
    }
}

/* The deep sea nearest the world's middle on the default seed, from the terrain itself. The walked script this replaced went from
 * the start over a beach to sea; since plan 006 phase 03 a jungle stands in that way, and the land will move again (plan 007), so
 * the dive finds its sea, and goes there by /tp. */
static bool uw_sea_find(test_context_t *t, int32_t *out_x, int32_t *out_y) {
    terrain_t *const terrain = test_alloc(t, sizeof(terrain_t));
    if (!CHECK(terrain_init(terrain, TERRAIN_SEED_DEFAULT))) {
        return false;
    }
    bool found = false;
    const int32_t step = (int32_t)UW_SEA_SEARCH_STEP;
    for (int32_t ring = 0; ring < (int32_t)UW_SEA_SEARCH_RINGS && !found; ring++) {
        for (int32_t dy = -ring; dy <= ring && !found; dy++) {
            for (int32_t dx = -ring; dx <= ring && !found; dx++) {
                if (dx != -ring && dx != ring && dy != -ring && dy != ring) {
                    continue;
                }
                const int32_t x = GAME_WORLD_MIDDLE + dx * step, y = GAME_WORLD_MIDDLE + dy * step;
                const terrain_hydrology_column_t column =
                    terrain_hydrology_column_at(terrain->borrowed_hydrology, x, y);
                if ((column.kind == TERRAIN_HYDROLOGY_KIND_OCEAN || column.kind == TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN) &&
                    column.ground <= UW_SEA_DEEP) {
                    *out_x = x;
                    *out_y = y;
                    found = true;
                }
            }
        }
    }
    terrain_deinit(terrain);
    return CHECK(found);
}

/* Stands at the start for land.png, then /tp to the sea's surface, swims there until the place is drawn, and sinks to the bed. */
static bool uw_script_make(test_context_t *t, const char *path) {
    int32_t x = 0, y = 0;
    if (!uw_sea_find(t, &x, &y)) {
        return false;
    }
    printf("  the dive's sea: %" PRId32 ", %" PRId32 "\n", x, y);
    uw_script_t *const s = test_alloc(t, sizeof(uw_script_t));
    memset(s, 0, sizeof(*s));
    uw_line(s, "100 screenshot land.png");
    s->tick = UW_TP_TICK;
    char command[96];
    snprintf(command, sizeof(command), "/tp %" PRId32 " %" PRId32 " 1", x, y);
    uw_type(s, command);
    uw_key(s, 2u, "enter");
    char line[64];
    /* Space held keeps it swimming at the surface, breathing, as the walked script jumped its way out; then down. */
    snprintf(line, sizeof(line), "%" PRIu32 " key space down", s->tick + 2u);
    uw_line(s, line);
    const uint32_t dive = s->tick + UW_LOADED_TICKS;
    snprintf(line, sizeof(line), "%" PRIu32 " key space up", dive);
    uw_line(s, line);
    snprintf(line, sizeof(line), "%" PRIu32 " key lshift down", dive);
    uw_line(s, line);
    const uint32_t sea = dive + UW_SEA_TICKS;
    snprintf(line, sizeof(line), "%" PRIu32 " screenshot sea.png", sea);
    uw_line(s, line);
    snprintf(line, sizeof(line), "%" PRIu32 " screenshot sea_no_air.png", sea + UW_NO_AIR_TICKS);
    uw_line(s, line);
    snprintf(line, sizeof(line), "%" PRIu32 " end", sea + UW_NO_AIR_TICKS + 1u);
    uw_line(s, line);
    return CHECK_EQ_U64(file_write_atomic(path, s->text, s->length), FILE_OK);
}

/* flag may be nullptr, which then ends the list. */
static void uw_dive(test_context_t *t, const char *dir, const char *flag) {
    char *const argv[] = {
        t->borrowed_args[0],
        (char *)"--headless",
        (char *)"--test-world",
        (char *)"--walk",
        (char *)"--no-day-cycle",
        (char *)"--no-outline", /* whatever the eye ends up aimed at, its black outline is not the water's */
        (char *)"--time-of-day",
        (char *)"0.5",
        (char *)"--render-distance",
        (char *)"12",
        (char *)"--script",
        (char *)uw_dive_script,
        (char *)"--screenshot-dir",
        (char *)dir,
        (char *)"--width",
        (char *)"320",
        (char *)"--height",
        (char *)"180",
        (char *)flag,
        nullptr};
    test_process_t run;
    test_process_run(t, argv, UW_PROGRAM_TIMEOUT_MS, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
}

TEST(a_real_body_dives_into_a_real_sea_and_sees_water) {
    uw_temp_dir_t on, off;
    REQUIRE(uw_temp_dir_make(&on));
    REQUIRE(uw_temp_dir_make(&off));
    snprintf(uw_dive_script, sizeof(uw_dive_script), "%s/dive.script", on.path);
    REQUIRE(uw_script_make(t, uw_dive_script));
    uw_dive(t, on.path, nullptr);
    uw_dive(t, off.path, "--no-under-water");
    char path[256];

    /* On land the setting changes nothing, to the byte. */
    snprintf(path, sizeof(path), "%s/land.png", on.path);
    const uint8_t *const land_on = uw_png_read(t, path);
    snprintf(path, sizeof(path), "%s/land.png", off.path);
    const uint8_t *const land_off = uw_png_read(t, path);
    REQUIRE(land_on != nullptr && land_off != nullptr);
    CHECK_MEM_EQ(land_on, land_off, (uint64_t)UW_WIDTH * UW_HEIGHT * 4u);

    /* Deep down, what was very nearly all black is not. */
    snprintf(path, sizeof(path), "%s/sea.png", on.path);
    const uint8_t *const sea_on = uw_png_read(t, path);
    snprintf(path, sizeof(path), "%s/sea.png", off.path);
    const uint8_t *const sea_off = uw_png_read(t, path);
    REQUIRE(sea_on != nullptr && sea_off != nullptr);
    const uint64_t black_on = uw_black_pixels(sea_on), black_off = uw_black_pixels(sea_off);
    printf("  black pixels under the sea: %" PRIu64 " with it, %" PRIu64 " without, of %" PRIu64 "\n", black_on,
           black_off, (uint64_t)UW_WIDTH * UW_HEIGHT);
    CHECK_EQ_U64(black_on, 0u);
    CHECK(black_off > 0u);
    const uint32_t middle = uw_middle(sea_on);
    CHECK((middle & 255u) > ((middle >> 16) & 255u));

    uw_golden_compare(t, on.path, "land.png");
    uw_golden_compare(t, on.path, "sea.png");
    uw_golden_compare(t, on.path, "sea_no_air.png");
    uw_temp_dir_remove(&on);
    uw_temp_dir_remove(&off);
}

TEST(the_flag_is_refused_with_no_world) {
    char *const alone[] = {t->borrowed_args[0], (char *)"--headless",       (char *)"--ticks",
                           (char *)"1",         (char *)"--no-under-water", nullptr};
    test_process_t run;
    test_process_run(t, alone, UW_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "only mean something with --test-world");
}

TEST_MAIN()
