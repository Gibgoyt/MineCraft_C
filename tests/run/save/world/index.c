/* ctest run_save_world. See README.md. */
#include "harness/index.h"

#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "log/index.h"
#include "render/index.h"
#include "save/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/menu/catalog/index.h"
#include "world/index.h"

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum : uint64_t {
    SW_COLUMN_VOXELS = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS,
    SW_COLUMN_BYTES = SW_COLUMN_VOXELS * sizeof(uint16_t),
    SW_PATH_BYTES = 512u,
    SW_FILE_BYTES = 8ull << 20,
};
enum : uint32_t {
    SW_RADIUS = 2u,
    SW_SIDE = 2u * SW_RADIUS + 1u,
    SW_COLUMNS = SW_SIDE * SW_SIDE,
    SW_KILL_ROUNDS = 24u,
    SW_THREAD_READERS = 3u,
    SW_THREAD_COLUMNS = 25u,
    SW_THREAD_PASSES = 40u,
};
/* Chunks either side of a line between regions, and negative: both halves of what a shift and a mask have to get right. */
enum : int32_t {
    SW_CENTER_X = -32,
    SW_CENTER_Y = 32,
};
static const uint64_t SW_SEED = 7777u;

/* What a world in memory and on disk come to, in a test. */
typedef struct sw_rig_t {
    world_t *world;     /* 0 */
    save_t *save;       /* 8 */
    uint16_t *states;   /* 16: one column */
    uint8_t *scratch;   /* 24 */
    uint16_t *model;    /* 32: SW_COLUMNS columns, by brute force */
    const char *root;   /* 40 */
    const char *blocks; /* 48 */
    uint64_t seed;      /* 56 */
    int32_t center_x;   /* 64 */
    int32_t center_y;   /* 68 */
    uint32_t radius;    /* 72: of the square the model covers */
    uint32_t side;      /* 76 */
    uint32_t table;     /* 80: of the map's circle, which holds all of that square */
    uint8_t _pad0[4];   /* 84 */
} sw_rig_t;
static_assert(sizeof(sw_rig_t) == 88);

static uint64_t sw_next(uint64_t *state) {
    uint64_t value = *state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

/* A separate FNV-1a, so that a change to the one under test is not also made here. */
static uint64_t sw_fnv(const void *data, uint64_t bytes) {
    const uint8_t *const cursor = data;
    uint64_t hash = 14695981039346656037ull;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash ^= cursor[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static void *sw_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static char *sw_join(test_context_t *t, const char *left, const char *right) {
    char *const path = test_alloc(t, SW_PATH_BYTES);
    snprintf(path, SW_PATH_BYTES, "%s/%s", left, right);
    return path;
}

static char *sw_temp_directory(test_context_t *t) {
    char *const directory = test_alloc(t, SW_PATH_BYTES);
    snprintf(directory, SW_PATH_BYTES, "/tmp/mc_save_world_XXXXXX");
    return mkdtemp(directory);
}

static void sw_temp_remove(test_context_t *t, const char *directory) {
    if (directory == nullptr || strncmp(directory, "/tmp/mc_save_world_", 19u) != 0) {
        return;
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", (char *)directory, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static uint8_t *sw_slurp(test_context_t *t, const char *path, uint64_t *out_bytes) {
    uint8_t *const bytes = test_alloc(t, SW_FILE_BYTES);
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, SW_FILE_BYTES, out_bytes), FILE_OK)) {
        *out_bytes = 0u;
    }
    return bytes;
}

/* Errors a test sets out to cause, off the screen. Returns what to give back. */
static int32_t sw_quiet_begin(void) {
    return log_set_fd(open("/dev/null", O_WRONLY | O_CLOEXEC));
}
static void sw_quiet_end(int32_t previous) {
    const int32_t quiet = log_set_fd(previous);
    (void)close(quiet);
}

/* ---- A column as generated, without the cost of real terrain, and a world around it ---------------------------------- */

static void sw_generate(int32_t chunk_x, int32_t chunk_y, uint16_t *out) {
    for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
        uint16_t *const states = out + (uint64_t)section * SECTION_VOXELS;
        if (section > 20u) {
            memset(states, 0, SECTION_VOXELS * sizeof(uint16_t));
            continue;
        }
        const uint64_t base = (uint64_t)(uint32_t)chunk_x * 31u + (uint64_t)(uint32_t)chunk_y * 17u + section;
        for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
            const uint32_t pick = section > 10u ? (i >> 8) % 3u : 0u;
            states[i] = (uint16_t)(1u + (base + pick) % (BLOCK_COUNT - 1u));
        }
    }
}

static uint32_t sw_column_index(const sw_rig_t *rig, int32_t chunk_x, int32_t chunk_y) {
    const uint32_t dx = (uint32_t)(chunk_x - (rig->center_x - (int32_t)rig->radius));
    const uint32_t dy = (uint32_t)(chunk_y - (rig->center_y - (int32_t)rig->radius));
    return dy * rig->side + dx;
}

static uint16_t *sw_model_column(const sw_rig_t *rig, int32_t chunk_x, int32_t chunk_y) {
    return rig->model + (uint64_t)sw_column_index(rig, chunk_x, chunk_y) * SW_COLUMN_VOXELS;
}

/* The model at the start of a world, as if nothing had ever been edited. */
static void sw_model_reset(const sw_rig_t *rig) {
    for (uint32_t i = 0u; i < rig->side * rig->side; i++) {
        const int32_t chunk_x = rig->center_x - (int32_t)rig->radius + (int32_t)(i % rig->side);
        const int32_t chunk_y = rig->center_y - (int32_t)rig->radius + (int32_t)(i / rig->side);
        sw_generate(chunk_x, chunk_y, rig->model + (uint64_t)i * SW_COLUMN_VOXELS);
    }
}

static sw_rig_t *sw_rig_new(test_context_t *t, const char *root, uint32_t radius) {
    sw_rig_t *const rig = test_alloc(t, sizeof(sw_rig_t));
    const uint32_t side = 2u * radius + 1u;
    rig->world = sw_aligned(t, sizeof(world_t));
    rig->save = sw_aligned(t, sizeof(save_t));
    rig->states = test_alloc(t, SW_COLUMN_BYTES);
    rig->scratch = test_alloc(t, SAVE_WORLD_OVERLAY_SCRATCH_BYTES);
    rig->model = test_alloc(t, (uint64_t)side * side * SW_COLUMN_BYTES);
    rig->root = root;
    rig->blocks = sw_join(t, root, "blocks");
    rig->seed = SW_SEED;
    rig->center_x = SW_CENTER_X;
    rig->center_y = SW_CENTER_Y;
    rig->radius = radius;
    rig->side = side;
    rig->table = (radius * 3u + 1u) / 2u;
    sw_model_reset(rig);
    return rig;
}

/* Every column of the window in the map: as generated, with what is saved over it. */
static void sw_stream_all(test_context_t *t, sw_rig_t *rig) {
    world_t *const world = rig->world;
    world_map_request_t request;
    while (world_map_stream_take(&world->map, &request, 1u) == 1u) {
        sw_generate(request.chunk_x, request.chunk_y, rig->states);
        (void)save_world_column_overlay(&rig->save->world, request.chunk_x, request.chunk_y, rig->states, rig->scratch);
        column_t *const column = world_map_stream_commit(&world->map, &request);
        if (!CHECK(column != nullptr)) {
            return;
        }
        for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
            if (!CHECK(section_blocks_import(&column->sections[section], &world->section_pool,
                                             rig->states + (uint64_t)section * SECTION_VOXELS))) {
                return;
            }
        }
        world_edit_column_filled(&world->edit, request.slot);
    }
}

/* A session: both opened, and everything in. */
static bool sw_session_begin(test_context_t *t, sw_rig_t *rig) {
    const uint64_t table_side = 2ull * rig->table + 1ull;
    const world_limits_t limits = {.section_bytes = 1ull << 30, .column_count = table_side * table_side};
    if (!CHECK(save_init(rig->save)) || !CHECK(world_init(rig->world, limits))) {
        return false;
    }
    if (!CHECK_EQ_U64(save_open(rig->save, rig->root, rig->seed), SAVE_WORLD_OK) ||
        !CHECK(world_open(rig->world, rig->table, rig->center_x, rig->center_y))) {
        return false;
    }
    sw_stream_all(t, rig);
    return true;
}

static void sw_session_end(sw_rig_t *rig) {
    world_close(rig->world);
    world_deinit(rig->world);
    save_deinit(rig->save);
}

static void sw_voxel_of(const sw_rig_t *rig, uint64_t *rng, int32_t *x, int32_t *y, int32_t *z) {
    const int32_t span = (int32_t)(rig->side * COLUMN_SIZE);
    *x = (rig->center_x - (int32_t)rig->radius) * (int32_t)COLUMN_SIZE + (int32_t)(sw_next(rng) % (uint64_t)span);
    *y = (rig->center_y - (int32_t)rig->radius) * (int32_t)COLUMN_SIZE + (int32_t)(sw_next(rng) % (uint64_t)span);
    const uint64_t height = (uint64_t)(COLUMN_Z_GAMEPLAY_MAX - COLUMN_Z_GAMEPLAY_MIN + 1);
    *z = COLUMN_Z_GAMEPLAY_MIN + (int32_t)(sw_next(rng) % height);
}

/* A voxel of the model, as it would be after this edit. */
static uint16_t *sw_model_voxel(const sw_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    const uint32_t section = column_section_of(z);
    const uint32_t index = section_voxel_index(column_local_of(x), column_local_of(y), column_section_local_of(z));
    return sw_model_column(rig, column_chunk_of(x), column_chunk_of(y)) + (uint64_t)section * SECTION_VOXELS + index;
}

/* count blocks, at random, in the world and the model. */
static void sw_edit_random(test_context_t *t, const sw_rig_t *rig, uint64_t rng_start, uint32_t count) {
    uint64_t rng = rng_start;
    for (uint32_t i = 0u; i < count; i++) {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        sw_voxel_of(rig, &rng, &x, &y, &z);
        const uint16_t state = (uint16_t)(sw_next(&rng) % BLOCK_COUNT);
        const world_edit_status_t status = world_edit_block_set(&rig->world->edit, x, y, z, state);
        if (!CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED)) {
            return;
        }
        *sw_model_voxel(rig, x, y, z) = state;
    }
}

/* The same edits, in the model only. */
static void sw_edit_model_only(const sw_rig_t *rig, uint64_t rng_start, uint32_t count) {
    uint64_t rng = rng_start;
    for (uint32_t i = 0u; i < count; i++) {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        sw_voxel_of(rig, &rng, &x, &y, &z);
        *sw_model_voxel(rig, x, y, z) = (uint16_t)(sw_next(&rng) % BLOCK_COUNT);
    }
}

/* Every voxel of the world in memory against the model. */
static void sw_world_check(test_context_t *t, const sw_rig_t *rig) {
    uint16_t *const states = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    for (uint32_t i = 0u; i < rig->side * rig->side; i++) {
        const int32_t chunk_x = rig->center_x - (int32_t)rig->radius + (int32_t)(i % rig->side);
        const int32_t chunk_y = rig->center_y - (int32_t)rig->radius + (int32_t)(i / rig->side);
        const column_t *const column = world_map_column_at(&rig->world->map, chunk_x, chunk_y);
        if (!CHECK(column != nullptr)) {
            return;
        }
        for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
            section_blocks_export(&column->sections[section], states);
            if (!CHECK_MEM_EQ(states, sw_model_column(rig, chunk_x, chunk_y) + (uint64_t)section * SECTION_VOXELS,
                              SECTION_VOXELS * sizeof(uint16_t))) {
                printf("  at column %" PRId32 ", %" PRId32 " section %" PRIu32 "\n", chunk_x, chunk_y, section);
                return;
            }
        }
    }
}

/* How many in the directory have this prefix and suffix. */
static uint32_t sw_directory_count(const char *directory, const char *prefix, const char *suffix) {
    DIR *const handle = opendir(directory);
    if (handle == nullptr) {
        return 0u;
    }
    uint32_t count = 0u;
    for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
        const uint64_t bytes = strlen(entry->d_name);
        const uint64_t tail = strlen(suffix);
        if (strncmp(entry->d_name, prefix, strlen(prefix)) == 0 && bytes >= tail &&
            strcmp(entry->d_name + bytes - tail, suffix) == 0) {
            count++;
        }
    }
    (void)closedir(handle);
    return count;
}

static void sw_manifest_read(test_context_t *t, const sw_rig_t *rig, save_world_manifest_header_t *out,
                             const save_world_region_t **out_regions) {
    uint64_t bytes = 0u;
    const uint8_t *const file = sw_slurp(t, sw_join(t, rig->blocks, "manifest"), &bytes);
    memset(out, 0, sizeof(*out));
    if (CHECK(bytes >= sizeof(*out))) {
        memcpy(out, file, sizeof(*out));
        if (out_regions != nullptr) {
            *out_regions = (const save_world_region_t *)(const void *)(file + sizeof(*out));
        }
    }
}

