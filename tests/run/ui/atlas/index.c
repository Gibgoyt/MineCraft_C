/* ctest run_ui_atlas. See README.md. */
#include "harness/index.h"

#include "climate/layer/index.h"
#include "climate/cell/index.h"
#include "climate/systems/index.h"
#include "climate/weather/index.h"
#include "display/headless/png/index.h"
#include "file/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"
#include "world/column/index.h"
#include "world/section/index.h"

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The golden pictures' world: seed 2198175988018515243, and the place they are drawn round (found by the scan in
 * tests/run/ui/atlas/README.md: sea, a coast and land rising past 300 blocks inside 2 km). */
static const uint64_t GOLD_SEED = 2198175988018515243ull;
static const float64_t GOLD_X = 1290.0f64;
static const float64_t GOLD_Y = -2410.0f64;
static const float64_t GOLD_T = 196.4f64; /* the systems' time: day 196 of year 1 (D 196), 15:36 */

enum : uint32_t {
    GOLD_WIDTH = 320u,
    GOLD_HEIGHT = 240u,
};

static uint64_t rng_next(uint64_t *state) {
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static int64_t floor_div(int64_t a, int64_t b) {
    const int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

static void *aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

typedef struct temp_dir_t {
    char path[64];
} temp_dir_t;
static_assert(sizeof(temp_dir_t) == 64);

static bool temp_dir_make(temp_dir_t *dir) {
    snprintf(dir->path, sizeof(dir->path), "/tmp/mc_ui_atlas_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

/* Every file in it, then it. */
static void temp_dir_remove(const temp_dir_t *dir) {
    char command[160];
    snprintf(command, sizeof(command), "rm -rf -- '%s'", dir->path);
    if (strncmp(dir->path, "/tmp/mc_ui_atlas_", 17) == 0) {
        (void)system(command);
    }
}

static uint16_t block_named(const char *name) {
    for (uint32_t b = 0u; b < BLOCK_COUNT; b++) {
        if (strcmp(block_defs[b].name, name) == 0) {
            return (uint16_t)b;
        }
    }
    return UINT16_MAX;
}

/* ---- Tiles and the store ------------------------------------------------------------------------------------------- */

TEST(a_pixel_packs_colour_and_height_and_none_means_not_generated) {
    uint64_t state = 7u;
    for (int32_t z = -250; z <= 1250; z++) {
        for (uint32_t k = 0u; k < 4u; k++) {
            const uint16_t colour = (uint16_t)rng_next(&state);
            const uint32_t pixel = ui_atlas_pixel_pack(colour, (int16_t)z);
            CHECK_EQ_I64(ui_atlas_pixel_z(pixel), z);
            CHECK_EQ_U64(ui_atlas_pixel_rgb565(pixel), colour);
            CHECK(ui_atlas_pixel_generated(pixel));
        }
    }
    for (uint32_t k = 0u; k < 1000u; k++) {
        const uint16_t colour = (uint16_t)rng_next(&state);
        CHECK_EQ_U64(ui_atlas_pixel_rgb565(ui_atlas_pixel_pack(colour, 17)), colour);
    }
    CHECK(!ui_atlas_pixel_generated(UI_ATLAS_PIXEL_NONE));
    CHECK(ui_atlas_pixel_generated(ui_atlas_pixel_pack(0u, 0)));
    CHECK_EQ_U64(ui_atlas_pixel_pack(0u, 0), 256u << 16); /* plan 007 phase 02: heights from -256 */
}

TEST(a_columns_top_skips_air_and_plants_and_stops_at_water) {
    const uint16_t grass = block_named("grass_block"), flower = block_named("dandelion");
    const uint16_t water = block_named("water"), sand = block_named("sand");
    const uint16_t torch = block_named("torch"), stone = block_named("stone");
    REQUIRE(grass != UINT16_MAX && flower != UINT16_MAX && water != UINT16_MAX && sand != UINT16_MAX);
    REQUIRE(torch != UINT16_MAX && stone != UINT16_MAX);
    uint16_t *const colours = test_alloc(t, BLOCK_COUNT * sizeof(uint16_t));
    for (uint32_t b = 0u; b < BLOCK_COUNT; b++) {
        colours[b] = (uint16_t)(b * 977u + 13u); /* each block its own */
    }
    const uint64_t voxels = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS;
    uint16_t *const states = test_alloc(t, voxels * sizeof(uint16_t)); /* zero: air */
    /* A block at height z (from sea level) at (x, y) of the column. */
#define AT(x, y, z)                                                                                                    \
    states[(uint64_t)(((z) - COLUMN_Z_STORAGE_MIN) / 16) * SECTION_VOXELS +                                            \
           section_voxel_index((x), (y), (uint32_t)(((z) - COLUMN_Z_STORAGE_MIN) % 16))]
    uint32_t expected[UI_ATLAS_COLUMN_PIXELS];
    for (uint32_t y = 0u; y < 16u; y++) {
        for (uint32_t x = 0u; x < 16u; x++) {
            const uint32_t kind = (x + y) % 3u;
            uint32_t pixel = 0u;
            if (kind == 0u) { /* grass at 64 under a flower */
                AT(x, y, 63) = stone;
                AT(x, y, 64) = grass;
                AT(x, y, 65) = flower;
                pixel = ui_atlas_pixel_pack(colours[grass], 64);
            } else if (kind == 1u) { /* the sea at 0 over sand at -12 */
                AT(x, y, -12) = sand;
                for (int32_t z = -11; z <= 0; z++) {
                    AT(x, y, z) = water;
                }
                pixel = ui_atlas_pixel_pack(colours[water], 0);
            } else { /* a torch on stone */
                AT(x, y, 30) = stone;
                AT(x, y, 31) = torch;
                pixel = ui_atlas_pixel_pack(colours[stone], 30);
            }
            expected[(15u - y) * 16u + x] = pixel; /* row 0 north */
        }
    }
#undef AT
    uint32_t top[UI_ATLAS_COLUMN_PIXELS];
    ui_atlas_column_top(colours, BLOCK_COUNT, states, top);
    uint64_t wrong = 0u;
    for (uint32_t k = 0u; k < UI_ATLAS_COLUMN_PIXELS; k++) {
        wrong += top[k] == expected[k] ? 0u : 1u;
    }
    CHECK_EQ_U64(wrong, 0u);
}

TEST(a_column_lands_in_its_tile_at_its_place_and_nowhere_else) {
    /* Tiles -1 and 0 each way: 64 by 64 columns, x and y from -512 to 511. The model is a block's pixel, by block. */
    uint32_t *const tiles[2][2] = {{test_alloc(t, UI_ATLAS_TILE_BYTES), test_alloc(t, UI_ATLAS_TILE_BYTES)},
                                   {test_alloc(t, UI_ATLAS_TILE_BYTES), test_alloc(t, UI_ATLAS_TILE_BYTES)}};
    uint32_t *const model = test_alloc(t, 1024u * 1024u * 4u); /* block (x + 512, y + 512) */
    for (uint32_t k = 0u; k < 1024u * 1024u; k++) {
        model[k] = UI_ATLAS_PIXEL_NONE;
    }
    for (uint32_t a = 0u; a < 2u; a++) {
        for (uint32_t b = 0u; b < 2u; b++) {
            for (uint32_t k = 0u; k < UI_ATLAS_TILE_PIXELS; k++) {
                tiles[a][b][k] = UI_ATLAS_PIXEL_NONE;
            }
        }
    }
    uint64_t state = 99u;
    for (uint32_t n = 0u; n < 10000u; n++) {
        const int32_t cx = (int32_t)(rng_next(&state) % 64u) - 32, cy = (int32_t)(rng_next(&state) % 64u) - 32;
        uint32_t column[UI_ATLAS_COLUMN_PIXELS];
        for (uint32_t k = 0u; k < UI_ATLAS_COLUMN_PIXELS; k++) {
            column[k] = ui_atlas_pixel_pack((uint16_t)rng_next(&state), (int16_t)(rng_next(&state) % 1500u) - 250);
        }
        const int32_t tx = ui_atlas_tile_of((int64_t)cx * 16, 0u), ty = ui_atlas_tile_of((int64_t)cy * 16, 0u);
        CHECK_EQ_I64(tx, cx < 0 ? -1 : 0);
        CHECK_EQ_I64(ty, cy < 0 ? -1 : 0);
        ui_atlas_tile_column_put(tiles[tx + 1][ty + 1], cx, cy, column);
        for (uint32_t r = 0u; r < 16u; r++) {
            for (uint32_t c = 0u; c < 16u; c++) {
                const int64_t x = (int64_t)cx * 16 + c, y = (int64_t)cy * 16 + 15 - r;
                model[(uint64_t)(y + 512) * 1024u + (uint64_t)(x + 512)] = column[r * 16u + c];
            }
        }
    }
    /* Every block of all four tiles against the model, by the plan's arithmetic: i = x - 512 tx, j = 511 - (y - 512 ty). */
    uint64_t wrong = 0u;
    for (int64_t y = -512; y < 512; y++) {
        for (int64_t x = -512; x < 512; x++) {
            const int64_t tx = floor_div(x, 512), ty = floor_div(y, 512);
            const int64_t i = x - 512 * tx, j = 511 - (y - 512 * ty);
            const uint32_t got = tiles[tx + 1][ty + 1][j * 512 + i];
            wrong += got == model[(uint64_t)(y + 512) * 1024u + (uint64_t)(x + 512)] ? 0u : 1u;
        }
    }
    CHECK_EQ_U64(wrong, 0u);
}

TEST(a_mip_is_the_halving_of_its_children_as_worked_by_hand) {
    /* The Background's example: three generated, one not. */
    const uint32_t four[4] = {ui_atlas_pixel_pack((uint16_t)(25u << 11 | 25u << 5 | 5u), 70),
                              ui_atlas_pixel_pack((uint16_t)(22u << 11 | 30u << 5 | 7u), 74), UI_ATLAS_PIXEL_NONE,
                              ui_atlas_pixel_pack((uint16_t)(27u << 11 | 22u << 5 | 2u), 66)};
    const uint32_t mean = ui_atlas_pixel_mean(four);
    CHECK_EQ_U64(ui_atlas_pixel_rgb565(mean), (uint64_t)(25u << 11 | 26u << 5 | 5u));
    CHECK_EQ_I64(ui_atlas_pixel_z(mean), 70);
    const uint32_t none[4] = {UI_ATLAS_PIXEL_NONE, UI_ATLAS_PIXEL_NONE, UI_ATLAS_PIXEL_NONE, UI_ATLAS_PIXEL_NONE};
    CHECK_EQ_U64(ui_atlas_pixel_mean(none), UI_ATLAS_PIXEL_NONE);
    /* Random children against a model of their own: each parent pixel from its 2 x 2, floor((sum + n / 2) / n). */
    uint32_t *const child = test_alloc(t, UI_ATLAS_TILE_BYTES);
    uint32_t *const parent = test_alloc(t, UI_ATLAS_TILE_BYTES);
    uint64_t state = 3u;
    uint64_t wrong = 0u;
    for (uint32_t quarter = 0u; quarter < 4u; quarter++) {
        for (uint32_t k = 0u; k < UI_ATLAS_TILE_PIXELS; k++) {
            child[k] = rng_next(&state) % 5u == 0u
                           ? UI_ATLAS_PIXEL_NONE
                           : ui_atlas_pixel_pack((uint16_t)rng_next(&state), (int16_t)(rng_next(&state) % 1500u) - 250);
        }
        ui_atlas_tile_halve(child, quarter, parent);
        const uint32_t column0 = (quarter & 1u) != 0u ? 256u : 0u, row0 = (quarter & 2u) != 0u ? 0u : 256u;
        for (uint32_t j = 0u; j < 256u; j++) {
            for (uint32_t i = 0u; i < 256u; i++) {
                int64_t sums[4] = {0, 0, 0, 0}, n = 0;
                for (uint32_t d = 0u; d < 4u; d++) {
                    const uint32_t p = child[(2u * j + d / 2u) * 512u + 2u * i + d % 2u];
                    if (!ui_atlas_pixel_generated(p)) {
                        continue;
                    }
                    const uint32_t c = p & 0xFFFFu;
                    sums[0] += c >> 11;
                    sums[1] += (c >> 5) & 63u;
                    sums[2] += c & 31u;
                    sums[3] += ui_atlas_pixel_z(p);
                    n++;
                }
                uint32_t want = UI_ATLAS_PIXEL_NONE;
                if (n > 0) {
                    int64_t m[4];
                    for (uint32_t q = 0u; q < 4u; q++) {
                        m[q] = floor_div(sums[q] + n / 2, n);
                    }
                    want = ui_atlas_pixel_pack((uint16_t)(m[0] << 11 | m[1] << 5 | m[2]), (int32_t)m[3]);
                }
                wrong += parent[(row0 + j) * 512u + column0 + i] == want ? 0u : 1u;
            }
        }
    }
    CHECK_EQ_U64(wrong, 0u);
}

TEST(tile_files_read_back_the_same_and_a_damaged_or_foreign_file_is_ignored) {
    uint32_t *const tile = test_alloc(t, UI_ATLAS_TILE_BYTES);
    uint32_t *const back = test_alloc(t, UI_ATLAS_TILE_BYTES);
    uint8_t *const file = test_alloc(t, UI_ATLAS_FILE_BYTES_MAX);
    uint8_t *const bad = test_alloc(t, UI_ATLAS_FILE_BYTES_MAX);
    uint64_t state = 11u;
    /* A varied tile goes raw; a sea of runs goes as runs; each reads back byte for byte. */
    for (uint32_t kind = 0u; kind < 2u; kind++) {
        for (uint32_t k = 0u; k < UI_ATLAS_TILE_PIXELS; k++) {
            tile[k] =
                kind == 0u ? (uint32_t)rng_next(&state) : (k / 1000u % 2u == 0u ? UI_ATLAS_PIXEL_NONE : 0x00103070u);
        }
        const uint64_t length = ui_atlas_tile_encode(tile, 42u, 3u, -5, 7, file);
        ui_atlas_file_header_t header;
        memcpy(&header, file, sizeof(header));
        CHECK_EQ_U64(header.encoding, kind == 0u ? UI_ATLAS_ENCODING_RAW : UI_ATLAS_ENCODING_RUNS);
        CHECK(kind == 0u || length < UI_ATLAS_TILE_BYTES / 4u);
        CHECK(ui_atlas_tile_decode(file, length, 42u, 3u, -5, 7, back) == UI_ATLAS_DECODE_OK);
        CHECK_MEM_EQ(back, tile, UI_ATLAS_TILE_BYTES);
        /* Another seed, level, place; a short file; each header byte and a payload byte flipped. */
        CHECK(ui_atlas_tile_decode(file, length, 43u, 3u, -5, 7, back) == UI_ATLAS_DECODE_FOREIGN);
        CHECK(ui_atlas_tile_decode(file, length, 42u, 2u, -5, 7, back) == UI_ATLAS_DECODE_FOREIGN);
        CHECK(ui_atlas_tile_decode(file, length, 42u, 3u, -4, 7, back) == UI_ATLAS_DECODE_FOREIGN);
        CHECK(ui_atlas_tile_decode(file, length, 42u, 3u, -5, 8, back) == UI_ATLAS_DECODE_FOREIGN);
        CHECK(ui_atlas_tile_decode(file, length - 1u, 42u, 3u, -5, 7, back) != UI_ATLAS_DECODE_OK);
        CHECK(ui_atlas_tile_decode(file, 10u, 42u, 3u, -5, 7, back) != UI_ATLAS_DECODE_OK);
        for (uint32_t at = 0u; at < sizeof(ui_atlas_file_header_t); at++) {
            memcpy(bad, file, length);
            bad[at] ^= 0x01u;
            if (!CHECK(ui_atlas_tile_decode(bad, length, 42u, 3u, -5, 7, back) != UI_ATLAS_DECODE_OK)) {
                printf("  a flip at header byte %u was taken\n", at);
            }
        }
        memcpy(bad, file, length);
        bad[sizeof(ui_atlas_file_header_t) + 5u] ^= 0x40u;
        CHECK(ui_atlas_tile_decode(bad, length, 42u, 3u, -5, 7, back) == UI_ATLAS_DECODE_PAYLOAD);
    }
    /* Runs that cover the tile short or long, with every checksum right: refused. */
    for (uint32_t k = 0u; k < UI_ATLAS_TILE_PIXELS; k++) {
        tile[k] = UI_ATLAS_PIXEL_NONE;
    }
    const uint64_t length = ui_atlas_tile_encode(tile, 1u, 0u, 0, 0, file);
    CHECK_EQ_U64(length, sizeof(ui_atlas_file_header_t) + sizeof(ui_atlas_run_t));
    for (int32_t change = -1; change <= 1; change += 2) {
        memcpy(bad, file, length);
        uint32_t count = 0u;
        memcpy(&count, bad + sizeof(ui_atlas_file_header_t) + 4u, 4u);
        count = (uint32_t)((int64_t)count + change);
        memcpy(bad + sizeof(ui_atlas_file_header_t) + 4u, &count, 4u);
        ui_atlas_file_header_t header;
        memcpy(&header, bad, sizeof(header));
        header.payload_checksum = texture_blob_hash(bad + sizeof(header), header.payload_bytes);
        header.checksum = texture_blob_hash((const uint8_t *)&header, offsetof(ui_atlas_file_header_t, checksum));
        memcpy(bad, &header, sizeof(header));
        CHECK(ui_atlas_tile_decode(bad, length, 1u, 0u, 0, 0, back) == UI_ATLAS_DECODE_RUNS);
    }
}

/* A ui with its Atlas, for the store's and the screen's tests. */
typedef struct rig_t {
    ui_t ui;           /* 0 */
    texture_t texture; /* 1152 */
} rig_t;
static_assert(sizeof(rig_t) == 1344);

static rig_t *rig_start(test_context_t *t) {
    rig_t *const rig = aligned(t, sizeof(rig_t));
    if (!texture_init(&rig->texture) || !ui_init(&rig->ui, &rig->texture)) {
        return nullptr;
    }
    return rig;
}

static void rig_stop(rig_t *rig) {
    ui_deinit(&rig->ui);
    texture_deinit(&rig->texture);
}

static void column_random(uint64_t *state, uint32_t *column) {
    for (uint32_t k = 0u; k < UI_ATLAS_COLUMN_PIXELS; k++) {
        column[k] = ui_atlas_pixel_pack((uint16_t)rng_next(state), (int16_t)(rng_next(state) % 600u) - 100);
    }
}

/* A rectangle of the store's level pixels. */
static void store_read(rig_t *rig, arena_t *arena, uint32_t level, int64_t px, int64_t py, uint32_t width,
                       uint32_t height, uint32_t *out) {
    arena_reset(arena);
    ui_atlas_store_read(ui_atlas(&rig->ui), level, px, py, width, height, out, arena);
}

static arena_t *scratch_arena(test_context_t *t, uint64_t bytes) {
    arena_t *const arena = aligned(t, sizeof(arena_t));
    arena_init(arena, "test_atlas", aligned(t, bytes), bytes);
    return arena;
}

TEST(a_tile_made_before_its_file_was_read_keeps_what_is_newer) {
    temp_dir_t dir;
    REQUIRE(temp_dir_make(&dir));
    rig_t *const rig = rig_start(t);
    REQUIRE(rig != nullptr);
    /* The file: tile (0, 0) at level 0, every pixel generated, height 5. */
    uint32_t *const tile = test_alloc(t, UI_ATLAS_TILE_BYTES);
    for (uint32_t k = 0u; k < UI_ATLAS_TILE_PIXELS; k++) {
        tile[k] = ui_atlas_pixel_pack(0x1234u, 5);
    }
    uint8_t *const file = test_alloc(t, UI_ATLAS_FILE_BYTES_MAX);
    const uint64_t length = ui_atlas_tile_encode(tile, 77u, 0u, 0, 0, file);
    char path[160];
    snprintf(path, sizeof(path), "%s/L0_0_0.tile", dir.path);
    REQUIRE(file_write_atomic(path, file, length) == FILE_OK);
    REQUIRE(ui_atlas_world_open(&rig->ui, dir.path, 77u, nullptr, nullptr));
    /* Columns written into a fresh slot, made before the file was read. */
    uint64_t state = 5u;
    uint32_t columns[3][UI_ATLAS_COLUMN_PIXELS];
    static const int32_t AT[3][2] = {{0, 0}, {5, 9}, {31, 31}};
    for (uint32_t c = 0u; c < 3u; c++) {
        column_random(&state, columns[c]);
        ui_atlas_column_write(ui_atlas(&rig->ui), AT[c][0], AT[c][1], columns[c]);
    }
    arena_t *const arena = scratch_arena(t, 8ull << 20);
    uint32_t *const read = test_alloc(t, UI_ATLAS_TILE_BYTES);
    store_read(rig, arena, 0u, 0, -512, 512u, 512u, read); /* level-0 rows -512 to -1 are tile y 0 */
    uint64_t from_file = 0u, from_columns = 0u, wrong = 0u;
    for (int64_t y = 0; y < 512; y++) {
        for (int64_t x = 0; x < 512; x++) {
            const uint32_t got = read[(511 - y) * 512 + x];
            uint32_t want = ui_atlas_pixel_pack(0x1234u, 5);
            for (uint32_t c = 0u; c < 3u; c++) {
                if (x / 16 == AT[c][0] && y / 16 == AT[c][1]) {
                    want = columns[c][(15 - y % 16) * 16 + x % 16];
                }
            }
            wrong += got == want ? 0u : 1u;
            from_file += got == ui_atlas_pixel_pack(0x1234u, 5) ? 1u : 0u;
            from_columns += got != ui_atlas_pixel_pack(0x1234u, 5) ? 1u : 0u;
        }
    }
    CHECK_EQ_U64(wrong, 0u);
    CHECK_EQ_U64(from_columns, 3u * 256u);
    CHECK_EQ_U64(from_file, 512u * 512u - 3u * 256u);
    ui_atlas_world_close(&rig->ui);
    arena_deinit(arena);
    rig_stop(rig);
    temp_dir_remove(&dir);
}

TEST(tiles_written_while_playing_are_there_after_a_reopen) {
    /* The store as the game drives it: 50 columns written as they generate, the world closed (which saves), opened again,
     * and read before anything has streamed. The game's own call of this write is in run/src/game/index.c; the built
     * program's test below opens the Atlas in a real world. */
    temp_dir_t dir;
    REQUIRE(temp_dir_make(&dir));
    rig_t *const rig = rig_start(t);
    REQUIRE(rig != nullptr);
    REQUIRE(ui_atlas_world_open(&rig->ui, dir.path, 9u, nullptr, nullptr));
    uint64_t state = 21u;
    int32_t at[50][2];
    uint32_t *const columns = test_alloc(t, 50u * UI_ATLAS_COLUMN_PIXELS * 4u);
    for (uint32_t c = 0u; c < 50u; c++) {
        at[c][0] = (int32_t)(rng_next(&state) % 80u) - 40;
        at[c][1] = (int32_t)(rng_next(&state) % 80u) - 40;
        for (uint32_t d = 0u; d < c; d++) { /* each column once, so the last write is the only one */
            if (at[d][0] == at[c][0] && at[d][1] == at[c][1]) {
                at[c][0] += 100;
            }
        }
        column_random(&state, columns + (uint64_t)c * UI_ATLAS_COLUMN_PIXELS);
        ui_atlas_column_write(ui_atlas(&rig->ui), at[c][0], at[c][1], columns + (uint64_t)c * UI_ATLAS_COLUMN_PIXELS);
    }
    ui_atlas_world_close(&rig->ui);
    char path[160];
    snprintf(path, sizeof(path), "%s/L0_0_0.tile", dir.path);
    uint64_t size = 0u;
    CHECK(file_path_size(path, &size) == FILE_OK); /* (0, 0) holds some of them */
    REQUIRE(ui_atlas_world_open(&rig->ui, dir.path, 9u, nullptr, nullptr));
    arena_t *const arena = scratch_arena(t, 8ull << 20);
    uint32_t read[UI_ATLAS_COLUMN_PIXELS];
    uint64_t wrong = 0u;
    for (uint32_t c = 0u; c < 50u; c++) {
        store_read(rig, arena, 0u, (int64_t)at[c][0] * 16, -1 - ((int64_t)at[c][1] * 16 + 15), 16u, 16u, read);
        wrong += memcmp(read, columns + (uint64_t)c * UI_ATLAS_COLUMN_PIXELS, sizeof(read)) == 0 ? 0u : 1u;
    }
    CHECK_EQ_U64(wrong, 0u);
    /* Another seed's world does not read them. */
    ui_atlas_world_close(&rig->ui);
    REQUIRE(ui_atlas_world_open(&rig->ui, dir.path, 10u, nullptr, nullptr));
    store_read(rig, arena, 0u, (int64_t)at[0][0] * 16, -1 - ((int64_t)at[0][1] * 16 + 15), 16u, 16u, read);
    CHECK(!ui_atlas_pixel_generated(read[0]));
    CHECK(ui_atlas(&rig->ui)->store.files_refused > 0u);
    ui_atlas_world_close(&rig->ui);
    arena_deinit(arena);
    rig_stop(rig);
    temp_dir_remove(&dir);
}

TEST(mips_follow_their_children_after_a_save) {
    temp_dir_t dir;
    REQUIRE(temp_dir_make(&dir));
    rig_t *const rig = rig_start(t);
    REQUIRE(rig != nullptr);
    REQUIRE(ui_atlas_world_open(&rig->ui, dir.path, 3u, nullptr, nullptr));
    ui_atlas_t *const atlas = ui_atlas(&rig->ui);
    /* Columns in four level-0 tiles round the origin: (-1, -1) to (0, 0). */
    uint64_t state = 8u;
    for (uint32_t n = 0u; n < 600u; n++) {
        uint32_t column[UI_ATLAS_COLUMN_PIXELS];
        column_random(&state, column);
        ui_atlas_column_write(atlas, (int32_t)(rng_next(&state) % 64u) - 32, (int32_t)(rng_next(&state) % 64u) - 32,
                              column);
    }
    arena_t *const arena = scratch_arena(t, 16ull << 20);
    ui_atlas_save_now(atlas, arena);
    /* Level 0 over the 1024 by 1024 blocks, then each level halved by hand and read from the store. */
    uint32_t *const levels[2] = {test_alloc(t, 1024u * 1024u * 4u), test_alloc(t, 1024u * 1024u * 4u)};
    uint32_t *const read = test_alloc(t, 1024u * 1024u * 4u);
    store_read(rig, arena, 0u, -512, -512, 1024u, 1024u, levels[0]);
    uint32_t side = 1024u;
    uint64_t wrong = 0u;
    for (uint32_t level = 1u; level < UI_ATLAS_LEVELS; level++) {
        const uint32_t *const below = levels[(level - 1u) % 2u];
        uint32_t *const here = levels[level % 2u];
        const uint32_t half = side / 2u;
        for (uint32_t j = 0u; j < half; j++) {
            for (uint32_t i = 0u; i < half; i++) {
                const uint32_t four[4] = {below[(2u * j) * side + 2u * i], below[(2u * j) * side + 2u * i + 1u],
                                          below[(2u * j + 1u) * side + 2u * i],
                                          below[(2u * j + 1u) * side + 2u * i + 1u]};
                here[j * half + i] = ui_atlas_pixel_mean(four);
            }
        }
        side = half;
        /* The same blocks, -512 to 511, at this level: pixels -512 / 2^L on. */
        const int64_t first = -512 >> level;
        store_read(rig, arena, level, first, first, side, side, read);
        for (uint32_t k = 0u; k < side * side; k++) {
            wrong += read[k] == here[k] ? 0u : 1u;
        }
        if (side == 1u) {
            break;
        }
    }
    CHECK_EQ_U64(wrong, 0u);
    ui_atlas_world_close(&rig->ui);
    arena_deinit(arena);
    rig_stop(rig);
    temp_dir_remove(&dir);
}

typedef struct lock_race_t {
    rig_t *rig;            /* 0 */
    _Atomic uint32_t stop; /* 8 */
    uint8_t _pad0[4];      /* 12 */
    uint64_t writes;       /* 16 */
    uint64_t reads;        /* 24 */
    uint64_t saves;        /* 32 */
    arena_t *read_arena;   /* 40 */
    arena_t *save_arena;   /* 48 */
} lock_race_t;
static_assert(sizeof(lock_race_t) == 56);

static void race_writer(void *borrowed_argument) {
    lock_race_t *const race = borrowed_argument;
    uint64_t state = 1u;
    while (atomic_load_explicit(&race->stop, memory_order_acquire) == 0u) {
        uint32_t column[UI_ATLAS_COLUMN_PIXELS];
        column_random(&state, column);
        ui_atlas_column_write(ui_atlas(&race->rig->ui), (int32_t)(rng_next(&state) % 128u) - 64,
                              (int32_t)(rng_next(&state) % 128u) - 64, column);
        race->writes++;
    }
}

static void race_reader(void *borrowed_argument) {
    lock_race_t *const race = borrowed_argument;
    while (atomic_load_explicit(&race->stop, memory_order_acquire) == 0u) {
        arena_reset(race->read_arena);
        uint32_t *const out = arena_alloc(race->read_arena, 1024u * 1024u * 4u, 64u);
        ui_atlas_store_read(ui_atlas(&race->rig->ui), (uint32_t)(race->reads % 3u), -512, -512, 1024u, 1024u, out,
                            race->read_arena);
        race->reads++;
    }
}

static void race_saver(void *borrowed_argument) {
    lock_race_t *const race = borrowed_argument;
    while (atomic_load_explicit(&race->stop, memory_order_acquire) == 0u) {
        arena_reset(race->save_arena);
        ui_atlas_save_now(ui_atlas(&race->rig->ui), race->save_arena);
        race->saves++;
    }
}

TEST(the_sim_never_waits_long_on_the_lock) {
    temp_dir_t dir;
    REQUIRE(temp_dir_make(&dir));
    rig_t *const rig = rig_start(t);
    REQUIRE(rig != nullptr);
    REQUIRE(ui_atlas_world_open(&rig->ui, dir.path, 4u, nullptr, nullptr));
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    REQUIRE(thread_topology_read(topology));
    lock_race_t *const race = aligned(t, sizeof(lock_race_t));
    race->rig = rig;
    race->read_arena = scratch_arena(t, 16ull << 20);
    race->save_arena = scratch_arena(t, 16ull << 20);
    thread_t *const threads = aligned(t, 3u * sizeof(thread_t));
    REQUIRE(thread_start(&threads[0], topology, THREAD_ROLE_WORKER, 0u, race_writer, race));
    REQUIRE(thread_start(&threads[1], topology, THREAD_ROLE_WORKER, 1u, race_reader, race));
    REQUIRE(thread_start(&threads[2], topology, THREAD_ROLE_WORKER, 2u, race_saver, race));
    const int64_t until = time_now_ns() + 5000000000;
    while (time_now_ns() < until) {
        time_sleep_ns(50000000);
    }
    atomic_store_explicit(&race->stop, 1u, memory_order_release);
    for (uint32_t k = 0u; k < 3u; k++) {
        thread_join(&threads[k]);
    }
    const uint64_t waited = ui_atlas(&rig->ui)->store.wait_ns_most;
    printf("%" PRIu64 " writes, %" PRIu64 " reads, %" PRIu64 " saves; the longest wait %.3f ms\n", race->writes,
           race->reads, race->saves, (libc_double_t)((float64_t)waited / 1.0e6f64));
    CHECK(race->writes > 1000u && race->reads > 0u && race->saves > 0u);
    /* 2 ms is a budget at the build's own speed. Under the sanitizers every copy of a tile under the lock is checked
     * byte by byte, ten times slower, so the bound there is ten times wider: what they are for is the races, not the time. */
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
    CHECK(waited < 20000000u);
#else
    CHECK(waited < 2000000u);
#endif
    ui_atlas_world_close(&rig->ui);
    arena_deinit(race->read_arena);
    arena_deinit(race->save_arena);
    rig_stop(rig);
    temp_dir_remove(&dir);
}

TEST(with_every_slot_dirty_a_write_is_dropped_counted_and_said_once) {
    temp_dir_t dir;
    REQUIRE(temp_dir_make(&dir));
    rig_t *const rig = rig_start(t);
    REQUIRE(rig != nullptr);
    REQUIRE(ui_atlas_world_open(&rig->ui, dir.path, 5u, nullptr, nullptr));
    ui_atlas_t *const atlas = ui_atlas(&rig->ui);
    uint32_t column[UI_ATLAS_COLUMN_PIXELS];
    uint64_t state = 2u;
    column_random(&state, column);
    /* One column in each of 160 tiles fills every slot with a dirty tile; 40 more have nowhere to go. */
    for (uint32_t n = 0u; n < UI_ATLAS_SLOTS + 40u; n++) {
        ui_atlas_column_write(atlas, (int32_t)n * 32, 0, column);
    }
    CHECK_EQ_U64(atlas->store.columns_lost, 40u);
    CHECK_EQ_U64(atlas->store.lost_said, 1u);
    /* A save makes room again. */
    arena_t *const arena = scratch_arena(t, 16ull << 20);
    ui_atlas_save_now(atlas, arena);
    ui_atlas_column_write(atlas, 100000, 0, column);
    CHECK_EQ_U64(atlas->store.columns_lost, 40u);
    ui_atlas_world_close(&rig->ui);
    arena_deinit(arena);
    rig_stop(rig);
    temp_dir_remove(&dir);
}

/* ---- Drawing ---------------------------------------------------------------------------------------------------------- */

TEST(the_layer_table_is_whole) {
    bool group_used[UI_ATLAS_GROUPS + 1u] = {false};
    for (uint32_t i = 0u; i < UI_ATLAS_LAYER_COUNT; i++) {
        const ui_atlas_layer_t *const row = &ui_atlas_layers[i];
        bool ok = row->key != nullptr && row->label != nullptr && row->unit != nullptr && row->band != nullptr;
        for (const char *c = row->key; ok && *c != '\0'; c++) {
            ok = (*c >= 'a' && *c <= 'z') || *c == '_' || (*c >= '0' && *c <= '9');
        }
        for (uint32_t j = 0u; ok && j < i; j++) {
            ok = strcmp(ui_atlas_layers[j].key, row->key) != 0;
        }
        ok = ok && row->group >= 1u && row->group <= UI_ATLAS_GROUPS;
        ok = ok && (row->over_grey == 0u || row->time == UI_ATLAS_TIME_NOW);
        /* A field layer has a scale or entries; the terrain and the systems-only layers need none. */
        ui_atlas_scale_t scale;
        const bool field = row->climate_layer != UI_ATLAS_NO_CLIMATE_LAYER ||
                           (row->value != UI_ATLAS_VALUE_NONE && row->value != UI_ATLAS_VALUE_CONTOURS);
        ok = ok && (!field || ui_atlas_layer_scale(i, &scale) || row->entry_count > 0u);
        ok = ok && (row->entry_count == 0u) == (row->entries == nullptr);
        if (!CHECK(ok)) {
            printf("  row %u (%s) is not whole\n", i, row->key != nullptr ? row->key : "?");
        }
        group_used[row->group <= UI_ATLAS_GROUPS ? row->group : 0u] = true;
    }
    for (uint32_t g = 1u; g <= UI_ATLAS_GROUPS; g++) {
        CHECK(group_used[g]);
    }
    CHECK_EQ_U64(ui_atlas_layer_find("terrain"), 0u);
    CHECK_EQ_U64(ui_atlas_layer_find("flood"), 25u);
    CHECK_EQ_U64(ui_atlas_layer_find("nonsense"), UI_ATLAS_LAYER_COUNT);
}

TEST(hill_shading_is_the_worked_example) {
    CHECK_NEAR(ui_atlas_shade(70.0f32, 70.0f32, 70.0f32, 70.0f32, 1.0f32, 1.0f32), 1.0f32, 0.001f32);
    CHECK_NEAR(ui_atlas_shade(70.0f32, 74.0f32, 72.0f32, 72.0f32, 1.0f32, 1.0f32), 1.079f32, 0.001f32);
    CHECK_NEAR(ui_atlas_shade(74.0f32, 70.0f32, 72.0f32, 72.0f32, 1.0f32, 1.0f32), 0.4f32, 0.001f32);
    /* And the colour: each channel times b, rounded, held at 255. */
    CHECK_EQ_U64(ui_atlas_shade_apply(0x00646464u, 1.079f32), 0xFF6C6C6Cu);
    CHECK_EQ_U64(ui_atlas_shade_apply(0x00F0F0F0u, 1.3f32), 0xFFFFFFFFu);
}

TEST(marching_squares_is_the_worked_example) {
    const float32_t square[4] = {1010.0f32, 1013.0f32, 1015.0f32, 1011.0f32};
    ui_atlas_segment_t out[2];
    REQUIRE(ui_atlas_march(square, 1012.0f32, 4.0f32, out) == 1u);
    CHECK_NEAR(out[0].x0, 2.6667f32, 0.01f32);
    CHECK_NEAR(out[0].y0, 0.0f32, 0.01f32);
    CHECK_NEAR(out[0].x1, 1.0f32, 0.01f32);
    CHECK_NEAR(out[0].y1, 4.0f32, 0.01f32);
    /* The saddles: top-left and bottom-right above. Mean above: joined, so the top-right and bottom-left are cut off. */
    const float32_t saddle_high[4] = {1014.0f32, 1010.0f32, 1014.0f32, 1011.0f32}; /* mean 1012.25 */
    REQUIRE(ui_atlas_march(saddle_high, 1012.0f32, 4.0f32, out) == 2u);
    CHECK_NEAR(out[0].y0, 0.0f32, 0.01f32); /* top, then right: around the top-right corner */
    CHECK_NEAR(out[0].x1, 4.0f32, 0.01f32);
    CHECK_NEAR(out[1].y0, 4.0f32, 0.01f32); /* bottom, then left */
    CHECK_NEAR(out[1].x1, 0.0f32, 0.01f32);
    const float32_t saddle_low[4] = {1013.0f32, 1009.0f32, 1013.0f32, 1009.0f32}; /* mean 1011 */
    REQUIRE(ui_atlas_march(saddle_low, 1012.0f32, 4.0f32, out) == 2u);
    CHECK_NEAR(out[0].x0, 0.0f32, 0.01f32); /* left, then top: around the top-left corner */
    CHECK_NEAR(out[0].y1, 0.0f32, 0.01f32);
    CHECK_NEAR(out[1].x0, 4.0f32, 0.01f32); /* right, then bottom */
    CHECK_NEAR(out[1].y1, 4.0f32, 0.01f32);
    /* The other saddle, top-right and bottom-left above, with the mean above: the top-left and bottom-right cut off. */
    const float32_t other[4] = {1010.0f32, 1014.0f32, 1010.0f32, 1015.0f32}; /* mean 1012.25 */
    REQUIRE(ui_atlas_march(other, 1012.0f32, 4.0f32, out) == 2u);
    CHECK_NEAR(out[0].x0, 0.0f32, 0.01f32);
    CHECK_NEAR(out[0].y1, 0.0f32, 0.01f32);
    const float32_t flat[4] = {1000.0f32, 1000.0f32, 1000.0f32, 1000.0f32};
    CHECK_EQ_U64(ui_atlas_march(flat, 1012.0f32, 4.0f32, out), 0u);
}

TEST(colour_scales_hit_their_stops_and_mix_between) {
    ui_atlas_scale_t scale;
    REQUIRE(ui_atlas_layer_scale(ui_atlas_layer_find("temperature_now"), &scale));
    CHECK_EQ_U64(ui_atlas_scale_colour(&scale, 25.0f32), (uint64_t)(249u << 16 | 167u << 8 | 103u));
    for (uint32_t k = 0u; k < scale.count; k++) {
        CHECK_EQ_U64(ui_atlas_scale_colour(&scale, scale.values[k]), scale.colours[k]);
    }
    CHECK_EQ_U64(ui_atlas_scale_colour(&scale, -100.0f32), scale.colours[0]);
    CHECK_EQ_U64(ui_atlas_scale_colour(&scale, 100.0f32), scale.colours[scale.count - 1u]);
    CHECK_EQ_U64(ui_atlas_scale_colour(&scale, nanf("")), scale.colours[0]);
    /* Every own scale is rising. */
    for (uint32_t i = 0u; i < UI_ATLAS_LAYER_COUNT; i++) {
        if (ui_atlas_layer_scale(i, &scale)) {
            for (uint32_t k = 1u; k < scale.count; k++) {
                CHECK(scale.values[k] > scale.values[k - 1u]);
            }
        }
    }
}

/* ---- The golden world ---------------------------------------------------------------------------------------------- */

typedef struct gold_t {
    thread_topology_t topology;   /* 0 */
    job_system_t *job;            /* 520 */
    climate_t *climate;           /* 528 */
    terrain_t *terrain;           /* 536 */
    arena_t *systems_arena;       /* 544 */
    climate_systems_day_t day;    /* 552 */
    ui_atlas_estimate_t estimate; /* 624 */
} gold_t;
static_assert(sizeof(gold_t) == 640);

/* A hand-placed system: its track integrated by phase 06's rules from where it starts. */
static void world_system(gold_t *w, climate_systems_mask_t *mask, climate_system_t *system, uint8_t kind, float64_t x,
                         float64_t y, float32_t radius, float32_t peak, uint32_t name_seed) {
    memset(system, 0, sizeof(*system));
    system->kind = kind;
    system->spawn_day = GOLD_T - 0.03f64; /* a moment ago, so it is still in the small picture */
    system->start_x = x;
    system->start_y = y;
    system->radius = radius;
    system->core_radius = kind == CLIMATE_SYSTEM_HURRICANE ? radius * 0.12f32 : radius * 0.5f32;
    system->peak_hpa = peak;
    system->life_days = 6.0f32;
    system->speed = kind == CLIMATE_SYSTEM_HURRICANE ? 1.0f32 : (kind == CLIMATE_SYSTEM_LOW ? 6000.0f32 : 2000.0f32);
    system->turn_psi_deg = kind == CLIMATE_SYSTEM_HURRICANE ? 25.0f32 : 0.0f32;
    system->front_length = kind == CLIMATE_SYSTEM_LOW ? radius * 1.5f32 : 0.0f32;
    system->shield_mm_hour = kind == CLIMATE_SYSTEM_LOW ? 3.0f32 : 0.0f32;
    system->name_seed = name_seed;
    system->id = 1000u + name_seed;
    climate_systems_track(w->climate, mask, system);
}

static gold_t *world_start(test_context_t *t, uint32_t workers) {
    gold_t *const w = aligned(t, sizeof(gold_t));
    w->job = aligned(t, sizeof(job_system_t));
    w->climate = aligned(t, sizeof(climate_t));
    w->terrain = aligned(t, sizeof(terrain_t));
    if (!thread_topology_read(&w->topology) || !job_init_count(w->job, &w->topology, workers) ||
        !climate_init(w->climate)) {
        return nullptr;
    }
    if (!terrain_init(w->terrain, GOLD_SEED) || !climate_open(w->climate, w->terrain)) {
        return nullptr;
    }
    const int32_t x = (int32_t)GOLD_X, y = (int32_t)GOLD_Y;
    /* The weather of a day reads tiles to 6 km round a place: temperature now needs them all. */
    climate_prepare_area(w->climate, w->job, x - 7168, y - 7168, x + 7167, y + 7167, nullptr, nullptr);
    w->systems_arena = scratch_arena(t, 4ull << 20);
    /* The fixed systems: the hurricane over the ungenerated third, coming west; a low with its front; a high; cells. */
    climate_systems_mask_t *const mask = test_alloc(t, sizeof(climate_systems_mask_t));
    climate_systems_mask_start(mask, w->climate, w->systems_arena, nullptr);
    climate_system_t *const systems = aligned(t, 3u * sizeof(climate_system_t));
    world_system(w, mask, &systems[0], CLIMATE_SYSTEM_HURRICANE, GOLD_X + 205.0f64, GOLD_Y + 10.0f64, 60.0f32, -70.0f32,
                 0x5EEDu);
    world_system(w, mask, &systems[1], CLIMATE_SYSTEM_LOW, GOLD_X - 250.0f64, GOLD_Y + 60.0f64, 45.0f32, -20.0f32, 7u);
    world_system(w, mask, &systems[2], CLIMATE_SYSTEM_HIGH, GOLD_X - 160.0f64, GOLD_Y - 70.0f64, 50.0f32, 15.0f32, 9u);
    climate_thunder_t *const cells = aligned(t, 3u * sizeof(climate_thunder_t));
    for (uint32_t k = 0u; k < 3u; k++) {
        cells[k] = (climate_thunder_t){.spawn_day = GOLD_T - 0.01f64,
                                       .start_x = GOLD_X - 60.0f64 + 40.0f64 * k,
                                       .start_y = GOLD_Y - 30.0f64 - 20.0f64 * k,
                                       .id = 5000u + k,
                                       .velocity = {1200.0f32, 600.0f32},
                                       .radius = 8.0f32 + 3.0f32 * (float32_t)k,
                                       .peak_mm_hour = 25.0f32 + 10.0f32 * (float32_t)k,
                                       .peak_hpa = -2.0f32,
                                       .life_days = 0.12f32};
    }
    w->day = (climate_systems_day_t){.borrowed_systems = systems,
                                     .borrowed_thunder = cells,
                                     .centre_x = GOLD_X,
                                     .centre_y = GOLD_Y,
                                     .day = (int64_t)GOLD_T,
                                     .radius = 4000.0f32,
                                     .thunder_radius = 4000.0f32,
                                     .system_count = 3u,
                                     .system_capacity = 3u,
                                     .thunder_count = 3u,
                                     .thunder_capacity = 3u,
                                     .thunder_complete = 1u};
    w->estimate = (ui_atlas_estimate_t){.borrowed_terrain = w->terrain, .generated_east_x = INT64_MAX};
    return w;
}

static void world_stop(gold_t *w) {
    arena_deinit(w->systems_arena); /* its memory is the test arena's, which the harness clears */
    climate_close(w->climate, w->job);
    climate_deinit(w->climate);
    job_deinit(w->job);
    terrain_deinit(w->terrain);
}

/* The view of a golden picture at blocks a pixel 2^(zoom / 2), its right third ungenerated. */
static ui_atlas_draw_input_t world_input(gold_t *w, uint32_t layer, int32_t zoom, float64_t cx, float64_t cy) {
    ui_atlas_draw_input_t input = {
        .view = {.center = {cx, cy},
                 .zoom = zoom,
                 .width = GOLD_WIDTH,
                 .height = GOLD_HEIGHT,
                 .day = 196u,
                 .layer = (uint8_t)layer},
        .ground = {.borrowed_context = &w->estimate, .read = ui_atlas_estimate_read},
        .borrowed_climate = w->climate,
        .borrowed_week = climate_week_store(w->climate),
        .borrowed_font = nullptr,
        .borrowed_systems = &w->day,
        .t = GOLD_T,
        .census_week = 28u,
        .sample = UI_ATLAS_SAMPLE_PIXELS,
        .flat = 0u,
    };
    const ui_atlas_geometry_t g = ui_atlas_geometry(&input.view);
    w->estimate.generated_east_x = (int64_t)floor(((float64_t)g.column0 + 2.0f64 * GOLD_WIDTH / 3.0f64) * g.step);
    return input;
}

typedef struct picture_t {
    uint32_t *pixels;
    uint8_t *grey;
    float32_t *field;
    uint8_t *overlay;
    ui_atlas_target_t target;
} picture_t;
static_assert(sizeof(picture_t) == 64);

static picture_t *picture_make(test_context_t *t) {
    picture_t *const p = test_alloc(t, sizeof(picture_t));
    p->pixels = test_alloc(t, GOLD_WIDTH * GOLD_HEIGHT * 4u);
    p->grey = test_alloc(t, GOLD_WIDTH * GOLD_HEIGHT);
    p->field = test_alloc(t, GOLD_WIDTH * GOLD_HEIGHT * 4u);
    p->overlay = aligned(t, UI_ATLAS_OVERLAY_SCRATCH_BYTES);
    p->target = (ui_atlas_target_t){p->pixels, p->grey, p->field, p->overlay};
    return p;
}

static uint64_t picture_hash(const picture_t *p) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint32_t k = 0u; k < GOLD_WIDTH * GOLD_HEIGHT; k++) {
        const uint32_t rgb = p->pixels[k] & 0xFFFFFFu;
        for (uint32_t b = 0u; b < 3u; b++) {
            hash ^= (rgb >> (8u * b)) & 0xFFu;
            hash *= 0x100000001B3ull;
        }
    }
    return hash;
}

/* The goldens' file: a line "<name> 0x<hash>" each. */
static const char GOLDEN_FILE[] = "tests/run/ui/atlas/fixtures/goldens.txt";

static bool golden_lookup(const char *table, const char *name, uint64_t *out) {
    const uint64_t length = strlen(name);
    for (const char *line = table; line != nullptr && *line != '\0';) {
        if (strncmp(line, name, length) == 0 && line[length] == ' ') {
            *out = strtoull(line + length + 1u, nullptr, 16);
            return true;
        }
        line = strchr(line, '\n');
        line = line != nullptr ? line + 1 : nullptr;
    }
    return false;
}

static void picture_png(test_context_t *t, const picture_t *p, const char *directory, const char *name) {
    const uint64_t capacity = display_png_bytes_needed(GOLD_WIDTH, GOLD_HEIGHT);
    uint8_t *const png = test_alloc(t, capacity);
    uint64_t bytes = 0u;
    if (display_png_encode((const uint8_t *)p->pixels, GOLD_WIDTH * 4u, GOLD_WIDTH, GOLD_HEIGHT, png, capacity,
                           &bytes)) {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s.png", directory, name);
        CHECK(file_write_atomic(path, png, bytes) == FILE_OK);
    }
}

static arena_t *picture_scratch(test_context_t *t) {
    return scratch_arena(t, ui_atlas_band_scratch_bytes(GOLD_WIDTH, GOLD_HEIGHT) + (1u << 20));
}

TEST(golden_pictures_of_every_layer_at_two_zooms) {
    gold_t *const w = world_start(t, 16u);
    REQUIRE(w != nullptr);
    rig_t *const rig = rig_start(t); /* for the font the names are written in */
    REQUIRE(rig != nullptr);
    picture_t *const p = picture_make(t);
    arena_t *const scratch = picture_scratch(t);
    const bool write = getenv("MC_GOLDEN_WRITE") != nullptr;
    const char *const png_dir = getenv("MC_ATLAS_PNG_DIR");
    char *const table = test_alloc(t, 1u << 16);
    uint64_t table_bytes = 0u;
    if (!write) {
        REQUIRE(file_read_whole(GOLDEN_FILE, table, (1u << 16) - 1u, &table_bytes) == FILE_OK);
    }
    char *const written = test_alloc(t, 1u << 16);
    uint64_t written_bytes = 0u;
    uint32_t wrong = 0u;
    static const int32_t ZOOMS[2] = {0, 6};
    static const char *const SUFFIX[2] = {"z1", "z8"};
    for (uint32_t layer = 0u; layer < UI_ATLAS_LAYER_COUNT; layer++) {
        for (uint32_t z = 0u; z < 2u; z++) {
            ui_atlas_draw_input_t input = world_input(w, layer, ZOOMS[z], GOLD_X, GOLD_Y);
            input.borrowed_font = &rig->ui.font;
            ui_atlas_draw_picture(&input, &p->target, nullptr, nullptr, 1u, scratch);
            char name[64];
            snprintf(name, sizeof(name), "%s_%s", ui_atlas_layers[layer].key, SUFFIX[z]);
            const uint64_t hash = picture_hash(p);
            written_bytes += (uint64_t)snprintf(written + written_bytes, (1u << 16) - written_bytes,
                                                "%s 0x%016" PRIX64 "\n", name, hash);
            if (png_dir != nullptr) {
                picture_png(t, p, png_dir, name);
            }
            /* Plan 007 phase 02 moved the lines onto terrain: terrain_plain is the old terrain, byte for byte. */
            static const uint64_t TERRAIN_BEFORE_CONTOURS[2] = {0x994D128267DEFE6Aull, 0x494E54C56AFDACA9ull};
            CHECK(strcmp(ui_atlas_layers[layer].key, "terrain_plain") != 0 || hash == TERRAIN_BEFORE_CONTOURS[z]);
            uint64_t want = 0u;
            if (!write && (!golden_lookup(table, name, &want) || want != hash)) {
                printf("  %s: 0x%016" PRIX64 ", the golden is 0x%016" PRIX64 "\n", name, hash, want);
                wrong++;
            }
        }
    }
    if (write) {
        CHECK(file_write_atomic(GOLDEN_FILE, written, written_bytes) == FILE_OK);
        printf("wrote %s: look at each picture (MC_ATLAS_PNG_DIR) before committing it\n", GOLDEN_FILE);
    }
    CHECK_EQ_U64(wrong, 0u);
    arena_deinit(scratch);
    rig_stop(rig);
    world_stop(w);
}

/* The colours an outline over grey may be: rings, tracks, cone edges, names and their shadow, the radar's edge. */
static bool outline_colour(uint32_t rgb) {
    static const uint32_t ALLOWED[] = {CLIMATE_LAYER_HURRICANE_RING,
                                       CLIMATE_LAYER_LOW_RING,
                                       CLIMATE_LAYER_HIGH_RING,
                                       0x80A0E0u,
                                       CLIMATE_LAYER_TRACK,
                                       0xFF6060u,
                                       0x000000u,
                                       0xFFFFFFu,
                                       CLIMATE_LAYER_THUNDER_DISC,
                                       CLIMATE_LAYER_RADAR_RGB[1]};
    for (uint32_t k = 0u; k < sizeof(ALLOWED) / sizeof(ALLOWED[0]); k++) {
        if (rgb == ALLOWED[k]) {
            return true;
        }
    }
    return false;
}

TEST(ungenerated_land_is_grey_and_shows_only_system_outlines) {
    gold_t *const w = world_start(t, 16u);
    REQUIRE(w != nullptr);
    rig_t *const rig = rig_start(t);
    REQUIRE(rig != nullptr);
    picture_t *const p = picture_make(t);
    arena_t *const scratch = picture_scratch(t);
    const uint32_t third = 2u * GOLD_WIDTH / 3u + 1u;
    for (uint32_t layer = 0u; layer < UI_ATLAS_LAYER_COUNT; layer++) {
        ui_atlas_draw_input_t input = world_input(w, layer, 0, GOLD_X, GOLD_Y);
        input.borrowed_font = &rig->ui.font;
        ui_atlas_draw_picture(&input, &p->target, nullptr, nullptr, 1u, scratch);
        const bool over = ui_atlas_layers[layer].over_grey != 0u;
        uint64_t wrong = 0u, first = UINT64_MAX;
        for (uint32_t y = 0u; y < GOLD_HEIGHT; y++) {
            for (uint32_t x = third; x < GOLD_WIDTH; x++) {
                const uint32_t pixel = p->pixels[y * GOLD_WIDTH + x];
                CHECK(p->grey[y * GOLD_WIDTH + x] != 0u);
                const bool grey = pixel == UI_ATLAS_COLOUR_UNGENERATED;
                const bool ok = grey || (over && outline_colour(pixel & 0xFFFFFFu));
                if (!ok) {
                    wrong++;
                    first = first == UINT64_MAX ? (uint64_t)y * GOLD_WIDTH + x : first;
                }
            }
        }
        if (!CHECK_EQ_U64(wrong, 0u)) {
            printf("  %s: %" PRIu64 " pixels over ungenerated land, the first at (%" PRIu64 ", %" PRIu64 ")\n",
                   ui_atlas_layers[layer].key, wrong, first % GOLD_WIDTH, first / GOLD_WIDTH);
        }
    }
    /* And the hurricane's ring is there over grey on the systems layer: 90% of its pixels in the grey third. */
    ui_atlas_draw_input_t input = world_input(w, ui_atlas_layer_find("systems"), 0, GOLD_X, GOLD_Y);
    input.borrowed_font = &rig->ui.font;
    ui_atlas_draw_picture(&input, &p->target, nullptr, nullptr, 1u, scratch);
    climate_system_state_t state;
    climate_system_state_at(&w->day.borrowed_systems[0], GOLD_T, &state);
    REQUIRE(state.alive != 0u);
    const ui_atlas_geometry_t g = ui_atlas_geometry(&input.view);
    const float64_t cx = state.x / g.step - (float64_t)g.column0, cy = -state.y / g.step - (float64_t)g.row0;
    const float64_t r = (float64_t)lround((float64_t)w->day.borrowed_systems[0].radius / g.step);
    uint64_t ring = 0u, drawn = 0u;
    for (uint32_t a = 0u; a < 720u; a++) {
        const float64_t angle = (float64_t)a * 3.14159265358979f64 / 360.0f64;
        const int64_t x = (int64_t)floor(floor(cx) + r * cos(angle) + 0.5f64);
        const int64_t y = (int64_t)floor(floor(cy) + r * sin(angle) + 0.5f64);
        if (x < (int64_t)third || x >= GOLD_WIDTH || y < 0 || y >= GOLD_HEIGHT) {
            continue;
        }
        ring++;
        /* Within a pixel of the ring the rule draws. */
        bool near = false;
        for (int64_t dy = -1; dy <= 1 && !near; dy++) {
            for (int64_t dx = -1; dx <= 1 && !near; dx++) {
                const int64_t qx = x + dx, qy = y + dy;
                near = qx >= 0 && qy >= 0 && qx < GOLD_WIDTH && qy < GOLD_HEIGHT &&
                       (p->pixels[qy * GOLD_WIDTH + qx] & 0xFFFFFFu) == CLIMATE_LAYER_HURRICANE_RING;
            }
        }
        drawn += near ? 1u : 0u;
    }
    printf("hurricane ring over grey: %" PRIu64 " of %" PRIu64 " places drawn\n", drawn, ring);
    CHECK(ring > 50u);
    CHECK(drawn * 10u >= ring * 9u);
    arena_deinit(scratch);
    rig_stop(rig);
    world_stop(w);
}

TEST(the_atlas_row_with_shading_off_is_the_climate_layers_own_bytes) {
    /* run_climate_layer holds climate_layer_draw to climate_layer_draw_scaled; this holds the Atlas's row to both: shading
     * off, every pixel sampled, nothing ungenerated, at a whole number of blocks a pixel. */
    gold_t *const w = world_start(t, 16u);
    REQUIRE(w != nullptr);
    picture_t *const p = picture_make(t);
    arena_t *const scratch = picture_scratch(t);
    uint8_t *const climate = test_alloc(t, GOLD_WIDTH * GOLD_HEIGHT * 4u);
    uint32_t checked = 0u;
    for (uint32_t layer = 0u; layer < UI_ATLAS_LAYER_COUNT; layer++) {
        const ui_atlas_layer_t *const row = &ui_atlas_layers[layer];
        if (row->climate_layer == UI_ATLAS_NO_CLIMATE_LAYER || row->climate_layer == CLIMATE_LAYER_LATITUDE) {
            continue;
        }
        /* At 8 blocks a pixel round a centre that puts world pixel column 0 of the picture on a multiple of 32. */
        ui_atlas_draw_input_t input = world_input(w, layer, 6, 2560.0f64, -2496.0f64);
        w->estimate.generated_east_x = INT64_MAX;
        input.flat = 1u;
        input.sample = 1u;
        input.borrowed_systems = nullptr;
        const ui_atlas_geometry_t g = ui_atlas_geometry(&input.view);
        REQUIRE(g.column0 % 32 == 0 && g.row0 % 32 == 0);
        ui_atlas_draw_picture(&input, &p->target, nullptr, nullptr, 1u, scratch);
        climate_layer_draw(w->climate, (climate_layer_t)row->climate_layer, (int32_t)(g.column0 * 8),
                           (int32_t)(-g.row0 * 8), 8, GOLD_WIDTH, GOLD_HEIGHT,
                           row->time == UI_ATLAS_TIME_NORMALS ? 196u : 0u, climate, GOLD_WIDTH * 4u);
        uint64_t wrong = 0u;
        for (uint32_t k = 0u; k < GOLD_WIDTH * GOLD_HEIGHT; k++) {
            uint32_t c = 0u;
            memcpy(&c, climate + 4u * k, 4u);
            /* Where a climate tile is not built the Atlas shows the ground instead of grey. */
            wrong += c == CLIMATE_LAYER_GREY || (p->pixels[k] & 0xFFFFFFu) == (c & 0xFFFFFFu) ? 0u : 1u;
        }
        if (!CHECK_EQ_U64(wrong, 0u)) {
            printf("  %s: %" PRIu64 " pixels differ from climate_layer_draw\n", row->key, wrong);
        }
        checked++;
    }
    CHECK(checked >= 12u);
    arena_deinit(scratch);
    world_stop(w);
}

TEST(any_split_into_bands_gives_the_same_bytes) {
    gold_t *const w = world_start(t, 16u);
    REQUIRE(w != nullptr);
    rig_t *const rig = rig_start(t);
    REQUIRE(rig != nullptr);
    job_system_t *const one = aligned(t, sizeof(job_system_t));
    REQUIRE(job_init_count(one, &w->topology, 1u));
    job_group_t *const group = aligned(t, sizeof(job_group_t));
    job_group_init(group);
    picture_t *const a = picture_make(t);
    picture_t *const b = picture_make(t);
    arena_t *const scratch = picture_scratch(t);
    for (uint32_t layer = 0u; layer < UI_ATLAS_LAYER_COUNT; layer++) {
        ui_atlas_draw_input_t input = world_input(w, layer, 3, GOLD_X, GOLD_Y);
        input.borrowed_font = &rig->ui.font;
        ui_atlas_draw_picture(&input, &a->target, nullptr, nullptr, 1u, scratch);
        uint32_t differ = 0u;
        static const uint32_t SPLITS[2] = {7u, 16u};
        for (uint32_t s = 0u; s < 2u; s++) {
            ui_atlas_draw_picture(&input, &b->target, nullptr, nullptr, SPLITS[s], scratch);
            differ += memcmp(a->pixels, b->pixels, GOLD_WIDTH * GOLD_HEIGHT * 4u) == 0 ? 0u : 1u;
        }
        ui_atlas_draw_picture(&input, &b->target, one, group, 16u, scratch);
        differ += memcmp(a->pixels, b->pixels, GOLD_WIDTH * GOLD_HEIGHT * 4u) == 0 ? 0u : 1u;
        ui_atlas_draw_picture(&input, &b->target, w->job, group, 16u, scratch);
        differ += memcmp(a->pixels, b->pixels, GOLD_WIDTH * GOLD_HEIGHT * 4u) == 0 ? 0u : 1u;
        if (!CHECK_EQ_U64(differ, 0u)) {
            printf("  %s: %u of 4 draws differ from one band on this thread\n", ui_atlas_layers[layer].key, differ);
        }
    }
    job_group_deinit(group);
    job_deinit(one);
    arena_deinit(scratch);
    rig_stop(rig);
    world_stop(w);
}

TEST(a_pan_samples_the_same_world_points) {
    gold_t *const w = world_start(t, 16u);
    REQUIRE(w != nullptr);
    rig_t *const rig = rig_start(t);
    REQUIRE(rig != nullptr);
    picture_t *const a = picture_make(t);
    picture_t *const b = picture_make(t);
    arena_t *const scratch = picture_scratch(t);
    enum : int32_t { PAN_X = 13, PAN_Y = 7, MARGIN = 48 };
    for (uint32_t layer = 0u; layer < UI_ATLAS_LAYER_COUNT; layer++) {
        const int32_t zoom = 3; /* 2.83 blocks a pixel: not a whole number */
        const float64_t s = ui_atlas_blocks_per_pixel(zoom);
        ui_atlas_draw_input_t input = world_input(w, layer, zoom, GOLD_X, GOLD_Y);
        input.borrowed_font = &rig->ui.font;
        w->estimate.generated_east_x = INT64_MAX; /* the same ground for both */
        ui_atlas_draw_picture(&input, &a->target, nullptr, nullptr, 1u, scratch);
        input.view.center[0] += (float64_t)PAN_X * s;
        input.view.center[1] -= (float64_t)PAN_Y * s;
        ui_atlas_draw_picture(&input, &b->target, nullptr, nullptr, 1u, scratch);
        uint64_t wrong = 0u;
        for (int32_t y = MARGIN + PAN_Y; y < (int32_t)GOLD_HEIGHT - MARGIN; y++) {
            for (int32_t x = MARGIN + PAN_X; x < (int32_t)GOLD_WIDTH - MARGIN; x++) {
                wrong +=
                    a->pixels[y * (int32_t)GOLD_WIDTH + x] == b->pixels[(y - PAN_Y) * (int32_t)GOLD_WIDTH + (x - PAN_X)]
                        ? 0u
                        : 1u;
            }
        }
        if (!CHECK_EQ_U64(wrong, 0u)) {
            printf("  %s: %" PRIu64 " pixels differ after the pan\n", ui_atlas_layers[layer].key, wrong);
        }
    }
    arena_deinit(scratch);
    rig_stop(rig);
    world_stop(w);
}

/* ---- The Atlas on screen ---------------------------------------------------------------------------------------------- */

static void key_set(input_keys_t *keys, uint32_t code) {
    keys->words[code / 64u] |= 1ull << (code % 64u);
}

static input_frame_t frame_pressing(uint32_t key) {
    input_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    if (key != 0u) {
        key_set(&frame.pressed, key);
        key_set(&frame.down, key);
    }
    return frame;
}

static ui_atlas_world_t world_now(int64_t ns) {
    return (ui_atlas_world_t){.player = {GOLD_X, GOLD_Y},
                              .t = GOLD_T + (float64_t)ns / 1.2e12f64,
                              .world_ns = ns,
                              .census_week = 28u,
                              .yaw = 0.5f32,
                              .day_of_year = 196u};
}

/* A frame of the Atlas on a canvas of the golden size, as the game draws it. */
static void screen_frame(test_context_t *t, rig_t *rig, uint32_t *canvas_pixels, const ui_atlas_world_t *world) {
    ui_atlas_frame(ui_atlas(&rig->ui), GOLD_WIDTH, GOLD_HEIGHT, world);
    REQUIRE(ui_frame_begin(&rig->ui, (uint8_t *)canvas_pixels, GOLD_WIDTH * 4u, GOLD_WIDTH, GOLD_HEIGHT));
    ui_atlas_frame_draw(ui_atlas(&rig->ui), &rig->ui.widget, &rig->ui.canvas, &rig->ui.font, world);
    (void)ui_frame_end(&rig->ui, true);
}

TEST(a_picture_is_drawn_once_per_change_and_now_layers_twice_a_second) {
    gold_t *const w = world_start(t, 16u);
    REQUIRE(w != nullptr);
    rig_t *const rig = rig_start(t);
    REQUIRE(rig != nullptr);
    REQUIRE(ui_atlas_world_open(&rig->ui, nullptr, GOLD_SEED, nullptr, w->climate));
    ui_atlas_t *const atlas = ui_atlas(&rig->ui);
    uint32_t *const canvas = test_alloc(t, GOLD_WIDTH * GOLD_HEIGHT * 4u);
    int64_t ns = 1000000000;
    ui_atlas_world_t world = world_now(ns);
    input_frame_t frame = frame_pressing(KEY_M);
    REQUIRE(ui_atlas_input(atlas, &rig->ui.widget, &frame, GOLD_WIDTH, GOLD_HEIGHT, &world, false));
    frame = frame_pressing(KEY_2); /* temperature on the day: a normals layer */
    REQUIRE(ui_atlas_input(atlas, &rig->ui.widget, &frame, GOLD_WIDTH, GOLD_HEIGHT, &world, false));
    const int64_t frame_ns = 16666667;
    /* 10 s held still: one picture. */
    uint64_t before = atlas->pictures_drawn;
    for (uint32_t f = 0u; f < 600u; f++, ns += frame_ns) {
        world = world_now(ns);
        screen_frame(t, rig, canvas, &world);
    }
    CHECK_EQ_U64(atlas->pictures_drawn - before, 1u);
    /* A day later: one more. */
    before = atlas->pictures_drawn;
    frame = frame_pressing(KEY_RIGHTBRACE);
    CHECK(ui_atlas_input(atlas, &rig->ui.widget, &frame, GOLD_WIDTH, GOLD_HEIGHT, &world, false));
    for (uint32_t f = 0u; f < 60u; f++, ns += frame_ns) {
        world = world_now(ns);
        screen_frame(t, rig, canvas, &world);
    }
    CHECK_EQ_U64(atlas->pictures_drawn - before, 1u);
    /* A now layer over 10 s: twenty, give or take one. */
    frame = frame_pressing(KEY_7); /* systems */
    CHECK(ui_atlas_input(atlas, &rig->ui.widget, &frame, GOLD_WIDTH, GOLD_HEIGHT, &world, false));
    before = atlas->pictures_drawn;
    for (uint32_t f = 0u; f < 600u; f++, ns += frame_ns) {
        world = world_now(ns);
        screen_frame(t, rig, canvas, &world);
    }
    const uint64_t now_pictures = atlas->pictures_drawn - before;
    printf("now layer: %" PRIu64 " pictures in 10 s\n", now_pictures);
    CHECK(now_pictures >= 19u && now_pictures <= 21u);
    /* Columns streaming in at 100 a second on a fixed layer: at most 20 pictures in 10 s. */
    frame = frame_pressing(KEY_1);
    CHECK(ui_atlas_input(atlas, &rig->ui.widget, &frame, GOLD_WIDTH, GOLD_HEIGHT, &world, false));
    uint32_t column[UI_ATLAS_COLUMN_PIXELS];
    uint64_t state = 4u;
    column_random(&state, column);
    before = atlas->pictures_drawn;
    for (uint32_t f = 0u; f < 600u; f++, ns += frame_ns) {
        if (f % 6u == 0u) {
            for (uint32_t c = 0u; c < 10u; c++) {
                ui_atlas_column_write(atlas, (int32_t)(f + c) % 40 + (int32_t)(GOLD_X / 16.0f64) - 20,
                                      (int32_t)(GOLD_Y / 16.0f64), column);
            }
        }
        world = world_now(ns);
        screen_frame(t, rig, canvas, &world);
    }
    const uint64_t streamed = atlas->pictures_drawn - before;
    printf("streaming: %" PRIu64 " pictures in 10 s\n", streamed);
    CHECK(streamed >= 2u && streamed <= 21u);
    ui_atlas_world_close(&rig->ui);
    rig_stop(rig);
    world_stop(w);
}

TEST(keys_do_what_the_table_says) {
    rig_t *const rig = rig_start(t);
    REQUIRE(rig != nullptr);
    REQUIRE(ui_atlas_world_open(&rig->ui, nullptr, 1u, nullptr, nullptr));
    ui_atlas_t *const atlas = ui_atlas(&rig->ui);
    ui_widget_t *const widget = &rig->ui.widget;
    ui_atlas_world_t world = world_now(0);
    const uint32_t W = GOLD_WIDTH, H = GOLD_HEIGHT;
    input_frame_t frame = frame_pressing(KEY_M);
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    CHECK(ui_atlas_up(atlas));
    CHECK_EQ_I64(atlas->wanted.zoom, UI_ATLAS_ZOOM_DEFAULT);
    CHECK_EQ_U64(atlas->wanted.layer, 0u);
    CHECK_EQ_U64(atlas->wanted.day, 196u);
    frame = frame_pressing(KEY_M);
    CHECK(!ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    CHECK(!ui_atlas_up(atlas));
    frame = frame_pressing(KEY_M);
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    frame = frame_pressing(KEY_ESC);
    CHECK(!ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    CHECK(!ui_atlas_up(atlas));
    frame = frame_pressing(KEY_M);
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    /* A first frame gives the pointer its canvas. Then the pointer at (200, 50); a wheel detent toward the screen zooms
     * in by sqrt 2 and keeps the block under it. */
    uint32_t *const canvas = test_alloc(t, W * H * 4u);
    REQUIRE(ui_frame_begin(&rig->ui, (uint8_t *)canvas, W * 4u, W, H));
    (void)ui_frame_end(&rig->ui, false);
    frame = frame_pressing(0u);
    frame.mouse_dx = 200 - widget->input.pointer_x;
    frame.mouse_dy = 50 - widget->input.pointer_y;
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    REQUIRE(widget->input.pointer_x == 200 && widget->input.pointer_y == 50);
    ui_atlas_view_t view = atlas->wanted;
    view.width = W;
    view.height = H;
    const float64_t s0 = ui_atlas_blocks_per_pixel(view.zoom);
    const float64_t bx = view.center[0] + (200.5f64 - W / 2.0f64) * s0,
                    by = view.center[1] - (50.5f64 - H / 2.0f64) * s0;
    frame = frame_pressing(0u);
    frame.wheel = 1;
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    CHECK_EQ_I64(atlas->wanted.zoom, UI_ATLAS_ZOOM_DEFAULT - 1);
    const float64_t s1 = ui_atlas_blocks_per_pixel(atlas->wanted.zoom);
    CHECK_NEAR(s0 / s1, 1.41421356f64, 1.0e-6f64);
    const float64_t ax = atlas->wanted.center[0] + (200.5f64 - W / 2.0f64) * s1;
    const float64_t ay = atlas->wanted.center[1] - (50.5f64 - H / 2.0f64) * s1;
    CHECK_NEAR(ax, bx, 0.5f64 * s1);
    CHECK_NEAR(ay, by, 0.5f64 * s1);
    /* A drag of (100, -40) at 4 blocks a pixel moves the centre by (-400, -160). */
    atlas->wanted.zoom = 4;
    const float64_t start[2] = {atlas->wanted.center[0], atlas->wanted.center[1]};
    frame = frame_pressing(0u);
    key_set(&frame.pressed, BTN_LEFT);
    key_set(&frame.down, BTN_LEFT);
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    REQUIRE(ui_frame_begin(&rig->ui, (uint8_t *)canvas, W * 4u, W, H));
    (void)ui_frame_end(&rig->ui, false); /* the press's edge is forgotten, as at a frame's end */
    frame = frame_pressing(0u);
    key_set(&frame.down, BTN_LEFT);
    frame.mouse_dx = 100;
    frame.mouse_dy = -40;
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    CHECK_NEAR(atlas->wanted.center[0] - start[0], -400.0f64, 1.0e-9f64);
    CHECK_NEAR(atlas->wanted.center[1] - start[1], -160.0f64, 1.0e-9f64);
    frame = frame_pressing(0u);
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    CHECK(atlas->dragging == 0u);
    /* 1 to 9: a group's first layer, again for the next, round. */
    frame = frame_pressing(KEY_2);
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    CHECK_EQ_U64(atlas->wanted.layer, ui_atlas_layer_find("temperature_day"));
    for (uint32_t k = 0u; k < 4u; k++) {
        frame = frame_pressing(KEY_2);
        CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    }
    CHECK_EQ_U64(atlas->wanted.layer, ui_atlas_layer_find("temperature_now"));
    frame = frame_pressing(KEY_2);
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    CHECK_EQ_U64(atlas->wanted.layer, ui_atlas_layer_find("temperature_day"));
    frame = frame_pressing(KEY_9);
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    CHECK_EQ_U64(atlas->wanted.layer, ui_atlas_layer_find("snow"));
    /* [ and ] on a normals layer, round, and with Shift by 30. */
    frame = frame_pressing(KEY_2);
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    atlas->wanted.day = 364u;
    frame = frame_pressing(KEY_RIGHTBRACE);
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    CHECK_EQ_U64(atlas->wanted.day, 0u);
    frame = frame_pressing(KEY_LEFTBRACE);
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    CHECK_EQ_U64(atlas->wanted.day, 364u);
    frame = frame_pressing(KEY_RIGHTBRACE);
    key_set(&frame.down, KEY_LEFTSHIFT);
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    CHECK_EQ_U64(atlas->wanted.day, 29u);
    /* P centres on the player. */
    frame = frame_pressing(KEY_P);
    CHECK(ui_atlas_input(atlas, widget, &frame, W, H, &world, false));
    CHECK_NEAR(atlas->wanted.center[0], GOLD_X, 1.0e-9f64);
    CHECK_NEAR(atlas->wanted.center[1], GOLD_Y, 1.0e-9f64);
    ui_atlas_world_close(&rig->ui);
    rig_stop(rig);
}

TEST(m_does_nothing_with_a_screen_up_and_e_does_nothing_with_the_atlas_up) {
    rig_t *const rig = rig_start(t);
    REQUIRE(rig != nullptr);
    REQUIRE(ui_atlas_world_open(&rig->ui, nullptr, 1u, nullptr, nullptr));
    ui_atlas_t *const atlas = ui_atlas(&rig->ui);
    ui_atlas_world_t world = world_now(0);
    input_frame_t frame = frame_pressing(KEY_M);
    CHECK(!ui_atlas_input(atlas, &rig->ui.widget, &frame, GOLD_WIDTH, GOLD_HEIGHT, &world, true));
    CHECK(!ui_atlas_up(atlas));
    CHECK(ui_atlas_input(atlas, &rig->ui.widget, &frame, GOLD_WIDTH, GOLD_HEIGHT, &world, false));
    /* E with the Atlas up: the Atlas keeps the input (game_screen_input then gives it to nothing else) and stays up. */
    frame = frame_pressing(KEY_E);
    CHECK(ui_atlas_input(atlas, &rig->ui.widget, &frame, GOLD_WIDTH, GOLD_HEIGHT, &world, false));
    CHECK(ui_atlas_up(atlas));
    ui_atlas_hide(atlas);
    CHECK(!ui_atlas_up(atlas));
    ui_atlas_world_close(&rig->ui);
    rig_stop(rig);
}

TEST(the_reading_is_what_the_climate_functions_say) {
    gold_t *const w = world_start(t, 16u);
    REQUIRE(w != nullptr);
    rig_t *const rig = rig_start(t);
    REQUIRE(rig != nullptr);
    REQUIRE(ui_atlas_world_open(&rig->ui, nullptr, GOLD_SEED, nullptr, w->climate));
    ui_atlas_t *const atlas = ui_atlas(&rig->ui);
    /* Generated: a column of height 72 under each place read. */
    uint32_t column[UI_ATLAS_COLUMN_PIXELS];
    for (uint32_t k = 0u; k < UI_ATLAS_COLUMN_PIXELS; k++) {
        column[k] = ui_atlas_pixel_pack(0x07E0u, 72);
    }
    uint64_t state = 31u;
    char text[UI_ATLAS_READOUT_BYTES];
    const ui_atlas_world_t world = {.player = {GOLD_X, GOLD_Y},
                                    .t = GOLD_T,
                                    .world_ns = 0,
                                    .census_week = 28u,
                                    .yaw = 0.0f32,
                                    .day_of_year = 196u,
                                    ._pad0 = {0}};
    const uint32_t now_layer = ui_atlas_layer_find("temperature_now");
    REQUIRE(now_layer < UI_ATLAS_LAYER_COUNT);
    arena_t *const weather = scratch_arena(t, 8ull << 20);
    for (uint32_t n = 0u; n < 20u; n++) {
        const int64_t x = (int64_t)GOLD_X + (int64_t)(rng_next(&state) % 3000u) - 1500;
        const int64_t y = (int64_t)GOLD_Y + (int64_t)(rng_next(&state) % 3000u) - 1500;
        ui_atlas_column_write(atlas, (int32_t)floor_div(x, 16), (int32_t)floor_div(y, 16), column);
        const uint16_t day = (uint16_t)(rng_next(&state) % 365u);
        ui_atlas_readout(atlas, x, y, 2u, day, &world, text, sizeof(text));
        climate_normals_at_t normals;
        climate_air_t air;
        REQUIRE(climate_normals_at(w->climate, (float64_t)x + 0.5f64, (float64_t)y + 0.5f64, nanf(""), (float32_t)day,
                                   &normals));
        REQUIRE(climate_air_at(w->climate, (float64_t)x + 0.5f64, (float64_t)y + 0.5f64, (float32_t)day, &air));
        const float64_t lat = climate_latitude_deg((float64_t)y + 0.5f64);
        char want[96];
        snprintf(want, sizeof(want), "%.1f\xC2\xB0%s  x %" PRId64 " y %" PRId64 "  z 72", (libc_double_t)fabs(lat),
                 lat >= 0.0f64 ? "N" : "S", x, y);
        CHECK_CONTAINS(text, want);
        snprintf(want, sizeof(want),
                 "%.1f \xC2\xB0"
                 "C  %.0f mm a year  wind %.1f from ",
                 (libc_double_t)normals.t_day_c, (libc_double_t)normals.rain_mm_year,
                 (libc_double_t)sqrtf(air.wind[0] * air.wind[0] + air.wind[1] * air.wind[1]));
        CHECK_CONTAINS(text, want);
        snprintf(want, sizeof(want), "  %.0f hPa\ncurrent ", (libc_double_t)air.pressure_hpa);
        CHECK_CONTAINS(text, want);
        /* On a now layer: climate_weather_at at the explored ground, at the world's t, with the last census's week. */
        ui_atlas_readout(atlas, x, y, now_layer, 196u, &world, text, sizeof(text));
        const climate_week_store_t *const store = climate_week_store(w->climate);
        const climate_week_t *const last =
            store != nullptr ? climate_week_store_cell(store, (int32_t)floor_div(x, CLIMATE_CELL_BLOCKS),
                                                       (int32_t)floor_div(y, CLIMATE_CELL_BLOCKS))
                             : nullptr;
        climate_now_t now;
        arena_reset(weather);
        REQUIRE(climate_weather_at(w->climate, (float64_t)x + 0.5f64, (float64_t)y + 0.5f64, 72.0f64, GOLD_T, last,
                                   weather, &now));
        snprintf(want, sizeof(want),
                 "%.1f \xC2\xB0"
                 "C  %.1f mm an hour  wind %.1f from ",
                 (libc_double_t)now.temperature_c, (libc_double_t)now.rain_mm_h,
                 (libc_double_t)sqrtf(now.wind[0] * now.wind[0] + now.wind[1] * now.wind[1]));
        CHECK_CONTAINS(text, want);
        snprintf(want, sizeof(want), "  %.0f hPa\ncurrent ", (libc_double_t)now.pressure_hpa);
        CHECK_CONTAINS(text, want);
    }
    arena_deinit(weather);
    ui_atlas_readout(atlas, 900000, 900000, 0u, 10u, &world, text, sizeof(text));
    CHECK_CONTAINS(text, "not explored");
    ui_atlas_world_close(&rig->ui);
    rig_stop(rig);
    world_stop(w);
}

TEST(no_heap_after_the_world_opens) {
    gold_t *const w = world_start(t, 16u);
    REQUIRE(w != nullptr);
    rig_t *const rig = rig_start(t);
    REQUIRE(rig != nullptr);
    REQUIRE(ui_atlas_world_open(&rig->ui, nullptr, GOLD_SEED, nullptr, w->climate));
    ui_atlas_t *const atlas = ui_atlas(&rig->ui);
    uint32_t *const canvas = test_alloc(t, GOLD_WIDTH * GOLD_HEIGHT * 4u);
    heap_counters_t before, after;
    heap_counters_read(&before);
    int64_t ns = 0;
    ui_atlas_world_t world = world_now(ns);
    input_frame_t frame = frame_pressing(KEY_M);
    CHECK(ui_atlas_input(atlas, &rig->ui.widget, &frame, GOLD_WIDTH, GOLD_HEIGHT, &world, false));
    uint32_t column[UI_ATLAS_COLUMN_PIXELS];
    uint64_t state = 6u;
    column_random(&state, column);
    static const uint32_t KEYS[8] = {KEY_2, KEY_RIGHTBRACE, KEY_3, KEY_6, KEY_7, KEY_1, KEY_9, KEY_8};
    for (uint32_t f = 0u; f < 200u; f++, ns += 16666667) {
        world = world_now(ns);
        frame = frame_pressing(f % 25u == 0u ? KEYS[(f / 25u) % 8u] : 0u);
        frame.wheel = f % 50u == 10u ? 1 : (f % 50u == 30u ? -1 : 0);
        frame.mouse_dx = f % 7u == 0u ? 3 : 0;
        CHECK(ui_atlas_input(atlas, &rig->ui.widget, &frame, GOLD_WIDTH, GOLD_HEIGHT, &world, false));
        for (uint32_t c = 0u; c < 5u; c++) {
            ui_atlas_column_write(atlas, (int32_t)(f * 5u + c) % 200 - 100, (int32_t)(f % 50u), column);
        }
        screen_frame(t, rig, canvas, &world);
    }
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    ui_atlas_world_close(&rig->ui);
    rig_stop(rig);
    world_stop(w);
}

TEST_MAIN()