/* ---- Records --------------------------------------------------------------------------------------------------------- */

TEST(a_record_of_each_form_by_hand) {
    uint16_t *const palette_slots = test_alloc(t, 65536u * sizeof(uint16_t));
    uint16_t *const states = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint16_t *const back = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const record = test_alloc(t, SAVE_WORLD_RECORD_BYTES_MAX);
    static const uint16_t head_uniform[3] = {0u, 1u, 5u};
    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
        states[i] = 5u;
    }
    CHECK_EQ_U64(save_world_record_encode(states, palette_slots, record), 6u);
    CHECK_MEM_EQ(record, head_uniform, sizeof(head_uniform));
    REQUIRE(save_world_record_decode(record, 6u, back));
    CHECK_MEM_EQ(back, states, SECTION_VOXELS * sizeof(uint16_t));

    /* Two states, turn about: 7 is the first seen, so it is index 0, which puts the 1s in the odd bits. */
    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
        states[i] = (i & 1u) != 0u ? 9u : 7u;
    }
    CHECK_EQ_U64(save_world_record_encode(states, palette_slots, record), 4u + 4u + 512u);
    static const uint16_t head_two[4] = {1u, 2u, 7u, 9u};
    CHECK_MEM_EQ(record, head_two, sizeof(head_two));
    CHECK_EQ_U64(record[8], 0xAAu);
    CHECK_EQ_U64(record[8u + 511u], 0xAAu);
    REQUIRE(save_world_record_decode(record, 520u, back));
    CHECK_MEM_EQ(back, states, SECTION_VOXELS * sizeof(uint16_t));

    /* Then the fewest bits that hold each count, and its size. */
    static const uint32_t counts[] = {3u, 4u, 5u, 16u, 17u, 100u};
    static const uint32_t bits[] = {2u, 2u, 4u, 4u, 8u, 8u};
    for (uint32_t c = 0u; c < 6u; c++) {
        for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
            states[i] = (uint16_t)(i % counts[c]);
        }
        const uint32_t bytes = 4u + 2u * counts[c] + SECTION_VOXELS * bits[c] / 8u;
        CHECK_EQ_U64(save_world_record_encode(states, palette_slots, record), bytes);
        uint16_t head[2];
        memcpy(head, record, sizeof(head));
        CHECK_EQ_U64(head[0], bits[c]);
        CHECK_EQ_U64(head[1], counts[c]);
        REQUIRE(save_world_record_decode(record, bytes, back));
        CHECK_MEM_EQ(back, states, SECTION_VOXELS * sizeof(uint16_t));
    }

    /* More than a palette holds: the states as they are. Past the block table, so no reader would have them. */
    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
        states[i] = (uint16_t)(i % 300u);
    }
    CHECK_EQ_U64(save_world_record_encode(states, palette_slots, record), SAVE_WORLD_RECORD_BYTES_MAX);
    CHECK_MEM_EQ(record + 4u, states, SECTION_VOXELS * sizeof(uint16_t));
    CHECK(!save_world_record_decode(record, SAVE_WORLD_RECORD_BYTES_MAX, back));
    /* And with the table's states only, as a direct section could hold them, it reads back. */
    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
        states[i] = (uint16_t)(i % BLOCK_COUNT);
    }
    const uint16_t head_direct[2] = {16u, 0u};
    memcpy(record, head_direct, sizeof(head_direct));
    memcpy(record + 4u, states, SECTION_VOXELS * sizeof(uint16_t));
    REQUIRE(save_world_record_decode(record, SAVE_WORLD_RECORD_BYTES_MAX, back));
    CHECK_MEM_EQ(back, states, SECTION_VOXELS * sizeof(uint16_t));

    /* It left its working memory as it found it. */
    for (uint32_t i = 0u; i < 65536u; i++) {
        if (palette_slots[i] != 0u) {
            CHECK_EQ_U64(palette_slots[i], 0u);
            break;
        }
    }
}

TEST(a_bad_record_is_refused_and_writes_nothing) {
    uint16_t *const palette_slots = test_alloc(t, 65536u * sizeof(uint16_t));
    uint16_t *const states = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint16_t *const out = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const record = test_alloc(t, SAVE_WORLD_RECORD_BYTES_MAX);
    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
        states[i] = (uint16_t)(i % 3u); /* a palette of 3, with room for a fourth that no index may point to */
    }
    const uint32_t bytes = save_world_record_encode(states, palette_slots, record);
    memset(out, 0xEE, SECTION_VOXELS * sizeof(uint16_t));

    CHECK(!save_world_record_decode(record, 0u, out));
    CHECK(!save_world_record_decode(record, 3u, out));
    CHECK(!save_world_record_decode(record, bytes - 1u, out));
    CHECK(!save_world_record_decode(record, SAVE_WORLD_RECORD_BYTES_MAX + 1ull, out));
    uint16_t head[2];
    const uint16_t bad_heads[][2] = {{3u, 3u}, {16u, 3u}, {2u, 5u}, {2u, 0u}, {0u, 2u}, {9u, 3u}, {65535u, 3u}};
    for (uint32_t i = 0u; i < sizeof(bad_heads) / sizeof(bad_heads[0]); i++) {
        memcpy(head, bad_heads[i], sizeof(head));
        uint8_t *const copy = test_alloc(t, bytes);
        memcpy(copy, record, bytes);
        memcpy(copy, head, sizeof(head));
        CHECK(!save_world_record_decode(copy, bytes, out));
    }
    /* A state past the table, and an index past the palette. */
    uint8_t *const copy = test_alloc(t, bytes);
    memcpy(copy, record, bytes);
    const uint16_t past_table = BLOCK_COUNT;
    memcpy(copy + 4u, &past_table, sizeof(past_table));
    CHECK(!save_world_record_decode(copy, bytes, out));
    memcpy(copy, record, bytes);
    copy[bytes - 1u] = 0xFFu;
    CHECK(!save_world_record_decode(copy, bytes, out));
    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
        if (out[i] != 0xEEEEu) {
            CHECK_EQ_U64(out[i], 0xEEEEu);
            break;
        }
    }
    /* Any bytes at all: it says no, or gives only states that exist. */
    uint64_t rng = 0x1234567u;
    for (uint32_t round = 0u; round < 20000u; round++) {
        uint8_t noise[64];
        for (uint32_t i = 0u; i < sizeof(noise); i++) {
            noise[i] = (uint8_t)sw_next(&rng);
        }
        noise[0] = (uint8_t)(1u << (sw_next(&rng) % 4u));
        noise[1] = 0u;
        noise[3] = 0u;
        memcpy(copy, record, bytes);
        memcpy(copy, noise, sizeof(noise));
        if (save_world_record_decode(copy, bytes, out)) {
            for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
                if (out[i] >= BLOCK_COUNT) {
                    CHECK(out[i] < BLOCK_COUNT);
                    return;
                }
            }
        }
    }
}

/* ---- The files ------------------------------------------------------------------------------------------------------- */

TEST(a_new_world_is_one_manifest_and_its_bytes_are_known) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    save_t *const save = sw_aligned(t, sizeof(save_t));
    REQUIRE(save_init(save));
    REQUIRE(save_open(save, root, 0x1122334455667788ull) == SAVE_WORLD_OK);
    save_close(save);
    char *const blocks = sw_join(t, root, "blocks");
    uint64_t bytes = 0u;
    const uint8_t *const file = sw_slurp(t, sw_join(t, blocks, "manifest"), &bytes);
    REQUIRE(bytes == 64u);
    CHECK_MEM_EQ(file, "MCWORLDM", 8u);
    static const uint8_t seed[8] = {0x88u, 0x77u, 0x66u, 0x55u, 0x44u, 0x33u, 0x22u, 0x11u};
    CHECK_MEM_EQ(file + 8u, seed, 8u);
    uint64_t value = 1u;
    memcpy(&value, file + 16u, 8u);
    CHECK_EQ_U64(value, 0u); /* no saves yet */
    memcpy(&value, file + 24u, 8u);
    uint64_t table = 14695981039346656037ull;
    for (uint32_t i = 0u; i < BLOCK_COUNT; i++) {
        const char *const name = block_defs[i].name;
        for (uint64_t c = 0u; c <= strlen(name); c++) {
            table ^= (uint8_t)name[c];
            table *= 1099511628211ull;
        }
    }
    CHECK_EQ_U64(value, table);
    memcpy(&value, file + 32u, 8u);
    CHECK_EQ_U64(value, 14695981039346656037ull); /* the checksum of nothing */
    uint32_t word = 0u;
    memcpy(&word, file + 40u, 4u);
    CHECK_EQ_U64(word, SAVE_WORLD_VERSION);
    memcpy(&word, file + 44u, 4u);
    CHECK_EQ_U64(word, 64u);
    memcpy(&word, file + 48u, 4u);
    CHECK_EQ_U64(word, 0u);
    memcpy(&word, file + 52u, 4u);
    CHECK_EQ_U64(word, TERRAIN_GENERATOR_VERSION);
    memcpy(&value, file + 56u, 8u);
    CHECK_EQ_U64(value, sw_fnv(file, 56u));
    CHECK_EQ_U64(sw_directory_count(blocks, "r.", ""), 0u);
    /* Nothing changed, so opening it again changes nothing. */
    REQUIRE(save_open(save, root, 0x1122334455667788ull) == SAVE_WORLD_OK);
    save_close(save);
    uint64_t again = 0u;
    const uint8_t *const second = sw_slurp(t, sw_join(t, blocks, "manifest"), &again);
    REQUIRE(again == bytes);
    CHECK_MEM_EQ(second, file, bytes);
    save_deinit(save);
    sw_temp_remove(t, root);
}

TEST(a_world_reloads_byte_for_byte_over_many_saves) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_rig_new(t, root, SW_RADIUS);
    uint64_t rng = 0xABCDEF12345ull;
    for (uint32_t session = 0u; session < 4u; session++) {
        REQUIRE(sw_session_begin(t, rig));
        /* What was saved is what has come back, before anything else happens. */
        sw_world_check(t, rig);
        for (uint32_t save = 0u; save < 3u; save++) {
            sw_edit_random(t, rig, sw_next(&rng) | 1u, 200u + session * 300u);
            REQUIRE(save_world_flush(&rig->save->world, &rig->world->edit) == SAVE_WORLD_OK);
            CHECK_EQ_U64(rig->save->world.generation, session * 3u + save + 1u);
        }
        sw_session_end(rig);
    }
    REQUIRE(sw_session_begin(t, rig));
    sw_world_check(t, rig);
    CHECK_EQ_U64(atomic_load(&rig->save->world.sections_damaged), 0u);
    sw_session_end(rig);
    CHECK_EQ_U64(sw_directory_count(rig->blocks, "", ".tmp"), 0u);
    /* The window crosses a line between regions on both axes: -34 to -30 by 30 to 34 is four regions. */
    CHECK_EQ_U64(sw_directory_count(rig->blocks, "r.", ""), 4u);
    sw_temp_remove(t, root);
}

TEST(only_what_changed_is_on_disk) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_rig_new(t, root, SW_RADIUS);
    REQUIRE(sw_session_begin(t, rig));
    /* Nothing edited, nothing to write, no new generation. */
    REQUIRE(save_world_flush(&rig->save->world, &rig->world->edit) == SAVE_WORLD_OK);
    CHECK_EQ_U64(rig->save->world.generation, 0u);
    /* Three sections of one column, then a second save of another region's one, and two edits of the first section again. */
    REQUIRE(world_edit_block_set(&rig->world->edit, -33 * 16, 33 * 16, 0, 3u) == WORLD_EDIT_STATUS_CHANGED);
    *sw_model_voxel(rig, -33 * 16, 33 * 16, 0) = 3u;
    REQUIRE(world_edit_block_set(&rig->world->edit, -33 * 16, 33 * 16, 16, 3u) == WORLD_EDIT_STATUS_CHANGED);
    *sw_model_voxel(rig, -33 * 16, 33 * 16, 16) = 3u;
    REQUIRE(world_edit_block_set(&rig->world->edit, -33 * 16, 33 * 16, -16, 3u) == WORLD_EDIT_STATUS_CHANGED);
    *sw_model_voxel(rig, -33 * 16, 33 * 16, -16) = 3u;
    REQUIRE(save_world_flush(&rig->save->world, &rig->world->edit) == SAVE_WORLD_OK);
    CHECK_EQ_U64(rig->save->world.sections_written, 3u);
    save_world_manifest_header_t header;
    const save_world_region_t *regions = nullptr;
    sw_manifest_read(t, rig, &header, &regions);
    REQUIRE(header.region_count == 1u);
    CHECK_EQ_I64(regions[0].region_x, -2); /* -33 >> 5 */
    CHECK_EQ_I64(regions[0].region_y, 1);  /* 33 >> 5 */
    CHECK_EQ_U64(regions[0].section_count, 3u);
    CHECK_EQ_U64(regions[0].generation, 1u);
    REQUIRE(world_edit_block_set(&rig->world->edit, -31 * 16, 31 * 16, 0, 4u) == WORLD_EDIT_STATUS_CHANGED);
    *sw_model_voxel(rig, -31 * 16, 31 * 16, 0) = 4u;
    REQUIRE(save_world_flush(&rig->save->world, &rig->world->edit) == SAVE_WORLD_OK);
    sw_manifest_read(t, rig, &header, &regions);
    REQUIRE(header.region_count == 2u);
    /* The first file, still from the first save: a save rewrites what it touches and nothing else. */
    CHECK_EQ_I64(regions[0].region_x, -2);
    CHECK_EQ_I64(regions[0].region_y, 1);
    CHECK_EQ_U64(regions[0].generation, 1u);
    CHECK_EQ_I64(regions[1].region_x, -1); /* -31 >> 5 */
    CHECK_EQ_I64(regions[1].region_y, 0);  /* 31 >> 5 */
    CHECK_EQ_U64(regions[1].generation, 2u);
    CHECK(file_path_size(sw_join(t, rig->blocks, "r.-2.1.1"), &(uint64_t){0u}) == FILE_OK);
    /* A section edited again replaces itself: the same region, the old file gone. */
    REQUIRE(world_edit_block_set(&rig->world->edit, -33 * 16, 33 * 16, 0, 5u) == WORLD_EDIT_STATUS_CHANGED);
    *sw_model_voxel(rig, -33 * 16, 33 * 16, 0) = 5u;
    REQUIRE(save_world_flush(&rig->save->world, &rig->world->edit) == SAVE_WORLD_OK);
    sw_manifest_read(t, rig, &header, &regions);
    CHECK_EQ_U64(regions[0].section_count, 3u);
    CHECK_EQ_U64(regions[0].generation, 3u);
    CHECK(file_path_size(sw_join(t, rig->blocks, "r.-2.1.1"), &(uint64_t){0u}) == FILE_ERROR_NOT_FOUND);
    sw_session_end(rig);
    REQUIRE(sw_session_begin(t, rig));
    sw_world_check(t, rig);
    sw_session_end(rig);
    sw_temp_remove(t, root);
}

TEST(the_same_changes_make_the_same_files_in_any_order) {
    char *const first = sw_temp_directory(t);
    char *const second = sw_temp_directory(t);
    REQUIRE(first != nullptr && second != nullptr);
    sw_rig_t *const rig_a = sw_rig_new(t, first, SW_RADIUS);
    sw_rig_t *const rig_b = sw_rig_new(t, second, SW_RADIUS);
    REQUIRE(sw_session_begin(t, rig_a));
    REQUIRE(sw_session_begin(t, rig_b));
    /* One voxel per section, so that order makes no difference to the blocks, only to what is dirty first. */
    enum : uint32_t { COUNT = 200u };
    int32_t *const places = test_alloc(t, COUNT * 3u * sizeof(int32_t));
    uint64_t rng = 0x77123ull;
    for (uint32_t i = 0u; i < COUNT; i++) {
        sw_voxel_of(rig_a, &rng, &places[i * 3u], &places[i * 3u + 1u], &places[i * 3u + 2u]);
    }
    for (uint32_t i = 0u; i < COUNT; i++) {
        const int32_t *const a = &places[i * 3u];
        const int32_t *const b = &places[(COUNT - 1u - i) * 3u];
        REQUIRE(world_edit_block_set(&rig_a->world->edit, a[0], a[1], a[2], 9u) != WORLD_EDIT_STATUS_NOT_LOADED);
        REQUIRE(world_edit_block_set(&rig_b->world->edit, b[0], b[1], b[2], 9u) != WORLD_EDIT_STATUS_NOT_LOADED);
    }
    REQUIRE(save_world_flush(&rig_a->save->world, &rig_a->world->edit) == SAVE_WORLD_OK);
    REQUIRE(save_world_flush(&rig_b->save->world, &rig_b->world->edit) == SAVE_WORLD_OK);
    sw_session_end(rig_a);
    sw_session_end(rig_b);
    DIR *const handle = opendir(rig_a->blocks);
    REQUIRE(handle != nullptr);
    uint32_t compared = 0u;
    for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
        if (entry->d_name[0] == '.' || strcmp(entry->d_name, "lock") == 0) {
            continue;
        }
        uint64_t left_bytes = 0u;
        uint64_t right_bytes = 0u;
        const uint8_t *const left = sw_slurp(t, sw_join(t, rig_a->blocks, entry->d_name), &left_bytes);
        const uint8_t *const right = sw_slurp(t, sw_join(t, rig_b->blocks, entry->d_name), &right_bytes);
        if (CHECK_EQ_U64(left_bytes, right_bytes)) {
            CHECK_MEM_EQ(left, right, left_bytes);
        }
        compared++;
    }
    (void)closedir(handle);
    CHECK_EQ_U64(compared, 5u); /* the manifest and four regions */
    sw_temp_remove(t, first);
    sw_temp_remove(t, second);
}

TEST(what_is_on_its_way_is_read_before_any_file) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_rig_new(t, root, SW_RADIUS);
    REQUIRE(sw_session_begin(t, rig));
    save_world_t *const save = &rig->save->world;
    world_edit_t *const edit = &rig->world->edit;
    const int32_t x = SW_CENTER_X * 16;
    const int32_t y = SW_CENTER_Y * 16;
    const uint32_t section = column_section_of(40);
    const uint32_t index = section_voxel_index(0u, 0u, column_section_local_of(40));
    uint16_t *const states = rig->states;
#define SW_SEEN()                                                                                                      \
    (sw_generate(SW_CENTER_X, SW_CENTER_Y, states),                                                                    \
     save_world_column_overlay(save, SW_CENTER_X, SW_CENTER_Y, states, rig->scratch),                                  \
     states[(uint64_t)section * SECTION_VOXELS + index])

    /* In no set, and no file. */
    const uint16_t generated = SW_SEEN();
    REQUIRE(generated != 11u && generated != 12u && generated != 13u);
    /* Captured. */
    REQUIRE(world_edit_block_set(edit, x, y, 40, 11u) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(save_world_capture_dirty(save, edit) == SAVE_WORLD_OK);
    CHECK_EQ_U64(SW_SEEN(), 11u);
    /* On its way to a file. */
    REQUIRE(save_world_commit_begin(save));
    CHECK_EQ_U64(SW_SEEN(), 11u);
    /* And something newer captured while that is so. */
    REQUIRE(world_edit_block_set(edit, x, y, 40, 12u) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(save_world_capture_dirty(save, edit) == SAVE_WORLD_OK);
    CHECK_EQ_U64(SW_SEEN(), 12u);
    /* The older gets to its file, and the newer still wins. */
    REQUIRE(save_world_commit_write(save) == SAVE_WORLD_OK);
    CHECK_EQ_U64(SW_SEEN(), 12u);
    CHECK_EQ_U64(save->generation, 1u);
    /* A capture of the same section twice in one set: the later wins. */
    REQUIRE(world_edit_block_set(edit, x, y, 40, 13u) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(save_world_capture_dirty(save, edit) == SAVE_WORLD_OK);
    CHECK_EQ_U64(SW_SEEN(), 13u);
    CHECK_EQ_U64(save->pending[save->active].count, 1u);
    /* All of it down. */
    REQUIRE(save_world_commit_begin(save));
    REQUIRE(save_world_commit_write(save) == SAVE_WORLD_OK);
    CHECK(!save_world_commit_begin(save));
    CHECK_EQ_U64(SW_SEEN(), 13u);
    CHECK_EQ_U64(save->generation, 2u);
    /* The mask says what was written over, one section. */
    sw_generate(SW_CENTER_X, SW_CENTER_Y, states);
    CHECK(column_mask_equal(save_world_column_overlay(save, SW_CENTER_X, SW_CENTER_Y, states, rig->scratch),
                            column_mask_of(section)));
    /* And a column nothing happened to in a region with a file is left alone. */
    sw_generate(SW_CENTER_X + 1, SW_CENTER_Y, states);
    CHECK(!column_mask_any(save_world_column_overlay(save, SW_CENTER_X + 1, SW_CENTER_Y, states, rig->scratch)));
#undef SW_SEEN
    sw_session_end(rig);
    sw_temp_remove(t, root);
}

TEST(a_column_on_its_way_out_keeps_its_changes_and_comes_back_with_them) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_rig_new(t, root, SW_RADIUS);
    REQUIRE(sw_session_begin(t, rig));
    world_t *const world = rig->world;
    sw_edit_random(t, rig, 0x51515ull, 400u);
    /* Every column gets captured and unloaded, and is then offered again, as walking away and back does. No save has happened. */
    for (uint32_t slot = 0u; slot < world->map.slot_count; slot++) {
        if (world->map.borrowed_slots[slot].state == WORLD_MAP_STATE_READY) {
            REQUIRE(save_world_capture_column(&rig->save->world, &world->edit, slot) == SAVE_WORLD_OK);
            CHECK(!column_mask_any(world_edit_dirty_peek(&world->edit, slot, WORLD_EDIT_CONSUMER_SAVE)));
            world_map_unload(&world->map, slot);
        }
    }
    CHECK_EQ_U64(rig->save->world.generation, 0u);
    sw_stream_all(t, rig);
    sw_world_check(t, rig);
    /* With the same on disk, and more edits past that. */
    REQUIRE(save_world_flush(&rig->save->world, &world->edit) == SAVE_WORLD_OK);
    CHECK_EQ_U64(rig->save->world.generation, 1u);
    sw_edit_random(t, rig, 0x61616ull, 400u);
    REQUIRE(save_world_flush(&rig->save->world, &world->edit) == SAVE_WORLD_OK);
    sw_session_end(rig);
    REQUIRE(sw_session_begin(t, rig));
    sw_world_check(t, rig);
    sw_session_end(rig);
    sw_temp_remove(t, root);
}

TEST(when_there_is_no_room_nothing_is_lost) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    /* A circle of some thousand columns of 48 sections each: more than one set holds, and all of it marked changed. */
    enum : uint32_t { RADIUS = 11u };
    sw_rig_t *const rig = sw_rig_new(t, root, RADIUS);
    REQUIRE(sw_session_begin(t, rig));
    save_world_t *const save = &rig->save->world;
    world_t *const world = rig->world;
    for (uint32_t slot = 0u; slot < world->map.slot_count; slot++) {
        if (world->map.borrowed_slots[slot].state == WORLD_MAP_STATE_READY) {
            world_edit_sections_mark(&world->edit, slot, WORLD_EDIT_SECTIONS_ALL, WORLD_EDIT_FOR_SAVE);
        }
    }
    const uint32_t total = world->map.ready_count * COLUMN_SECTIONS;
    REQUIRE(total > SAVE_WORLD_PENDING_SECTIONS);
    REQUIRE(save_world_capture_dirty(save, &world->edit) == SAVE_WORLD_FULL);
    CHECK_EQ_U64(save->pending[save->active].count, SAVE_WORLD_PENDING_SECTIONS);
    /* A column whose flags are left says so too, and keeps them. */
    bool found = false;
    for (uint32_t slot = 0u; slot < world->map.slot_count && !found; slot++) {
        const column_mask_t before = world_edit_dirty_peek(&world->edit, slot, WORLD_EDIT_CONSUMER_SAVE);
        if (column_mask_any(before)) {
            found = true;
            CHECK_EQ_U64(save_world_capture_column(save, &world->edit, slot), SAVE_WORLD_FULL);
            CHECK(column_mask_equal(world_edit_dirty_peek(&world->edit, slot, WORLD_EDIT_CONSUMER_SAVE), before));
        }
    }
    CHECK(found);
    REQUIRE(save_world_flush(save, &world->edit) == SAVE_WORLD_OK);
    CHECK_EQ_U64(save->sections_written, total);
    sw_session_end(rig);
    /* All of it on disk: with nothing changed from what generates, it should read the same. */
    REQUIRE(sw_session_begin(t, rig));
    sw_world_check(t, rig);
    uint64_t sections = 0u;
    save_world_manifest_header_t header;
    const save_world_region_t *regions = nullptr;
    sw_manifest_read(t, rig, &header, &regions);
    for (uint32_t i = 0u; i < header.region_count; i++) {
        sections += regions[i].section_count;
    }
    CHECK_EQ_U64(sections, total);
    sw_session_end(rig);
    sw_temp_remove(t, root);
}

TEST(a_save_that_fails_changes_nothing_and_loses_nothing) {
    if (geteuid() == 0u) {
        printf("  as root no directory can refuse, so there is nothing to see here\n");
        return;
    }
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_rig_new(t, root, SW_RADIUS);
    REQUIRE(sw_session_begin(t, rig));
    save_world_t *const save = &rig->save->world;
    world_edit_t *const edit = &rig->world->edit;
    sw_edit_random(t, rig, 0x123ull, 100u);
    REQUIRE(save_world_flush(save, edit) == SAVE_WORLD_OK);
    uint64_t bytes_before = 0u;
    const uint8_t *const before = sw_slurp(t, sw_join(t, rig->blocks, "manifest"), &bytes_before);
    sw_edit_random(t, rig, 0x456ull, 100u);
    REQUIRE(chmod(rig->blocks, 0555) == 0);
    const int32_t quiet = sw_quiet_begin();
    const save_world_status_t failed = save_world_flush(save, edit);
    sw_quiet_end(quiet);
    CHECK_EQ_U64(failed, SAVE_WORLD_ERROR_FILE);
    CHECK_EQ_U64(save->file_status, FILE_ERROR_ACCESS);
    CHECK_EQ_U64(save->generation, 1u);
    uint64_t bytes_after = 0u;
    const uint8_t *const after = sw_slurp(t, sw_join(t, rig->blocks, "manifest"), &bytes_after);
    REQUIRE(bytes_after == bytes_before);
    CHECK_MEM_EQ(after, before, bytes_before);
    /* A column read now still gets what could not be written. */
    world_t *const world = rig->world;
    for (uint32_t slot = 0u; slot < world->map.slot_count; slot++) {
        if (world->map.borrowed_slots[slot].state == WORLD_MAP_STATE_READY) {
            world_map_unload(&world->map, slot);
        }
    }
    sw_stream_all(t, rig);
    sw_world_check(t, rig);
    /* And once the disk is back, it arrives. */
    REQUIRE(chmod(rig->blocks, 0755) == 0);
    REQUIRE(save_world_flush(save, edit) == SAVE_WORLD_OK);
    CHECK_EQ_U64(save->generation, 2u);
    sw_session_end(rig);
    REQUIRE(sw_session_begin(t, rig));
    sw_world_check(t, rig);
    sw_session_end(rig);
    sw_temp_remove(t, root);
}

TEST(a_save_that_fails_halfway_through_its_regions_leaves_no_trace) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_rig_new(t, root, SW_RADIUS);
    REQUIRE(sw_session_begin(t, rig));
    save_world_t *const save = &rig->save->world;
    world_edit_t *const edit = &rig->world->edit;
    /* One voxel in a region that sorts first, which fits in its file, and then a region with far too much for the limit below. */
    const int32_t small_x = (SW_CENTER_X - 1) * 16;
    const int32_t small_y = (SW_CENTER_Y - 1) * 16;
    REQUIRE(world_edit_block_set(edit, small_x, small_y, 0, 3u) == WORLD_EDIT_STATUS_CHANGED);
    *sw_model_voxel(rig, small_x, small_y, 0) = 3u;
    uint64_t rng = 0x909ull;
    for (uint32_t i = 0u; i < 40000u; i++) {
        const int32_t x = SW_CENTER_X * 16 + (int32_t)(sw_next(&rng) % 32u);
        const int32_t y = SW_CENTER_Y * 16 + (int32_t)(sw_next(&rng) % 32u);
        const int32_t z = (int32_t)(sw_next(&rng) % 400u);
        const uint16_t state = (uint16_t)(sw_next(&rng) % BLOCK_COUNT);
        REQUIRE(world_edit_block_set(edit, x, y, z, state) != WORLD_EDIT_STATUS_NOT_LOADED);
        *sw_model_voxel(rig, x, y, z) = state;
    }
    /* The table alone is 24576 bytes, so that much and a little fits, and no more. What goes past it gets EFBIG, not a signal. */
    struct rlimit before;
    REQUIRE(getrlimit(RLIMIT_FSIZE, &before) == 0);
    struct rlimit limited = before;
    limited.rlim_cur = 64u * 1024u;
    void (*const previous)(int32_t) = signal(SIGXFSZ, SIG_IGN);
    REQUIRE(setrlimit(RLIMIT_FSIZE, &limited) == 0);
    const int32_t quiet = sw_quiet_begin();
    const save_world_status_t failed = save_world_flush(save, edit);
    sw_quiet_end(quiet);
    REQUIRE(setrlimit(RLIMIT_FSIZE, &before) == 0);
    (void)signal(SIGXFSZ, previous);
    CHECK_EQ_U64(failed, SAVE_WORLD_ERROR_FILE);
    CHECK_EQ_U64(save->file_status, FILE_ERROR_TOO_LARGE);
    CHECK_EQ_U64(save->generation, 0u);
    /* Neither the first region's file, which was finished, nor the second's, which was not. */
    CHECK_EQ_U64(sw_directory_count(rig->blocks, "r.", ""), 0u);
    CHECK_EQ_U64(sw_directory_count(rig->blocks, "", ".tmp"), 0u);
    save_world_manifest_header_t header;
    sw_manifest_read(t, rig, &header, nullptr);
    CHECK_EQ_U64(header.generation, 0u);
    /* With room again it all arrives. */
    REQUIRE(save_world_flush(save, edit) == SAVE_WORLD_OK);
    CHECK_EQ_U64(save->generation, 1u);
    sw_session_end(rig);
    REQUIRE(sw_session_begin(t, rig));
    sw_world_check(t, rig);
    sw_session_end(rig);
    sw_temp_remove(t, root);
}

/* ---- What is refused ------------------------------------------------------------------------------------------------- */

/* A world with something in it, and its directory's manifest as it was left. */
static sw_rig_t *sw_saved_world(test_context_t *t, const char *root) {
    sw_rig_t *const rig = sw_rig_new(t, root, SW_RADIUS);
    if (!sw_session_begin(t, rig)) {
        return rig;
    }
    sw_edit_random(t, rig, 0xFEEDull, 300u);
    if (!CHECK_EQ_U64(save_world_flush(&rig->save->world, &rig->world->edit), SAVE_WORLD_OK)) {
        return rig;
    }
    sw_session_end(rig);
    return rig;
}

static save_world_status_t sw_open_status(save_t *save, const char *root, uint64_t seed) {
    const save_world_status_t status = save_open(save, root, seed);
    save_close(save);
    return status;
}

/* Rewrites a header field and makes its checksum right, for a refusal that must come from the field and not the checksum. */
static void sw_manifest_forge(test_context_t *t, const char *path, uint64_t offset, const void *value, uint64_t bytes) {
    uint64_t length = 0u;
    uint8_t *const file = sw_slurp(t, path, &length);
    memcpy(file + offset, value, bytes);
    const uint64_t checksum = sw_fnv(file, 56u);
    memcpy(file + 56u, &checksum, sizeof(checksum));
    CHECK_EQ_U64(file_write_atomic(path, file, length), FILE_OK);
}

TEST(what_cannot_be_trusted_is_refused_and_left_as_it_was) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_saved_world(t, root);
    save_t *const save = sw_aligned(t, sizeof(save_t));
    REQUIRE(save_init(save));
    char *const manifest = sw_join(t, rig->blocks, "manifest");
    uint64_t good_bytes = 0u;
    const uint8_t *const good = sw_slurp(t, manifest, &good_bytes);
    REQUIRE(good_bytes > 64u);
    REQUIRE(sw_open_status(save, root, SW_SEED) == SAVE_WORLD_OK);
    const uint64_t region_files = sw_directory_count(rig->blocks, "r.", "");
    const int32_t quiet = sw_quiet_begin();

    CHECK_EQ_U64(sw_open_status(save, root, SW_SEED + 1u), SAVE_WORLD_ERROR_SEED);
    /* A later format: read past no checksum, and still refused for what it is. */
    const uint32_t later = SAVE_WORLD_VERSION + 1u;
    sw_manifest_forge(t, manifest, 40u, &later, sizeof(later));
    CHECK_EQ_U64(sw_open_status(save, root, SW_SEED), SAVE_WORLD_ERROR_VERSION);
    CHECK_EQ_U64(file_write_atomic(manifest, good, good_bytes), FILE_OK);
    /* An older format: it could not say what made its land, and that is what the person is told. */
    const uint32_t older = SAVE_WORLD_VERSION - 1u;
    sw_manifest_forge(t, manifest, 40u, &older, sizeof(older));
    CHECK_EQ_U64(sw_open_status(save, root, SW_SEED), SAVE_WORLD_ERROR_GENERATOR);
    CHECK_EQ_U64(file_write_atomic(manifest, good, good_bytes), FILE_OK);
    /* Land from a generator older or newer than this game's, in this format. Refused, and the manifest is left saying so. */
    static const uint32_t other_generators[] = {0u, TERRAIN_GENERATOR_VERSION - 1u, TERRAIN_GENERATOR_VERSION + 1u};
    for (uint32_t i = 0u; i < 3u; i++) {
        sw_manifest_forge(t, manifest, offsetof(save_world_manifest_header_t, generator), &other_generators[i],
                          sizeof(other_generators[i]));
        CHECK_EQ_U64(sw_open_status(save, root, SW_SEED), SAVE_WORLD_ERROR_GENERATOR);
        uint64_t left_bytes = 0u;
        const uint8_t *const left = sw_slurp(t, manifest, &left_bytes);
        REQUIRE(left_bytes == good_bytes);
        save_world_manifest_header_t left_header;
        memcpy(&left_header, left, sizeof(left_header));
        CHECK_EQ_U64(left_header.generator, other_generators[i]);
        CHECK_EQ_U64(file_write_atomic(manifest, good, good_bytes), FILE_OK);
    }
    /* What a person is told fits the menu's line after "Could not open it: ". */
    CHECK(strlen("Could not open it: ") + strlen(save_world_status_name(SAVE_WORLD_ERROR_GENERATOR)) <
          UI_MENU_MESSAGE_BYTES);
    /* Another block table. */
    const uint64_t other_table = 12345u;
    sw_manifest_forge(t, manifest, 24u, &other_table, sizeof(other_table));
    CHECK_EQ_U64(sw_open_status(save, root, SW_SEED), SAVE_WORLD_ERROR_TABLES);
    CHECK_EQ_U64(file_write_atomic(manifest, good, good_bytes), FILE_OK);

    /* One bit, anywhere in it, and it is not believed. */
    uint8_t *const copy = test_alloc(t, good_bytes);
    for (uint64_t i = 0u; i < good_bytes; i++) {
        memcpy(copy, good, good_bytes);
        copy[i] ^= (uint8_t)(1u << (i % 8u));
        CHECK_EQ_U64(file_write_atomic(manifest, copy, good_bytes), FILE_OK);
        const save_world_status_t status = sw_open_status(save, root, SW_SEED);
        if (!CHECK(status != SAVE_WORLD_OK)) {
            printf("  a flip at byte %" PRIu64 " was believed\n", i);
        }
        /* It wrote nothing, and took nothing away. */
        uint64_t after_bytes = 0u;
        const uint8_t *const after = (uint8_t *)test_alloc(t, good_bytes + 1u);
        CHECK_EQ_U64(file_read_whole(manifest, (void *)after, good_bytes + 1u, &after_bytes), FILE_OK);
        CHECK_MEM_EQ(after, copy, good_bytes);
        CHECK_EQ_U64(sw_directory_count(rig->blocks, "r.", ""), region_files);
    }
    /* Cut short, and with something after its end. */
    CHECK_EQ_U64(file_write_atomic(manifest, good, 63u), FILE_OK);
    CHECK_EQ_U64(sw_open_status(save, root, SW_SEED), SAVE_WORLD_ERROR_DAMAGED);
    CHECK_EQ_U64(file_write_atomic(manifest, good, good_bytes - 1u), FILE_OK);
    CHECK_EQ_U64(sw_open_status(save, root, SW_SEED), SAVE_WORLD_ERROR_DAMAGED);
    uint8_t *const longer = test_alloc(t, good_bytes + 1u);
    memcpy(longer, good, good_bytes);
    CHECK_EQ_U64(file_write_atomic(manifest, longer, good_bytes + 1u), FILE_OK);
    CHECK_EQ_U64(sw_open_status(save, root, SW_SEED), SAVE_WORLD_ERROR_DAMAGED);
    CHECK_EQ_U64(file_write_atomic(manifest, good, good_bytes), FILE_OK);
    /* A file it names is gone, or not the size it said. */
    save_world_region_t first;
    memcpy(&first, good + 64u, sizeof(first));
    char name[96];
    snprintf(name, sizeof(name), "r.%" PRId32 ".%" PRId32 ".%" PRIu64, first.region_x, first.region_y,
             first.generation);
    char *const delta = sw_join(t, rig->blocks, name);
    uint64_t delta_bytes = 0u;
    const uint8_t *const delta_file = sw_slurp(t, delta, &delta_bytes);
    REQUIRE(delta_bytes == first.file_bytes);
    CHECK_EQ_U64(file_write_atomic(delta, delta_file, delta_bytes - 1u), FILE_OK);
    CHECK_EQ_U64(sw_open_status(save, root, SW_SEED), SAVE_WORLD_ERROR_DAMAGED);
    CHECK_EQ_U64(file_remove(delta), FILE_OK);
    CHECK_EQ_U64(sw_open_status(save, root, SW_SEED), SAVE_WORLD_ERROR_DAMAGED);
    CHECK_EQ_U64(file_write_atomic(delta, delta_file, delta_bytes), FILE_OK);
    /* Deltas with no manifest: they are all that is left, and nobody should be told this is a new world. */
    CHECK_EQ_U64(file_remove(manifest), FILE_OK);
    CHECK_EQ_U64(sw_open_status(save, root, SW_SEED), SAVE_WORLD_ERROR_DAMAGED);
    CHECK_EQ_U64(sw_directory_count(rig->blocks, "manifest", ""), 0u);
    CHECK_EQ_U64(sw_directory_count(rig->blocks, "r.", ""), region_files);
    CHECK_EQ_U64(file_write_atomic(manifest, good, good_bytes), FILE_OK);
    /* Open in another game: and one that closes lets the next in. */
    REQUIRE(save_open(save, root, SW_SEED) == SAVE_WORLD_OK);
    save_t *const second = sw_aligned(t, sizeof(save_t));
    REQUIRE(save_init(second));
    CHECK_EQ_U64(save_open(second, root, SW_SEED), SAVE_WORLD_ERROR_LOCKED);
    CHECK_EQ_U64(second->world.open, 0u);
    save_close(save);
    CHECK_EQ_U64(save_open(second, root, SW_SEED), SAVE_WORLD_OK);
    save_close(second);
    save_deinit(second);
    /* Somewhere no directory can be. */
    CHECK_EQ_U64(sw_open_status(save, "/proc/nowhere", SW_SEED), SAVE_WORLD_ERROR_FILE);
    sw_quiet_end(quiet);

    /* After all that, it is as it was. */
    save_deinit(save);
    REQUIRE(sw_session_begin(t, rig));
    sw_world_check(t, rig);
    sw_session_end(rig);
    sw_temp_remove(t, root);
}

TEST(a_bad_byte_in_a_delta_costs_only_what_it_is_in) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_saved_world(t, root);
    save_t *const save = rig->save;
    REQUIRE(save_init(save));
    save_world_manifest_header_t header;
    const save_world_region_t *regions = nullptr;
    sw_manifest_read(t, rig, &header, &regions);
    REQUIRE(header.region_count == 4u);
    save_world_region_t region = regions[0];
    char name[96];
    snprintf(name, sizeof(name), "r.%" PRId32 ".%" PRId32 ".%" PRIu64, region.region_x, region.region_y,
             region.generation);
    char *const delta = sw_join(t, rig->blocks, name);
    uint64_t bytes = 0u;
    const uint8_t *const good = sw_slurp(t, delta, &bytes);
    REQUIRE(bytes == region.file_bytes);
    uint8_t *const copy = test_alloc(t, bytes);
    /* The columns of the window in this region. */
    const int32_t low_x = SW_CENTER_X - (int32_t)SW_RADIUS;
    const int32_t low_y = SW_CENTER_Y - (int32_t)SW_RADIUS;
    uint16_t *const states = rig->states;
    uint16_t *const generated = test_alloc(t, SW_COLUMN_BYTES);
    const int32_t quiet = sw_quiet_begin();
    uint64_t damage_seen = 0u;
    uint64_t rounds = 0u;
    for (uint64_t at = 0u; at < bytes; at += at < 128u ? 1u : 61u) {
        memcpy(copy, good, bytes);
        copy[at] ^= 0x40u;
        REQUIRE(file_write_atomic(delta, copy, bytes) == FILE_OK);
        if (!CHECK_EQ_U64(save_open(save, root, SW_SEED), SAVE_WORLD_OK)) {
            break;
        }
        rounds++;
        for (uint32_t i = 0u; i < SW_COLUMNS; i++) {
            const int32_t chunk_x = low_x + (int32_t)(i % SW_SIDE);
            const int32_t chunk_y = low_y + (int32_t)(i / SW_SIDE);
            if ((chunk_x >> SAVE_WORLD_REGION_SHIFT) != region.region_x ||
                (chunk_y >> SAVE_WORLD_REGION_SHIFT) != region.region_y) {
                continue;
            }
            sw_generate(chunk_x, chunk_y, states);
            const column_mask_t mask = save_world_column_overlay(&save->world, chunk_x, chunk_y, states, rig->scratch);
            /* Each section is the save's exactly, or what generates there exactly: never something between, and never a wrong block. */
            sw_generate(chunk_x, chunk_y, generated);
            for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
                const uint64_t voxel = (uint64_t)section * SECTION_VOXELS;
                const uint16_t *const expected =
                    column_mask_has(mask, section) ? sw_model_column(rig, chunk_x, chunk_y) : generated;
                if (!CHECK_MEM_EQ(states + voxel, expected + voxel, SECTION_VOXELS * sizeof(uint16_t))) {
                    printf("  a flip at byte %" PRIu64 " gave a wrong section\n", at);
                }
            }
        }
        damage_seen += atomic_load(&save->world.sections_damaged);
        save_close(save);
    }
    REQUIRE(file_write_atomic(delta, good, bytes) == FILE_OK);
    sw_quiet_end(quiet);
    CHECK(rounds > 100u);
    CHECK(damage_seen > 0u);
    save_deinit(save);
    sw_temp_remove(t, root);
}

TEST(a_save_over_a_damaged_delta_keeps_it_and_everything_it_could_read) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_rig_new(t, root, SW_RADIUS);
    REQUIRE(sw_session_begin(t, rig));
    /* Two sections of one column, at the middle of the window. */
    const int32_t x = SW_CENTER_X * 16;
    const int32_t y = SW_CENTER_Y * 16;
    REQUIRE(world_edit_block_set(&rig->world->edit, x, y, 0, 3u) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_block_set(&rig->world->edit, x, y, 160, 3u) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(save_world_flush(&rig->save->world, &rig->world->edit) == SAVE_WORLD_OK);
    sw_session_end(rig);
    save_world_manifest_header_t header;
    const save_world_region_t *regions = nullptr;
    sw_manifest_read(t, rig, &header, &regions);
    REQUIRE(header.region_count == 1u);
    char name[96];
    snprintf(name, sizeof(name), "r.%" PRId32 ".%" PRId32 ".%" PRIu64, regions[0].region_x, regions[0].region_y,
             regions[0].generation);
    char *const delta = sw_join(t, rig->blocks, name);
    /* The first record comes first in the file, right after the header and the table. */
    uint64_t bytes = 0u;
    uint8_t *const file = sw_slurp(t, delta, &bytes);
    const uint64_t first_record =
        sizeof(save_world_delta_header_t) + SAVE_WORLD_REGION_COLUMNS * sizeof(save_world_column_entry_t);
    file[first_record + 2u] ^= 1u;
    REQUIRE(file_write_atomic(delta, file, bytes) == FILE_OK);

    const int32_t quiet = sw_quiet_begin();
    REQUIRE(sw_session_begin(t, rig));
    /* What could not be read generates, what could comes back, and the count says so. */
    CHECK(atomic_load(&rig->save->world.sections_damaged) != 0u);
    uint16_t block = 0u;
    REQUIRE(world_map_block_get(&rig->world->map, x, y, 160, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, 3u);
    const uint16_t generated = *sw_model_voxel(rig, x, y, 0);
    REQUIRE(world_map_block_get(&rig->world->map, x, y, 0, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, generated);
    /* A save of that region carries on from there, and the old file stays, under another name. */
    REQUIRE(world_edit_block_set(&rig->world->edit, x, y, 320, 4u) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(save_world_flush(&rig->save->world, &rig->world->edit) == SAVE_WORLD_OK);
    sw_session_end(rig);
    sw_quiet_end(quiet);
    CHECK_EQ_U64(sw_directory_count(rig->blocks, "damaged.", ""), 1u);
    CHECK(file_path_size(delta, &(uint64_t){0u}) == FILE_ERROR_NOT_FOUND);
    sw_manifest_read(t, rig, &header, &regions);
    REQUIRE(header.region_count == 1u);
    CHECK_EQ_U64(regions[0].section_count, 2u);
    REQUIRE(sw_session_begin(t, rig));
    CHECK_EQ_U64(atomic_load(&rig->save->world.sections_damaged), 0u);
    REQUIRE(world_map_block_get(&rig->world->map, x, y, 160, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, 3u);
    REQUIRE(world_map_block_get(&rig->world->map, x, y, 320, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, 4u);
    sw_session_end(rig);
    /* It is never tidied away. */
    CHECK_EQ_U64(sw_directory_count(rig->blocks, "damaged.", ""), 1u);
    sw_temp_remove(t, root);
}

/* With every hash right, so that only what it says is wrong: a palette naming a block there is not. A save used to carry this over, to be counted damaged for ever. */
TEST(a_save_drops_a_record_no_reader_will_take_though_its_checksum_is_good) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_rig_new(t, root, SW_RADIUS);
    REQUIRE(sw_session_begin(t, rig));
    const int32_t x = SW_CENTER_X * 16;
    const int32_t y = SW_CENTER_Y * 16;
    REQUIRE(world_edit_block_set(&rig->world->edit, x, y, 0, 3u) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_block_set(&rig->world->edit, x, y, 160, 3u) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(save_world_flush(&rig->save->world, &rig->world->edit) == SAVE_WORLD_OK);
    sw_session_end(rig);
    save_world_manifest_header_t header;
    const save_world_region_t *regions = nullptr;
    sw_manifest_read(t, rig, &header, &regions);
    REQUIRE(header.region_count == 1u);
    char name[96];
    snprintf(name, sizeof(name), "r.%" PRId32 ".%" PRId32 ".%" PRIu64, regions[0].region_x, regions[0].region_y,
             regions[0].generation);
    char *const delta = sw_join(t, rig->blocks, name);
    uint64_t bytes = 0u;
    uint8_t *const file = sw_slurp(t, delta, &bytes);

    /* The first column there is, and its first section. */
    uint8_t *const table = file + sizeof(save_world_delta_header_t);
    save_world_column_entry_t column;
    uint32_t local = 0u;
    for (; local < SAVE_WORLD_REGION_COLUMNS; local++) {
        memcpy(&column, table + (uint64_t)local * sizeof(column), sizeof(column));
        if (column_mask_any(column.section_mask)) {
            break;
        }
    }
    REQUIRE(local < SAVE_WORLD_REGION_COLUMNS);
    const uint64_t entries_bytes =
        (uint64_t)column_mask_count(column.section_mask) * sizeof(save_world_section_entry_t);
    save_world_section_entry_t entry;
    memcpy(&entry, file + column.entries_offset, sizeof(entry));
    const uint16_t nothing = 0xFFFFu;
    memcpy(file + entry.offset + 4u, &nothing, sizeof(nothing));
    const uint64_t record_hash = sw_fnv(file + entry.offset, entry.bytes);
    entry.checksum = (uint32_t)(record_hash ^ (record_hash >> 32));
    memcpy(file + column.entries_offset, &entry, sizeof(entry));
    uint64_t column_hash = sw_fnv(&column, offsetof(save_world_column_entry_t, checksum));
    for (uint64_t i = 0u; i < entries_bytes; i++) {
        column_hash = (column_hash ^ file[column.entries_offset + i]) * 0x100000001B3ull;
    }
    column.checksum = column_hash;
    memcpy(table + (uint64_t)local * sizeof(column), &column, sizeof(column));
    REQUIRE(file_write_atomic(delta, file, bytes) == FILE_OK);

    const int32_t quiet = sw_quiet_begin();
    REQUIRE(sw_session_begin(t, rig));
    CHECK(atomic_load(&rig->save->world.sections_damaged) != 0u);
    REQUIRE(world_edit_block_set(&rig->world->edit, x, y, 320, 4u) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(save_world_flush(&rig->save->world, &rig->world->edit) == SAVE_WORLD_OK);
    sw_session_end(rig);
    sw_quiet_end(quiet);
    CHECK_EQ_U64(sw_directory_count(rig->blocks, "damaged.", ""), 1u);
    sw_manifest_read(t, rig, &header, &regions);
    REQUIRE(header.region_count == 1u);
    CHECK_EQ_U64(regions[0].section_count, 2u);
    REQUIRE(sw_session_begin(t, rig));
    CHECK_EQ_U64(atomic_load(&rig->save->world.sections_damaged), 0u);
    uint16_t block = 0u;
    REQUIRE(world_map_block_get(&rig->world->map, x, y, 160, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, 3u);
    REQUIRE(world_map_block_get(&rig->world->map, x, y, 320, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, 4u);
    sw_session_end(rig);
    sw_temp_remove(t, root);
}

/* Only an edited manifest says this. The count going round to 0 would leave the regions newer than the manifest, and a world that will not open. */
TEST(a_manifest_at_the_last_generation_opens_refuses_to_save_and_still_opens) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_saved_world(t, root);
    char *const manifest = sw_join(t, rig->blocks, "manifest");
    const uint64_t last = UINT64_MAX;
    sw_manifest_forge(t, manifest, offsetof(save_world_manifest_header_t, generation), &last, sizeof(last));
    uint64_t before_bytes = 0u;
    const uint8_t *const before = sw_slurp(t, manifest, &before_bytes);
    const int32_t quiet = sw_quiet_begin();
    REQUIRE(sw_session_begin(t, rig));
    const int32_t x = SW_CENTER_X * 16;
    const int32_t y = SW_CENTER_Y * 16;
    REQUIRE(world_edit_block_set(&rig->world->edit, x, y, 320, 4u) == WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_U64(save_world_flush(&rig->save->world, &rig->world->edit), SAVE_WORLD_ERROR_TOO_MANY);
    CHECK_EQ_U64(rig->save->world.generation, UINT64_MAX);
    sw_session_end(rig);
    sw_quiet_end(quiet);
    uint64_t after_bytes = 0u;
    const uint8_t *const after = sw_slurp(t, manifest, &after_bytes);
    REQUIRE(after_bytes == before_bytes);
    CHECK_MEM_EQ(after, before, before_bytes);
    CHECK_EQ_U64(sw_directory_count(rig->blocks, "r.", ".tmp"), 0u);
    /* And what it does not count as lost can be told by opening it again. */
    const int32_t quiet_again = sw_quiet_begin();
    REQUIRE(sw_session_begin(t, rig));
    CHECK_EQ_U64(rig->save->world.generation, UINT64_MAX);
    sw_session_end(rig);
    sw_quiet_end(quiet_again);
    sw_temp_remove(t, root);
}

TEST(what_a_crash_leaves_is_tidied_and_nothing_else) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_saved_world(t, root);
    static const char *const leftovers[] = {"manifest.tmp", "r.0.0.99", "r.0.0.99.tmp", "r.-5.7.123.tmp", "r.-2.0.0"};
    static const char *const others[] = {"notes.txt", "r.txt", "r.1.2", "tmp", "damaged.1.1.1", ".tmp"};
    for (uint32_t i = 0u; i < sizeof(leftovers) / sizeof(leftovers[0]); i++) {
        REQUIRE(file_write_atomic(sw_join(t, rig->blocks, leftovers[i]), "x", 1u) == FILE_OK);
    }
    for (uint32_t i = 0u; i < sizeof(others) / sizeof(others[0]); i++) {
        REQUIRE(file_write_atomic(sw_join(t, rig->blocks, others[i]), "x", 1u) == FILE_OK);
    }
    REQUIRE(sw_session_begin(t, rig));
    sw_world_check(t, rig);
    sw_session_end(rig);
    uint64_t size = 0u;
    for (uint32_t i = 0u; i < sizeof(leftovers) / sizeof(leftovers[0]); i++) {
        CHECK_EQ_U64(file_path_size(sw_join(t, rig->blocks, leftovers[i]), &size), FILE_ERROR_NOT_FOUND);
    }
    for (uint32_t i = 0u; i < sizeof(others) / sizeof(others[0]); i++) {
        CHECK_EQ_U64(file_path_size(sw_join(t, rig->blocks, others[i]), &size), FILE_OK);
    }
    sw_temp_remove(t, root);
}

/* ---- Dying ------------------------------------------------------------------------------------------------------------- */

/* Which time through which step to die. */
typedef struct sw_kill_plan_t {
    uint32_t step;      /* 0 */
    uint32_t remaining; /* 4: times to pass it first */
} sw_kill_plan_t;
static_assert(sizeof(sw_kill_plan_t) == 8);

static void sw_kill_hook(void *context, save_world_step_t step) {
    sw_kill_plan_t *const plan = context;
    if (step == plan->step) {
        if (plan->remaining == 0u) {
            (void)raise(SIGKILL);
        }
        plan->remaining--;
    }
}

/* The edits of each save in the worlds below: a pure function of its number. */
static uint64_t sw_kill_rng(uint64_t generation) {
    return 0xC0FFEE000ull + generation * 2654435761ull;
}
static const uint32_t SW_KILL_EDITS = 150u;

/* The model after this many saves. */
static void sw_kill_model(const sw_rig_t *rig, uint64_t generation) {
    sw_model_reset(rig);
    for (uint64_t g = 1u; g <= generation; g++) {
        sw_edit_model_only(rig, sw_kill_rng(g), SW_KILL_EDITS);
    }
}

/* The child's part: opens what is there, makes the next save, and dies inside it. */
[[noreturn]] static void sw_kill_child(test_context_t *t, sw_rig_t *rig, const sw_kill_plan_t *plan_in) {
    sw_kill_plan_t plan = *plan_in;
    if (!sw_session_begin(t, rig)) {
        _exit(10);
    }
    save_world_step_hook_set(&rig->save->world, sw_kill_hook, &plan);
    sw_edit_random(t, rig, sw_kill_rng(rig->save->world.generation + 1u), SW_KILL_EDITS);
    (void)save_world_flush(&rig->save->world, &rig->world->edit);
    _exit(11); /* it was to have died */
}

TEST(killed_at_every_step_of_a_save_the_world_is_one_save_or_the_other) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_rig_new(t, root, SW_RADIUS);
    /* First a save every test below starts from. */
    REQUIRE(sw_session_begin(t, rig));
    sw_edit_random(t, rig, sw_kill_rng(1u), SW_KILL_EDITS);
    REQUIRE(save_world_flush(&rig->save->world, &rig->world->edit) == SAVE_WORLD_OK);
    sw_session_end(rig);

    /* Each step, and each of its first and second time through: there are four regions, so most come several times. */
    static const sw_kill_plan_t plans[] = {
        {SAVE_WORLD_STEP_DELTA_RECORDS, 0u}, {SAVE_WORLD_STEP_DELTA_RECORDS, 1u}, {SAVE_WORLD_STEP_DELTA_SYNCED, 0u},
        {SAVE_WORLD_STEP_DELTA_SYNCED, 2u},  {SAVE_WORLD_STEP_DELTA_RENAMED, 0u}, {SAVE_WORLD_STEP_DELTA_RENAMED, 3u},
        {SAVE_WORLD_STEP_DELTAS_DONE, 0u},   {SAVE_WORLD_STEP_MANIFEST_DONE, 0u}, {SAVE_WORLD_STEP_REPLACED_GONE, 0u},
    };
    uint64_t generation = 1u;
    for (uint32_t i = 0u; i < sizeof(plans) / sizeof(plans[0]); i++) {
        (void)fflush(stdout);
        const pid_t child = fork();
        REQUIRE(child >= 0);
        if (child == 0) {
            sw_kill_child(t, rig, &plans[i]);
        }
        int32_t status = 0;
        REQUIRE(waitpid(child, &status, 0) == child);
        if (!CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)) {
            printf("  plan %" PRIu32 " ended with %" PRId32 "\n", i, status);
            continue;
        }
        /* Once the manifest has been replaced the save has happened. Before that, not. */
        const bool happened = plans[i].step >= SAVE_WORLD_STEP_MANIFEST_DONE;
        const uint64_t expected = generation + (happened ? 1u : 0u);
        sw_kill_model(rig, expected);
        REQUIRE(sw_session_begin(t, rig));
        if (!CHECK_EQ_U64(rig->save->world.generation, expected)) {
            printf("  plan %" PRIu32 "\n", i);
        }
        sw_world_check(t, rig);
        CHECK_EQ_U64(atomic_load(&rig->save->world.sections_damaged), 0u);
        sw_session_end(rig);
        generation = expected;
        /* Whatever it left has gone, and only what the manifest names is there. */
        CHECK_EQ_U64(sw_directory_count(rig->blocks, "", ".tmp"), 0u);
        CHECK_EQ_U64(sw_directory_count(rig->blocks, "r.", ""), 4u);
    }
    CHECK_EQ_U64(generation, 3u);
    /* And the world goes on: a save after all of that, for the next time. */
    REQUIRE(sw_session_begin(t, rig));
    sw_edit_random(t, rig, sw_kill_rng(generation + 1u), SW_KILL_EDITS);
    REQUIRE(save_world_flush(&rig->save->world, &rig->world->edit) == SAVE_WORLD_OK);
    CHECK_EQ_U64(rig->save->world.generation, generation + 1u);
    sw_session_end(rig);
    sw_kill_model(rig, generation + 1u);
    REQUIRE(sw_session_begin(t, rig));
    sw_world_check(t, rig);
    sw_session_end(rig);
    sw_temp_remove(t, root);
}

/* Saves as fast as it can, saying each one down the pipe, until it is killed. */
[[noreturn]] static void sw_kill_saver(test_context_t *t, sw_rig_t *rig, int32_t pipe_out) {
    if (!sw_session_begin(t, rig)) {
        _exit(10);
    }
    for (;;) {
        const uint64_t next = rig->save->world.generation + 1u;
        sw_edit_random(t, rig, sw_kill_rng(next), SW_KILL_EDITS);
        if (save_world_flush(&rig->save->world, &rig->world->edit) != SAVE_WORLD_OK) {
            _exit(12);
        }
        if (write(pipe_out, &next, sizeof(next)) != (int64_t)sizeof(next)) {
            _exit(13);
        }
    }
}

TEST(killed_at_any_moment_it_loses_at_most_the_save_in_progress) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_rig_new(t, root, SW_RADIUS);
    uint64_t rng = (uint64_t)time_now_ns() | 1u;
    uint64_t confirmed = 0u;
    uint32_t interrupted = 0u;
    uint32_t mid_save = 0u;
    for (uint32_t round = 0u; round < SW_KILL_ROUNDS; round++) {
        int32_t pipe_ends[2];
        REQUIRE(pipe(pipe_ends) == 0);
        (void)fflush(stdout);
        const pid_t child = fork();
        REQUIRE(child >= 0);
        if (child == 0) {
            (void)close(pipe_ends[0]);
            sw_kill_saver(t, rig, pipe_ends[1]);
        }
        (void)close(pipe_ends[1]);
        /* Long enough to be past its start and in among its saves, and never the same. */
        const struct timespec wait = {.tv_sec = 0, .tv_nsec = (int64_t)(250000000u + sw_next(&rng) % 150000000u)};
        (void)nanosleep(&wait, nullptr);
        REQUIRE(kill(child, SIGKILL) == 0);
        int32_t status = 0;
        REQUIRE(waitpid(child, &status, 0) == child);
        CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
        uint64_t said = 0u;
        while (read(pipe_ends[0], &said, sizeof(said)) == (int64_t)sizeof(said)) {
            confirmed = said > confirmed ? said : confirmed;
        }
        (void)close(pipe_ends[0]);
        /* Look at what it left before anything tidies it. */
        if (sw_directory_count(rig->blocks, "", ".tmp") != 0u || sw_directory_count(rig->blocks, "r.", "") > 4u) {
            interrupted++;
        }
        save_world_manifest_header_t header;
        sw_manifest_read(t, rig, &header, nullptr);
        /* What it said it had saved is there. It may have got one further and not lived to say so. */
        CHECK(header.generation >= confirmed);
        CHECK(header.generation <= confirmed + 1u);
        mid_save += header.generation == confirmed + 1u ? 1u : 0u;
        confirmed = header.generation;
        sw_kill_model(rig, confirmed);
        REQUIRE(sw_session_begin(t, rig));
        CHECK_EQ_U64(rig->save->world.generation, confirmed);
        sw_world_check(t, rig);
        CHECK_EQ_U64(atomic_load(&rig->save->world.sections_damaged), 0u);
        sw_session_end(rig);
        CHECK_EQ_U64(sw_directory_count(rig->blocks, "", ".tmp"), 0u);
    }
    printf("  %" PRIu32 " rounds, %" PRIu64 " saves, %" PRIu32 " kills left files behind, %" PRIu32
           " came after the rename and before it could say so\n",
           (uint32_t)SW_KILL_ROUNDS, confirmed, interrupted, mid_save);
    /* If none of them landed in a save, this test proved nothing. */
    CHECK(interrupted > 0u);
    CHECK(confirmed > SW_KILL_ROUNDS);
    sw_temp_remove(t, root);
}

/* ---- Threads ----------------------------------------------------------------------------------------------------------- */

typedef struct sw_thread_shared_t {
    save_world_t *save;                         /* 0 */
    _Atomic uint32_t stamps[SW_THREAD_COLUMNS]; /* 8: a column's stamp, 0 while it is in the map */
    uint8_t _pad0[4];                           /* 108 */
    _Atomic uint32_t stop;                      /* 112 */
    _Atomic uint32_t wrong;                     /* 116 */
    _Atomic uint32_t write_asked;               /* 120 */
    _Atomic uint32_t write_failed;              /* 124 */
    _Atomic uint64_t reads;                     /* 128 */
} sw_thread_shared_t;
static_assert(sizeof(sw_thread_shared_t) == 136);

static int32_t sw_thread_chunk_x(uint32_t column) {
    return SW_CENTER_X - (int32_t)SW_RADIUS + (int32_t)(column % SW_SIDE);
}
static int32_t sw_thread_chunk_y(uint32_t column) {
    return SW_CENTER_Y - (int32_t)SW_RADIUS + (int32_t)(column / SW_SIDE);
}
static uint16_t sw_thread_state(uint32_t stamp) {
    return (uint16_t)(1u + stamp % (BLOCK_COUNT - 1u));
}

/* One reader, with memory of its own. */
typedef struct sw_thread_reader_t {
    sw_thread_shared_t *shared; /* 0 */
    uint16_t *states;           /* 8: one column */
    uint8_t *scratch;           /* 16 */
} sw_thread_reader_t;
static_assert(sizeof(sw_thread_reader_t) == 24);

/* Reads columns not in the map as streaming would: each must have the last stamp captured before it looked, whatever a save is doing. */
static void *sw_thread_reader(void *argument) {
    const sw_thread_reader_t *const reader = argument;
    sw_thread_shared_t *const shared = reader->shared;
    uint16_t *const column_states = reader->states;
    uint8_t *const scratch = reader->scratch;
    uint64_t rng = (uint64_t)(uintptr_t)reader | 1u;
    while (atomic_load_explicit(&shared->stop, memory_order_acquire) == 0u) {
        const uint32_t column = (uint32_t)(sw_next(&rng) % SW_THREAD_COLUMNS);
        const uint32_t before = atomic_load_explicit(&shared->stamps[column], memory_order_acquire);
        if (before == 0u) {
            continue;
        }
        const uint32_t section = column_section_of(0);
        column_states[(uint64_t)section * SECTION_VOXELS] = 0u;
        (void)save_world_column_overlay(shared->save, sw_thread_chunk_x(column), sw_thread_chunk_y(column),
                                        column_states, scratch);
        const uint32_t after = atomic_load_explicit(&shared->stamps[column], memory_order_acquire);
        if (before == after && column_states[(uint64_t)section * SECTION_VOXELS] != sw_thread_state(before)) {
            atomic_fetch_add_explicit(&shared->wrong, 1u, memory_order_relaxed);
        }
        atomic_fetch_add_explicit(&shared->reads, 1u, memory_order_relaxed);
    }
    return nullptr;
}

/* A worker making each save it is asked for. */
static void *sw_thread_writer(void *argument) {
    sw_thread_shared_t *const shared = argument;
    while (atomic_load_explicit(&shared->stop, memory_order_acquire) == 0u) {
        if (atomic_load_explicit(&shared->write_asked, memory_order_acquire) == 0u) {
            continue;
        }
        if (save_world_commit_write(shared->save) != SAVE_WORLD_OK) {
            atomic_fetch_add_explicit(&shared->write_failed, 1u, memory_order_relaxed);
        }
        atomic_store_explicit(&shared->write_asked, 0u, memory_order_release);
    }
    return nullptr;
}

TEST(readers_on_threads_never_miss_a_section_between_a_set_and_its_file) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_rig_new(t, root, SW_RADIUS);
    REQUIRE(sw_session_begin(t, rig));
    world_t *const world = rig->world;
    save_world_t *const save = &rig->save->world;
    sw_thread_shared_t *const shared = test_alloc(t, sizeof(sw_thread_shared_t));
    shared->save = save;
    pthread_t readers[SW_THREAD_READERS];
    pthread_t writer;
    sw_thread_reader_t *const reader_parts = test_alloc(t, SW_THREAD_READERS * sizeof(sw_thread_reader_t));
    for (uint32_t i = 0u; i < SW_THREAD_READERS; i++) {
        reader_parts[i] = (sw_thread_reader_t){.shared = shared,
                                               .states = test_alloc(t, SW_COLUMN_BYTES),
                                               .scratch = test_alloc(t, SAVE_WORLD_OVERLAY_SCRATCH_BYTES)};
        REQUIRE(pthread_create(&readers[i], nullptr, sw_thread_reader, &reader_parts[i]) == 0);
    }
    REQUIRE(pthread_create(&writer, nullptr, sw_thread_writer, shared) == 0);
    uint32_t stamp = 0u;
    for (uint32_t pass = 0u; pass < SW_THREAD_PASSES; pass++) {
        for (uint32_t column = 0u; column < SW_THREAD_COLUMNS; column++) {
            const int32_t chunk_x = sw_thread_chunk_x(column);
            const int32_t chunk_y = sw_thread_chunk_y(column);
            /* In the map: what a reader is told not to look at. */
            atomic_store_explicit(&shared->stamps[column], 0u, memory_order_release);
            stamp++;
            const world_edit_status_t status =
                world_edit_block_set(&world->edit, chunk_x * 16, chunk_y * 16, 0, sw_thread_state(stamp));
            REQUIRE(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED);
            uint32_t slot = 0u;
            REQUIRE(world_map_slot_of(&world->map, chunk_x, chunk_y, &slot));
            REQUIRE(save_world_capture_column(save, &world->edit, slot) == SAVE_WORLD_OK);
            /* Out of the map: only the set and the files have it now. */
            atomic_store_explicit(&shared->stamps[column], stamp, memory_order_release);
            /* Every few captures a save, on the other thread, with the next captures going on over it. */
            if (stamp % 7u == 0u && atomic_load_explicit(&shared->write_asked, memory_order_acquire) == 0u &&
                save_world_commit_begin(save)) {
                atomic_store_explicit(&shared->write_asked, 1u, memory_order_release);
            }
        }
    }
    while (atomic_load_explicit(&shared->write_asked, memory_order_acquire) != 0u) {
        struct timespec nap = {.tv_sec = 0, .tv_nsec = 100000};
        (void)nanosleep(&nap, nullptr);
    }
    atomic_store_explicit(&shared->stop, 1u, memory_order_release);
    for (uint32_t i = 0u; i < SW_THREAD_READERS; i++) {
        REQUIRE(pthread_join(readers[i], nullptr) == 0);
    }
    REQUIRE(pthread_join(writer, nullptr) == 0);
    printf("  %" PRIu64 " reads over %" PRIu64 " saves\n", atomic_load(&shared->reads), save->saves_done);
    CHECK_EQ_U64(atomic_load(&shared->wrong), 0u);
    CHECK_EQ_U64(atomic_load(&shared->write_failed), 0u);
    CHECK(atomic_load(&shared->reads) > 100u);
    CHECK(save->saves_done > 3u);
    CHECK_EQ_U64(atomic_load(&save->sections_damaged), 0u);
    REQUIRE(save_world_flush(save, &world->edit) == SAVE_WORLD_OK);
    sw_session_end(rig);
    sw_temp_remove(t, root);
}

/* ---- The Owner, and what it costs --------------------------------------------------------------------------------------- */

TEST(the_owner_holds_one_region_and_gives_it_back) {
    heap_counters_t before;
    heap_counters_read(&before);
    save_t *const save = sw_aligned(t, sizeof(save_t));
    /* Zero bytes are safe to deinit. */
    save_deinit(save);
    REQUIRE(save_init(save));
    heap_counters_t held;
    heap_counters_read(&held);
    CHECK_EQ_U64(held.live_regions, before.live_regions + 1u);
    CHECK(save->owned_region.bytes >= SAVE_WORLD_ARENA_BYTES);
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    for (uint32_t i = 0u; i < 3u; i++) {
        REQUIRE(save_open(save, root, SW_SEED) == SAVE_WORLD_OK);
        CHECK(save->arena_world.used <= SAVE_WORLD_ARENA_BYTES);
        CHECK_EQ_U64(save->arena_world.failed_count, 0u);
        save_close(save);
        CHECK_EQ_U64(save->arena_world.used, 0u);
    }
    /* A world still open goes with it, and gives up its lock. */
    REQUIRE(save_open(save, root, SW_SEED) == SAVE_WORLD_OK);
    save_deinit(save);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    REQUIRE(save_init(save));
    CHECK_EQ_U64(save_open(save, root, SW_SEED), SAVE_WORLD_OK);
    save_deinit(save);
    sw_temp_remove(t, root);
}

TEST(no_heap_in_a_capture_a_save_or_a_read) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    sw_rig_t *const rig = sw_rig_new(t, root, SW_RADIUS);
    REQUIRE(sw_session_begin(t, rig));
    heap_counters_t before;
    heap_counters_read(&before);
    sw_edit_random(t, rig, 0x42ull, 500u);
    REQUIRE(save_world_capture_dirty(&rig->save->world, &rig->world->edit) == SAVE_WORLD_OK);
    REQUIRE(save_world_commit_begin(&rig->save->world));
    REQUIRE(save_world_commit_write(&rig->save->world) == SAVE_WORLD_OK);
    sw_generate(SW_CENTER_X, SW_CENTER_Y, rig->states);
    (void)save_world_column_overlay(&rig->save->world, SW_CENTER_X, SW_CENTER_Y, rig->states, rig->scratch);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    sw_session_end(rig);
    sw_temp_remove(t, root);
}

/* ---- Real terrain, and light ------------------------------------------------------------------------------------------- */

TEST(real_terrain_with_real_light_reloads_byte_for_byte) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    enum : uint32_t { RADIUS = 1u, SIDE = 2u * RADIUS + 1u, COLUMNS = SIDE * SIDE };
    world_t *const world = sw_aligned(t, sizeof(world_t));
    save_t *const save = sw_aligned(t, sizeof(save_t));
    terrain_t *const terrain = sw_aligned(t, sizeof(terrain_t));
    uint16_t *const states = test_alloc(t, SW_COLUMN_BYTES);
    uint8_t *const opened = test_alloc(t, SW_COLUMN_VOXELS);
    uint16_t *const blocks_expected = test_alloc(t, COLUMNS * SW_COLUMN_BYTES);
    uint8_t *const light_expected = test_alloc(t, COLUMNS * SW_COLUMN_VOXELS);
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    REQUIRE(terrain_init(terrain, SW_SEED));
    /* A circle of radius 2 holds all of the square of radius 1. */
    const world_limits_t limits = {.section_bytes = 256ull << 20, .column_count = 25u};
    for (uint32_t session = 0u; session < 2u; session++) {
        REQUIRE(save_init(save));
        REQUIRE(world_init(world, limits));
        REQUIRE(save_open(save, root, SW_SEED) == SAVE_WORLD_OK);
        REQUIRE(world_open(world, RADIUS + 1u, 0, 0));
        world_map_request_t request;
        while (world_map_stream_take(&world->map, &request, 1u) == 1u) {
            const terrain_density_box_t box = {.origin_x = request.chunk_x * (int32_t)COLUMN_SIZE,
                                               .origin_y = request.chunk_y * (int32_t)COLUMN_SIZE,
                                               .origin_z = COLUMN_Z_STORAGE_MIN,
                                               .count_x = COLUMN_SIZE,
                                               .count_y = COLUMN_SIZE,
                                               .count_z = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN)};
            terrain_generate_box(terrain, &box, opened, states);
            (void)save_world_column_overlay(&save->world, request.chunk_x, request.chunk_y, states, opened);
            column_t *const column = world_map_stream_commit(&world->map, &request);
            REQUIRE(column != nullptr);
            for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
                REQUIRE(section_blocks_import(&column->sections[section], &world->section_pool,
                                              states + (uint64_t)section * SECTION_VOXELS));
            }
            world_edit_column_filled(&world->edit, request.slot);
            (void)world_light_column_added(&world->light, request.slot);
        }
        while (world_light_update(&world->light) != WORLD_LIGHT_STATUS_OK) {
        }
        for (uint32_t i = 0u; i < COLUMNS && session == 1u; i++) {
            const column_t *const column = world_map_column_at(&world->map, (int32_t)(i % SIDE) - (int32_t)RADIUS,
                                                               (int32_t)(i / SIDE) - (int32_t)RADIUS);
            REQUIRE(column != nullptr);
            for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
                section_blocks_export(&column->sections[section], blocks);
                section_lights_export(&column->sections[section], light);
                const uint64_t at = (uint64_t)i * SW_COLUMN_VOXELS + (uint64_t)section * SECTION_VOXELS;
                CHECK_MEM_EQ(blocks, blocks_expected + at, SECTION_VOXELS * sizeof(uint16_t));
                CHECK_MEM_EQ(light, light_expected + at, SECTION_VOXELS);
            }
        }
        if (session == 0u) {
            /* A pit, a tower and a torch's worth of changes, all through the edit path. */
            uint64_t rng = 0x31337ull;
            for (uint32_t i = 0u; i < 3000u; i++) {
                const int32_t x = -16 + (int32_t)(sw_next(&rng) % 48u);
                const int32_t y = -16 + (int32_t)(sw_next(&rng) % 48u);
                const int32_t z = -60 + (int32_t)(sw_next(&rng) % 200u);
                const uint16_t state = i % 3u == 0u ? (uint16_t)BLOCK_ID_AIR : (uint16_t)(sw_next(&rng) % BLOCK_COUNT);
                const world_edit_status_t status = world_edit_block_set(&world->edit, x, y, z, state);
                REQUIRE(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED);
            }
            while (world_light_update(&world->light) != WORLD_LIGHT_STATUS_OK) {
            }
            for (uint32_t i = 0u; i < COLUMNS; i++) {
                const column_t *const column = world_map_column_at(&world->map, (int32_t)(i % SIDE) - (int32_t)RADIUS,
                                                                   (int32_t)(i / SIDE) - (int32_t)RADIUS);
                REQUIRE(column != nullptr);
                for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
                    const uint64_t at = (uint64_t)i * SW_COLUMN_VOXELS + (uint64_t)section * SECTION_VOXELS;
                    section_blocks_export(&column->sections[section], blocks_expected + at);
                    section_lights_export(&column->sections[section], light_expected + at);
                }
            }
            REQUIRE(save_world_flush(&save->world, &world->edit) == SAVE_WORLD_OK);
            CHECK(save->world.sections_written > 0u);
            /* Far less than a world: this is what a delta is for. */
            CHECK(save->world.sections_written < (uint64_t)COLUMNS * COLUMN_SECTIONS);
        } else {
            CHECK(atomic_load(&save->world.sections_loaded) > 0u);
            CHECK_EQ_U64(atomic_load(&save->world.sections_damaged), 0u);
        }
        world_close(world);
        world_deinit(world);
        save_deinit(save);
    }
    terrain_deinit(terrain);
    sw_temp_remove(t, root);
}

/* ---- The real game ------------------------------------------------------------------------------------------------------ */

/* Steps the sim until what it is waiting for is so. */
static bool sw_game_step_until(game_t *game, int32_t chunk_x, int32_t chunk_y, bool wanted) {
    const int64_t deadline = time_now_ns() + 60000000000ll;
    while (time_now_ns() < deadline) {
        game_sim_step(game);
        if ((world_map_column_at(&game->borrowed_world->map, chunk_x, chunk_y) != nullptr) == wanted) {
            return true;
        }
        const struct timespec nap = {.tv_sec = 0, .tv_nsec = 200000};
        (void)nanosleep(&nap, nullptr);
    }
    return false;
}

TEST(the_game_saves_a_column_that_leaves_and_finds_it_when_it_comes_back) {
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    gpu_bridge_t *const bridge = sw_aligned(t, sizeof(gpu_bridge_t));
    job_system_t *const job = sw_aligned(t, sizeof(job_system_t));
    world_t *const world = sw_aligned(t, sizeof(world_t));
    terrain_t *const terrain = sw_aligned(t, sizeof(terrain_t));
    render_t *const render = sw_aligned(t, sizeof(render_t));
    texture_t *const texture = sw_aligned(t, sizeof(texture_t));
    game_t *const game = sw_aligned(t, sizeof(game_t));
    save_t *const save = sw_aligned(t, sizeof(save_t));
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    REQUIRE(gpu_bridge_init(bridge, gpu_bridge_limits_default()) == GPU_STATUS_OK);
    REQUIRE(thread_topology_read(topology));
    REQUIRE(job_init(job, topology));
    REQUIRE(texture_init(texture));
    REQUIRE(world_init(world, world_limits_default()));
    REQUIRE(terrain_init(terrain, SW_SEED));
    REQUIRE(render_init(render, &bridge->device_memory, job, render_limits_default()));
    REQUIRE(save_init(save));
    game_init(game, world, terrain, render, job, texture, topology);
    game_save_set(game, save);
    REQUIRE(save_open(save, root, SW_SEED) == SAVE_WORLD_OK);
    const game_config_t config = {.render_distance = 3u, .width = 64u, .height = 36u, ._pad0 = {0}};
    REQUIRE(game_open(game, &config));
    const int32_t home_x = atomic_load(&game->center_x);
    const int32_t home_y = atomic_load(&game->center_y);
    REQUIRE(sw_game_step_until(game, home_x, home_y, true));
    /* A place that will go out of range, with a block in it no terrain would put there. */
    const int32_t x = home_x * 16 + 3;
    const int32_t y = home_y * 16 + 5;
    const int32_t z = 400;
    const uint16_t placed = 7u;
    REQUIRE(world_edit_block_set(&world->edit, x, y, z, placed) == WORLD_EDIT_STATUS_CHANGED);
    /* Far enough that the column goes, and no save has yet been asked for. */
    atomic_store(&game->center_x, home_x + 40);
    REQUIRE(sw_game_step_until(game, home_x, home_y, false));
    CHECK_EQ_U64(save->world.generation, 0u);
    CHECK_EQ_U64(save->world.pending[save->world.active].count, 1u);
    atomic_store(&game->center_x, home_x);
    REQUIRE(sw_game_step_until(game, home_x, home_y, true));
    uint16_t block = 0u;
    CHECK_EQ_U64(world_map_block_get(&world->map, x, y, z, &block), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, placed);
    /* And a world opened anew from the file alone has it too. */
    REQUIRE(save_world_flush(&save->world, &world->edit) == SAVE_WORLD_OK);
    CHECK_EQ_U64(save->world.generation, 1u);
    game_close(game);
    save_close(save);
    REQUIRE(save_open(save, root, SW_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(game, &config));
    REQUIRE(sw_game_step_until(game, home_x, home_y, true));
    CHECK_EQ_U64(world_map_block_get(&world->map, x, y, z, &block), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, placed);
    CHECK_EQ_U64(atomic_load(&save->world.sections_damaged), 0u);
    game_close(game);
    save_close(save);
    game_deinit(game);
    render_deinit(render);
    CHECK(gpu_bridge_exit_check(bridge));
    terrain_deinit(terrain);
    world_deinit(world);
    texture_deinit(texture);
    job_deinit(job);
    gpu_bridge_deinit(bridge);
    save_deinit(save);
    sw_temp_remove(t, root);
}

/* Opens the first world on the list, looks down, digs, and goes on digging until the end, which closes it. At 640 by 480, scale 2: see
 * tests/run/ui/menu/fixtures/flow.script for how these clicks were worked out. */
static const char SW_DIG_SCRIPT[] = "1 move 0 -40\n"
                                    "+1 button left down\n+1 button left up\n"
                                    "+1 move -20 -122\n"
                                    "+1 button left down\n+1 button left up\n"
                                    "+1 move -112 318\n"
                                    "+1 button left down\n+1 button left up\n"
                                    "+4 move 112 500\n"
                                    "+4 button left down\n"
                                    "+70 button left up\n"
                                    "+4 end\n";
/* The same round, with nothing dug. */
static const char SW_LOOK_SCRIPT[] = "1 move 0 -40\n"
                                     "+1 button left down\n+1 button left up\n"
                                     "+1 move -20 -122\n"
                                     "+1 button left down\n+1 button left up\n"
                                     "+1 move -112 318\n"
                                     "+1 button left down\n+1 button left up\n"
                                     "+6 end\n";

static void sw_program_run(test_context_t *t, const char *root, const char *saves, const char *name, const char *text,
                           test_process_t *out) {
    char *const script = sw_join(t, root, name);
    if (!CHECK_EQ_U64(file_write_atomic(script, text, strlen(text)), FILE_OK)) {
        return;
    }
    char *const argv[] = {
        t->borrowed_args[0], (char *)"--headless",        (char *)"--menu", (char *)"--saves",
        (char *)saves,       (char *)"--script",          script,           (char *)"--screenshot-dir",
        (char *)root,        (char *)"--width",           (char *)"640",    (char *)"--height",
        (char *)"480",       (char *)"--render-distance", (char *)"4",      nullptr};
    test_process_run(t, argv, 240000u, out);
}

TEST(the_built_program_keeps_what_was_dug_and_a_world_refuses_another_seed) {
    REQUIRE(t->arg_count >= 1);
    char *const root = sw_temp_directory(t);
    REQUIRE(root != nullptr);
    char *const saves = sw_join(t, root, "saves");
    ui_menu_world_t made;
    REQUIRE(ui_menu_catalog_create(saves, "dig", TERRAIN_SEED_DEFAULT, &made) == FILE_OK);
    test_process_t process;
    sw_program_run(t, root, saves, "dig.script", SW_DIG_SCRIPT, &process);
    REQUIRE(process.started != 0u);
    CHECK(process.timed_out == 0u);
    if (!CHECK_EQ_I64(process.exit_code, 0)) {
        printf("%s\n", process.output);
    }
    CHECK_CONTAINS(process.output, "session: world 'dig' is open");
    CHECK_CONTAINS(process.output, " 0 bytes live");

    /* On disk, from here on with no program: terrain as it generates, against what the files put over it. */
    char *const world_directory = sw_join(t, saves, made.directory);
    sw_rig_t reader = {.root = world_directory, .blocks = sw_join(t, world_directory, "blocks")};
    save_world_manifest_header_t header;
    const save_world_region_t *regions = nullptr;
    sw_manifest_read(t, &reader, &header, &regions);
    CHECK_EQ_U64(header.seed, TERRAIN_SEED_DEFAULT);
    CHECK_EQ_U64(header.generation, 1u);
    REQUIRE(header.region_count >= 1u);
    terrain_t *const terrain = sw_aligned(t, sizeof(terrain_t));
    REQUIRE(terrain_init(terrain, TERRAIN_SEED_DEFAULT));
    save_t *const save = sw_aligned(t, sizeof(save_t));
    REQUIRE(save_init(save));
    REQUIRE(save_open(save, world_directory, TERRAIN_SEED_DEFAULT) == SAVE_WORLD_OK);
    uint16_t *const generated = test_alloc(t, SW_COLUMN_BYTES);
    uint16_t *const saved = test_alloc(t, SW_COLUMN_BYTES);
    uint8_t *const opened = test_alloc(t, SW_COLUMN_VOXELS);
    uint64_t differ = 0u;
    uint64_t air_now = 0u;
    uint64_t sections = 0u;
    for (uint32_t r = 0u; r < header.region_count; r++) {
        for (uint32_t local = 0u; local < SAVE_WORLD_REGION_COLUMNS; local++) {
            const int32_t chunk_x =
                regions[r].region_x * (int32_t)SAVE_WORLD_REGION_SIDE + (int32_t)(local % SAVE_WORLD_REGION_SIDE);
            const int32_t chunk_y =
                regions[r].region_y * (int32_t)SAVE_WORLD_REGION_SIDE + (int32_t)(local / SAVE_WORLD_REGION_SIDE);
            memset(saved, 0, SW_COLUMN_BYTES);
            const column_mask_t mask = save_world_column_overlay(&save->world, chunk_x, chunk_y, saved, opened);
            if (!column_mask_any(mask)) {
                continue;
            }
            const terrain_density_box_t box = {.origin_x = chunk_x * (int32_t)COLUMN_SIZE,
                                               .origin_y = chunk_y * (int32_t)COLUMN_SIZE,
                                               .origin_z = COLUMN_Z_STORAGE_MIN,
                                               .count_x = COLUMN_SIZE,
                                               .count_y = COLUMN_SIZE,
                                               .count_z = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN)};
            terrain_generate_box(terrain, &box, opened, generated);
            for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
                if (!column_mask_has(mask, section)) {
                    continue;
                }
                sections++;
                for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
                    const uint64_t at = (uint64_t)section * SECTION_VOXELS + i;
                    if (generated[at] != saved[at]) {
                        differ++;
                        air_now += saved[at] == BLOCK_ID_AIR ? 1u : 0u;
                    }
                }
            }
        }
    }
    save_close(save);
    printf("  %" PRIu64 " sections saved, %" PRIu64 " voxels differ from the terrain, %" PRIu64 " of them dug out\n",
           sections, differ, air_now);
    /* Digging went into the ground, and nothing else changed more than a block or two around it: a section of that is far from all of it. */
    CHECK(sections >= 1u);
    CHECK(air_now >= 1u);
    CHECK(differ < sections * SECTION_VOXELS);

    /* A second run opens what the first left, reads it, and writes nothing new. */
    sw_program_run(t, root, saves, "look.script", SW_LOOK_SCRIPT, &process);
    REQUIRE(process.started != 0u);
    if (!CHECK_EQ_I64(process.exit_code, 0)) {
        printf("%s\n", process.output);
    }
    CHECK_CONTAINS(process.output, "at save 1, with ");
    CHECK_CONTAINS(process.output, " 0 past reading");
    CHECK(strstr(process.output, " 0 sections read back") == nullptr);
    sw_manifest_read(t, &reader, &header, nullptr);
    /* Standing on the ground changes nothing, though time passes and things fall. */
    CHECK(header.generation >= 1u);

    /* The same directory told it is another world: the program stays on the list and says why, the script runs out there, and nothing has been touched. */
    uint64_t manifest_bytes = 0u;
    const uint8_t *const manifest = sw_slurp(t, sw_join(t, reader.blocks, "manifest"), &manifest_bytes);
    char *const meta = sw_join(t, world_directory, "world.meta");
    static const char other_meta[] = "minecraft_c world 1\nseed 987654321\nname dig\n";
    REQUIRE(file_write_atomic(meta, other_meta, sizeof(other_meta) - 1u) == FILE_OK);
    sw_program_run(t, root, saves, "other.script", SW_LOOK_SCRIPT, &process);
    REQUIRE(process.started != 0u);
    CHECK_EQ_I64(process.exit_code, 0);
    CHECK_CONTAINS(process.output, "holds changes to the world of seed");
    CHECK_CONTAINS(process.output, "will not be opened");
    uint64_t after_bytes = 0u;
    const uint8_t *const after = sw_slurp(t, sw_join(t, reader.blocks, "manifest"), &after_bytes);
    REQUIRE(after_bytes == manifest_bytes);
    CHECK_MEM_EQ(after, manifest, manifest_bytes);
    save_deinit(save);
    terrain_deinit(terrain);
    sw_temp_remove(t, root);
}

TEST_MAIN()
