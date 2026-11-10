/* ctest run_save_world_names. See README.md. */
#include "harness/index.h"

#include "file/index.h"
#include "log/index.h"
#include "save/index.h"
#include "tables/blocks/index.h"
#include "world/block_state/index.h"
#include "world/index.h"

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint64_t {
    NW_COLUMN_VOXELS = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS,
    NW_COLUMN_BYTES = NW_COLUMN_VOXELS * sizeof(uint16_t),
    NW_PATH_BYTES = 512u,
    NW_FILE_BYTES = 8ull << 20,
};
enum : uint32_t {
    NW_TABLE_RADIUS = 2u,
    /* A section in the ground, and one up where it is air. Each column below has both. */
    NW_SECTION_LOW = 3u,
    NW_SECTION_HIGH = 40u,
    NW_COLUMNS = 2u,
};
/* Either side of the line between two regions, so that it is two files. */
static const int32_t NW_COLUMN_X[NW_COLUMNS] = {-1, 0};
static const uint64_t NW_SEED = 4242u;

typedef struct nw_rig_t {
    world_t *world;   /* 0 */
    save_t *save;     /* 8 */
    uint16_t *states; /* 16: one column */
    uint8_t *scratch; /* 24 */
    const char *root; /* 32 */
    char *manifest;   /* 40 */
} nw_rig_t;
static_assert(sizeof(nw_rig_t) == 48);

static uint64_t nw_fnv(const void *data, uint64_t bytes) {
    const uint8_t *const cursor = data;
    uint64_t hash = 14695981039346656037ull;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash ^= cursor[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static void *nw_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static nw_rig_t *nw_rig_new(test_context_t *t) {
    nw_rig_t *const rig = test_alloc(t, sizeof(nw_rig_t));
    char *const root = test_alloc(t, NW_PATH_BYTES);
    snprintf(root, NW_PATH_BYTES, "/tmp/mc_save_world_names_XXXXXX");
    if (mkdtemp(root) == nullptr) {
        return nullptr;
    }
    rig->root = root;
    rig->manifest = test_alloc(t, NW_PATH_BYTES);
    snprintf(rig->manifest, NW_PATH_BYTES, "%s/blocks/manifest", root);
    rig->world = nw_aligned(t, sizeof(world_t));
    rig->save = nw_aligned(t, sizeof(save_t));
    rig->states = test_alloc(t, NW_COLUMN_BYTES);
    rig->scratch = test_alloc(t, SAVE_WORLD_OVERLAY_SCRATCH_BYTES);
    return rig;
}

static void nw_rig_remove(test_context_t *t, const nw_rig_t *rig) {
    if (strncmp(rig->root, "/tmp/mc_save_world_names_", 25u) != 0) {
        return;
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", (char *)rig->root, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static uint8_t *nw_slurp(test_context_t *t, const char *path, uint64_t *out_bytes) {
    uint8_t *const bytes = test_alloc(t, NW_FILE_BYTES);
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, NW_FILE_BYTES, out_bytes), FILE_OK)) {
        *out_bytes = 0u;
    }
    return bytes;
}

/* A session: a world of air around the middle, with what is saved over it. */
static bool nw_session_begin(test_context_t *t, nw_rig_t *rig) {
    const uint64_t side = 2ull * NW_TABLE_RADIUS + 1ull;
    const world_limits_t limits = {.section_bytes = 1ull << 28, .column_count = side * side};
    if (!CHECK(save_init(rig->save)) || !CHECK(world_init(rig->world, limits))) {
        return false;
    }
    if (!CHECK_EQ_U64(save_open(rig->save, rig->root, NW_SEED), SAVE_WORLD_OK) ||
        !CHECK(world_open(rig->world, NW_TABLE_RADIUS, 0, 0))) {
        return false;
    }
    world_t *const world = rig->world;
    world_map_request_t request;
    while (world_map_stream_take(&world->map, &request, 1u) == 1u) {
        memset(rig->states, 0, NW_COLUMN_BYTES);
        (void)save_world_column_overlay(&rig->save->world, request.chunk_x, request.chunk_y, rig->states, rig->scratch);
        column_t *const column = world_map_stream_commit(&world->map, &request);
        if (!CHECK(column != nullptr)) {
            return false;
        }
        for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
            if (!CHECK(section_blocks_import(&column->sections[section], &world->section_pool,
                                             rig->states + (uint64_t)section * SECTION_VOXELS))) {
                return false;
            }
        }
        world_edit_column_filled(&world->edit, request.slot);
    }
    return true;
}

static void nw_session_end(nw_rig_t *rig) {
    world_close(rig->world);
    world_deinit(rig->world);
    save_deinit(rig->save);
}

static void nw_voxel_place(uint32_t column, uint32_t section, uint32_t voxel, int32_t *x, int32_t *y, int32_t *z) {
    *x = NW_COLUMN_X[column] * (int32_t)COLUMN_SIZE + (int32_t)(voxel & 15u);
    *y = (int32_t)((voxel >> 4) & 15u);
    *z = COLUMN_Z_STORAGE_MIN + (int32_t)(section * SECTION_SIZE + (voxel >> 8));
}

/* What this test puts at a voxel, in one list's numbers: every number the list has, again and again, differently in each place. */
static uint16_t nw_voxel_state(uint32_t column, uint32_t section, uint32_t voxel, uint32_t count) {
    return (uint16_t)((voxel + column * 7u + section * 13u) % count);
}

/* A world saved in a build whose table has count names: each number written is that build's number. Only numbers below count, which this build's table also has. */
static void nw_world_write(test_context_t *t, nw_rig_t *rig, uint32_t count) {
    if (!nw_session_begin(t, rig)) {
        return;
    }
    world_edit_t *const edit = &rig->world->edit;
    static const uint32_t sections[2] = {NW_SECTION_LOW, NW_SECTION_HIGH};
    for (uint32_t column = 0u; column < NW_COLUMNS; column++) {
        for (uint32_t which = 0u; which < 2u; which++) {
            for (uint32_t voxel = 0u; voxel < SECTION_VOXELS; voxel++) {
                int32_t x = 0, y = 0, z = 0;
                nw_voxel_place(column, sections[which], voxel, &x, &y, &z);
                const world_edit_status_t status =
                    world_edit_block_set(edit, x, y, z, nw_voxel_state(column, sections[which], voxel, count));
                CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED);
            }
        }
    }
    CHECK_EQ_U64(save_world_flush(&rig->save->world, edit), SAVE_WORLD_OK);
    nw_session_end(rig);
}

/* This build's table without one name, as the list of a build from before that name was added. */
static uint64_t nw_list_without(uint32_t skip, uint8_t *out, const char **out_names) {
    uint64_t at = sizeof(save_names_head_t);
    uint32_t count = 0u;
    for (uint32_t id = 0u; id < BLOCK_COUNT; id++) {
        if (id == skip) {
            continue;
        }
        const uint64_t bytes = strlen(block_defs[id].name) + 1u;
        memcpy(out + at, block_defs[id].name, bytes);
        out_names[count] = block_defs[id].name;
        count++;
        at += bytes;
    }
    const save_names_head_t head = {.count = count, .bytes = (uint32_t)(at - sizeof(save_names_head_t))};
    memcpy(out, &head, sizeof(head));
    while ((at & 7u) != 0u) {
        out[at] = 0u;
        at++;
    }
    return at;
}

/* The manifest with this list for its own, and each checksum made to agree: what an older build would have written. */
static void nw_manifest_list_set(test_context_t *t, const nw_rig_t *rig, const uint8_t *list, uint64_t list_bytes) {
    uint64_t bytes = 0u;
    uint8_t *const file = nw_slurp(t, rig->manifest, &bytes);
    save_world_manifest_header_t header;
    memcpy(&header, file, sizeof(header));
    const uint64_t names_at = sizeof(header) + (uint64_t)header.region_count * sizeof(save_world_region_t);
    memcpy(file + names_at, list, list_bytes);
    const uint64_t after = names_at + list_bytes;
    save_names_head_t head;
    memcpy(&head, list, sizeof(head));
    header.table_hash = nw_fnv(list + sizeof(head), head.bytes);
    header.regions_checksum = nw_fnv(file + sizeof(header), after - sizeof(header));
    header.checksum = nw_fnv(&header, offsetof(save_world_manifest_header_t, checksum));
    memcpy(file, &header, sizeof(header));
    CHECK_EQ_U64(file_write_atomic(rig->manifest, file, after), FILE_OK);
}

/* The name at each place of a world's list as it is on the disk. Returns how many. */
static uint32_t nw_manifest_list_read(test_context_t *t, const nw_rig_t *rig, const char **out_names) {
    uint64_t bytes = 0u;
    const uint8_t *const file = nw_slurp(t, rig->manifest, &bytes);
    save_world_manifest_header_t header;
    memcpy(&header, file, sizeof(header));
    const uint64_t names_at = sizeof(header) + (uint64_t)header.region_count * sizeof(save_world_region_t);
    save_names_view_t view;
    if (!CHECK(save_names_read(file + names_at, bytes - names_at, &view))) {
        return 0u;
    }
    CHECK_EQ_U64(view.total_bytes, bytes - names_at);
    const char *name = view.borrowed_names;
    for (uint32_t i = 0u; i < view.count; i++) {
        out_names[i] = name;
        name = save_names_next(name);
    }
    return view.count;
}

/* Every voxel of the world this test wrote, read from the disk through an open save, against what each number in the saved list meant. A name the build has not got is
 * air, and the rest of each column is what generates there, which here is air too. */
static void nw_world_check(test_context_t *t, nw_rig_t *rig, const char **saved_names, uint32_t saved_count,
                           const char *gone) {
    save_world_t *const save = &rig->save->world;
    for (uint32_t column = 0u; column < NW_COLUMNS; column++) {
        memset(rig->states, 0, NW_COLUMN_BYTES);
        (void)save_world_column_overlay(save, NW_COLUMN_X[column], 0, rig->states, rig->scratch);
        for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
            const bool written = section == NW_SECTION_LOW || section == NW_SECTION_HIGH;
            for (uint32_t voxel = 0u; voxel < SECTION_VOXELS; voxel++) {
                const uint16_t state = rig->states[(uint64_t)section * SECTION_VOXELS + voxel];
                const uint16_t saved = nw_voxel_state(column, section, voxel, saved_count);
                const char *const meant =
                    written ? (gone != nullptr && strcmp(saved_names[saved], gone) == 0 ? "air" : saved_names[saved])
                            : "air";
                if (!CHECK(state < BLOCK_COUNT) || !CHECK(strcmp(block_defs[state].name, meant) == 0)) {
                    printf("  column %" PRIu32 " section %" PRIu32 " voxel %" PRIu32 ": %" PRIu32
                           ", and it should be %s\n",
                           column, section, voxel, (uint32_t)state, meant);
                    return;
                }
            }
        }
    }
    CHECK_EQ_U64(atomic_load(&save->sections_damaged), 0u);
}

/* One edit, by a build with its own table, and a save. */
static void nw_edit_and_save(test_context_t *t, nw_rig_t *rig, int32_t x, int32_t y, int32_t z, uint16_t state) {
    if (nw_session_begin(t, rig)) {
        const world_edit_status_t status = world_edit_block_set(&rig->world->edit, x, y, z, state);
        CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED);
        CHECK_EQ_U64(save_world_flush(&rig->save->world, &rig->world->edit), SAVE_WORLD_OK);
        nw_session_end(rig);
    }
}

static bool nw_open_only(test_context_t *t, nw_rig_t *rig) {
    return CHECK(save_init(rig->save)) && CHECK_EQ_U64(save_open(rig->save, rig->root, NW_SEED), SAVE_WORLD_OK);
}

/* ---- A list's bytes ------------------------------------------------------------------------------------------------- */

TEST(a_list_is_these_bytes_by_hand_and_nothing_else_is_a_list) {
    static const uint8_t good[24] = {2u,  0u,  0u,  0u,  10u, 0u, 0u, 0u, 'a', 'i', 'r', 0u,
                                     's', 't', 'o', 'n', 'e', 0u, 0u, 0u, 0u,  0u,  0u,  0u};
    save_names_view_t view;
    REQUIRE(save_names_read(good, sizeof(good), &view));
    CHECK_EQ_U64(view.count, 2u);
    CHECK_EQ_U64(view.names_bytes, 10u);
    CHECK_EQ_U64(view.total_bytes, 24u);
    CHECK(strcmp(view.borrowed_names, "air") == 0);
    CHECK(strcmp(save_names_next(view.borrowed_names), "stone") == 0);
    /* Anything after it is someone else's: only the list's own length is its. */
    uint8_t longer[32];
    memset(longer, 0xAAu, sizeof(longer));
    memcpy(longer, good, sizeof(good));
    CHECK(save_names_read(longer, sizeof(longer), &view));
    CHECK_EQ_U64(view.total_bytes, 24u);

    uint8_t copy[24];
    /* Cut short, at every length. */
    for (uint32_t bytes = 0u; bytes < sizeof(good); bytes++) {
        CHECK(!save_names_read(good, bytes, &view));
    }
    /* Counts that do not match, past the most, and lengths past the most. */
    memcpy(copy, good, sizeof(copy));
    copy[0] = 3u;
    CHECK(!save_names_read(copy, sizeof(copy), &view));
    memcpy(copy, good, sizeof(copy));
    copy[0] = 1u;
    CHECK(!save_names_read(copy, sizeof(copy), &view));
    memcpy(copy, good, sizeof(copy));
    copy[3] = 0x7Fu;
    CHECK(!save_names_read(copy, sizeof(copy), &view));
    memcpy(copy, good, sizeof(copy));
    copy[7] = 0xFFu;
    CHECK(!save_names_read(copy, sizeof(copy), &view));
    /* A name with no end, an empty name, one with what no name has, and something where its padding goes. */
    memcpy(copy, good, sizeof(copy));
    copy[17] = 'x';
    CHECK(!save_names_read(copy, sizeof(copy), &view));
    memcpy(copy, good, sizeof(copy));
    copy[8] = 0u;
    CHECK(!save_names_read(copy, sizeof(copy), &view));
    memcpy(copy, good, sizeof(copy));
    copy[9] = '/';
    CHECK(!save_names_read(copy, sizeof(copy), &view));
    memcpy(copy, good, sizeof(copy));
    copy[23] = 1u;
    CHECK(!save_names_read(copy, sizeof(copy), &view));

    /* This build's own table, round and round. */
    uint8_t *const list = test_alloc(t, SAVE_NAMES_TABLE_BYTES_MAX);
    const uint64_t bytes = save_names_write(SAVE_NAMES_KIND_BLOCKS, list);
    REQUIRE(save_names_read(list, bytes, &view));
    CHECK_EQ_U64(view.count, BLOCK_COUNT);
    CHECK_EQ_U64(view.total_bytes, bytes);
    CHECK_EQ_U64(nw_fnv(view.borrowed_names, view.names_bytes), save_world_table_hash());
    uint16_t *const map = test_alloc(t, SAVE_NAMES_MAP_BYTES);
    CHECK_EQ_U64(save_names_map(&view, SAVE_NAMES_KIND_BLOCKS, map), 0u);
    for (uint32_t id = 0u; id < BLOCK_COUNT; id++) {
        CHECK_EQ_U64(map[id], id);
    }
    uint8_t *const all = test_alloc(t, (uint64_t)SAVE_NAMES_KIND_COUNT * SAVE_NAMES_TABLE_BYTES_MAX);
    const uint64_t all_bytes = save_names_write_all(all);
    CHECK(save_names_are_live(all, all_bytes));
    CHECK(!save_names_are_live(all, all_bytes - 8u));
    all[sizeof(save_names_head_t)] ^= 1u;
    CHECK(!save_names_are_live(all, all_bytes));
}

/* ---- A new block ---------------------------------------------------------------------------------------------------- */

/* The whole of it, with the new block at this place in the table. */
static void nw_new_block_test(test_context_t *t, uint32_t new_id) {
    nw_rig_t *const rig = nw_rig_new(t);
    REQUIRE(rig != nullptr);
    uint8_t *const list = test_alloc(t, SAVE_NAMES_TABLE_BYTES_MAX);
    const char **const old_names = test_alloc(t, BLOCK_COUNT * sizeof(const char *));
    const uint64_t list_bytes = nw_list_without(new_id, list, old_names);
    const uint32_t old_count = BLOCK_COUNT - 1u;

    /* Made in a build from before that block: every number it wrote means that build's table. */
    nw_world_write(t, rig, old_count);
    nw_manifest_list_set(t, rig, list, list_bytes);
    uint64_t before_bytes = 0u;
    const uint8_t *const before = nw_slurp(t, rig->manifest, &before_bytes);

    /* Opened in this one: every voxel is the block it was. */
    REQUIRE(nw_open_only(t, rig));
    CHECK_EQ_U64(rig->save->world.names_same,
                 new_id == BLOCK_COUNT - 1u ? 1u : 0u); /* last is where it would go anyway */
    CHECK_EQ_U64(rig->save->world.names_count, BLOCK_COUNT);
    nw_world_check(t, rig, old_names, old_count, nullptr);
    save_deinit(rig->save);
    /* And opening it changed nothing there. */
    uint64_t after_bytes = 0u;
    const uint8_t *const after = nw_slurp(t, rig->manifest, &after_bytes);
    REQUIRE(after_bytes == before_bytes);
    CHECK_MEM_EQ(after, before, before_bytes);

    /* Played: the new block is put down, in a section that has old numbers and another that is new, and saved. */
    int32_t x = 0, y = 0, z = 0;
    nw_voxel_place(0u, COLUMN_SECTIONS - 3u, 5u, &x, &y, &z);
    nw_edit_and_save(t, rig, x, y, z, (uint16_t)new_id);

    /* Its name went on the end of the world's list, and no number moved. */
    const char **const names = test_alloc(t, (BLOCK_COUNT + 1u) * sizeof(const char *));
    REQUIRE(nw_manifest_list_read(t, rig, names) == BLOCK_COUNT);
    CHECK(strcmp(names[old_count], block_defs[new_id].name) == 0);
    for (uint32_t i = 0u; i < old_count; i++) {
        CHECK(strcmp(names[i], old_names[i]) == 0);
    }

    /* Opened again: the old blocks, with the new one where it was put. */
    REQUIRE(nw_open_only(t, rig));
    memset(rig->states, 0, NW_COLUMN_BYTES);
    (void)save_world_column_overlay(&rig->save->world, NW_COLUMN_X[0], 0, rig->states, rig->scratch);
    CHECK_EQ_U64(rig->states[(uint64_t)(COLUMN_SECTIONS - 3u) * SECTION_VOXELS + 5u], new_id);
    rig->states[(uint64_t)(COLUMN_SECTIONS - 3u) * SECTION_VOXELS + 5u] = 0u;
    for (uint32_t voxel = 0u; voxel < SECTION_VOXELS; voxel += 97u) {
        const uint16_t saved = nw_voxel_state(0u, NW_SECTION_LOW, voxel, old_count);
        const uint16_t state = rig->states[(uint64_t)NW_SECTION_LOW * SECTION_VOXELS + voxel];
        CHECK(strcmp(block_defs[state].name, old_names[saved]) == 0);
    }
    CHECK_EQ_U64(atomic_load(&rig->save->world.sections_damaged), 0u);
    save_deinit(rig->save);
    nw_rig_remove(t, rig);
}

TEST(a_block_added_first_in_the_table_moves_every_id_and_every_block_stays) {
    nw_new_block_test(t, 1u); /* 0 is always air */
}

TEST(a_block_added_in_the_middle_of_the_table_keeps_every_block) {
    nw_new_block_test(t, BLOCK_COUNT / 2u);
}

TEST(a_block_added_last_in_the_table_keeps_every_block) {
    nw_new_block_test(t, BLOCK_COUNT - 1u);
}

/* What is on its way to a file is in the world's numbers too, and a reader gets this build's. */
TEST(sections_not_yet_written_read_right_in_a_world_with_other_numbers) {
    nw_rig_t *const rig = nw_rig_new(t);
    REQUIRE(rig != nullptr);
    uint8_t *const list = test_alloc(t, SAVE_NAMES_TABLE_BYTES_MAX);
    const char **const old_names = test_alloc(t, BLOCK_COUNT * sizeof(const char *));
    const uint64_t list_bytes = nw_list_without(1u, list, old_names);
    nw_world_write(t, rig, BLOCK_COUNT - 1u);
    nw_manifest_list_set(t, rig, list, list_bytes);
    REQUIRE(nw_session_begin(t, rig));
    int32_t x = 0, y = 0, z = 0;
    nw_voxel_place(1u, NW_SECTION_LOW, 9u, &x, &y, &z);
    const uint16_t stone = BLOCK_ID_STONE;
    world_edit_t *const edit = &rig->world->edit;
    CHECK_EQ_U64(world_edit_block_set(edit, x, y, z, stone), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_U64(save_world_capture_dirty(&rig->save->world, edit), SAVE_WORLD_OK);
    memset(rig->states, 0, NW_COLUMN_BYTES);
    (void)save_world_column_overlay(&rig->save->world, NW_COLUMN_X[1], 0, rig->states, rig->scratch);
    CHECK_EQ_U64(rig->states[(uint64_t)NW_SECTION_LOW * SECTION_VOXELS + 9u], stone);
    nw_session_end(rig);
    nw_rig_remove(t, rig);
}

/* ---- A block taken away ---------------------------------------------------------------------------------------------- */

TEST(a_block_gone_from_the_table_reads_as_air_the_log_says_so_and_only_a_save_makes_it_so) {
    nw_rig_t *const rig = nw_rig_new(t);
    REQUIRE(rig != nullptr);
    /* Stone is in the world's list as what no build now has: the same places, so every other number means what it did. */
    uint8_t *const list = test_alloc(t, SAVE_NAMES_TABLE_BYTES_MAX);
    const char **const names = test_alloc(t, BLOCK_COUNT * sizeof(const char *));
    uint64_t at = sizeof(save_names_head_t);
    const char *const gone = "gone_block";
    for (uint32_t id = 0u; id < BLOCK_COUNT; id++) {
        const char *const name = id == BLOCK_ID_STONE ? gone : block_defs[id].name;
        memcpy(list + at, name, strlen(name) + 1u);
        names[id] = name;
        at += strlen(name) + 1u;
    }
    const save_names_head_t head = {.count = BLOCK_COUNT, .bytes = (uint32_t)(at - sizeof(save_names_head_t))};
    memcpy(list, &head, sizeof(head));
    while ((at & 7u) != 0u) {
        list[at] = 0u;
        at++;
    }
    nw_world_write(t, rig, BLOCK_COUNT);
    nw_manifest_list_set(t, rig, list, at);

    /* The log, for what it says. */
    char log_path[NW_PATH_BYTES];
    snprintf(log_path, sizeof(log_path), "%s/log", rig->root);
    const int32_t log_fd = open(log_path, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    REQUIRE(log_fd >= 0);
    const int32_t previous = log_set_fd(log_fd);
    const bool opened = nw_open_only(t, rig);
    (void)log_set_fd(previous);
    (void)close(log_fd);
    REQUIRE(opened);
    uint64_t said_bytes = 0u;
    char *const said = (char *)nw_slurp(t, log_path, &said_bytes);
    said[said_bytes] = '\0';
    CHECK(strstr(said, "gone_block") != nullptr);
    CHECK(strstr(said, "air") != nullptr);
    /* Stone is still a block here, so it joins the end of the list as a new name. */
    CHECK_EQ_U64(rig->save->world.names_count, BLOCK_COUNT + 1u);
    nw_world_check(t, rig, names, BLOCK_COUNT, gone);
    save_deinit(rig->save);

    /* Something else in the one column's section changes, and it is saved: air is what it holds now. The other region's file is not touched. */
    int32_t x = 0, y = 0, z = 0;
    nw_voxel_place(0u, NW_SECTION_LOW, 0u, &x, &y, &z);
    nw_edit_and_save(t, rig, x, y, z, BLOCK_ID_DIRT);
    const char **const after = test_alloc(t, (BLOCK_COUNT + 2u) * sizeof(const char *));
    const uint32_t after_count = nw_manifest_list_read(t, rig, after);
    CHECK_EQ_U64(after_count, BLOCK_COUNT + 1u);
    CHECK(strcmp(after[BLOCK_ID_STONE], gone) == 0);

    /* A build that has that name back: what no save has touched is what it was. Its number has not moved, so the list only has to say so again. */
    uint64_t bytes = 0u;
    uint8_t *const manifest = nw_slurp(t, rig->manifest, &bytes);
    save_world_manifest_header_t header;
    memcpy(&header, manifest, sizeof(header));
    const uint64_t names_at = sizeof(header) + (uint64_t)header.region_count * sizeof(save_world_region_t);
    uint64_t back = sizeof(save_names_head_t);
    for (uint32_t id = 0u; id < BLOCK_COUNT; id++) {
        memcpy(list + back, block_defs[id].name, strlen(block_defs[id].name) + 1u);
        back += strlen(block_defs[id].name) + 1u;
    }
    const save_names_head_t back_head = {.count = BLOCK_COUNT, .bytes = (uint32_t)(back - sizeof(save_names_head_t))};
    memcpy(list, &back_head, sizeof(back_head));
    while ((back & 7u) != 0u) {
        list[back] = 0u;
        back++;
    }
    REQUIRE(names_at < bytes);
    nw_manifest_list_set(t, rig, list, back);
    REQUIRE(nw_open_only(t, rig));
    CHECK_EQ_U64(rig->save->world.names_same, 1u);
    memset(rig->states, 0, NW_COLUMN_BYTES);
    (void)save_world_column_overlay(&rig->save->world, NW_COLUMN_X[1], 0, rig->states, rig->scratch);
    uint32_t stones = 0u;
    for (uint32_t voxel = 0u; voxel < SECTION_VOXELS; voxel++) {
        const uint16_t state = rig->states[(uint64_t)NW_SECTION_LOW * SECTION_VOXELS + voxel];
        CHECK_EQ_U64(state, nw_voxel_state(1u, NW_SECTION_LOW, voxel, BLOCK_COUNT));
        stones += state == BLOCK_ID_STONE ? 1u : 0u;
    }
    CHECK(stones != 0u);
    save_deinit(rig->save);
    nw_rig_remove(t, rig);
}

/* ---- What is refused -------------------------------------------------------------------------------------------------- */

static void nw_manifest_forge(test_context_t *t, const nw_rig_t *rig, uint64_t offset, const void *value,
                              uint64_t bytes) {
    uint64_t length = 0u;
    uint8_t *const file = nw_slurp(t, rig->manifest, &length);
    memcpy(file + offset, value, bytes);
    save_world_manifest_header_t header;
    memcpy(&header, file, sizeof(header));
    header.regions_checksum = nw_fnv(file + sizeof(header), length - sizeof(header));
    header.checksum = nw_fnv(&header, offsetof(save_world_manifest_header_t, checksum));
    memcpy(file, &header, sizeof(header));
    CHECK_EQ_U64(file_write_atomic(rig->manifest, file, length), FILE_OK);
}

TEST(a_world_in_the_old_format_is_refused_says_why_and_is_left_as_it_was) {
    nw_rig_t *const rig = nw_rig_new(t);
    REQUIRE(rig != nullptr);
    nw_world_write(t, rig, BLOCK_COUNT);
    const uint32_t older = SAVE_WORLD_VERSION_FIRST_WITH_NAMES - 1u;
    nw_manifest_forge(t, rig, offsetof(save_world_manifest_header_t, version), &older, sizeof(older));
    uint64_t before_bytes = 0u;
    const uint8_t *const before = nw_slurp(t, rig->manifest, &before_bytes);

    char log_path[NW_PATH_BYTES];
    snprintf(log_path, sizeof(log_path), "%s/log", rig->root);
    const int32_t log_fd = open(log_path, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    REQUIRE(log_fd >= 0);
    const int32_t previous = log_set_fd(log_fd);
    REQUIRE(save_init(rig->save));
    CHECK_EQ_U64(save_open(rig->save, rig->root, NW_SEED), SAVE_WORLD_ERROR_VERSION);
    (void)log_set_fd(previous);
    (void)close(log_fd);
    save_deinit(rig->save);
    uint64_t said_bytes = 0u;
    char *const said = (char *)nw_slurp(t, log_path, &said_bytes);
    said[said_bytes] = '\0';
    CHECK(strstr(said, "before a world kept the names of its blocks") != nullptr);
    CHECK(strstr(said, "left as it was") != nullptr);

    uint64_t after_bytes = 0u;
    const uint8_t *const after = nw_slurp(t, rig->manifest, &after_bytes);
    REQUIRE(after_bytes == before_bytes);
    CHECK_MEM_EQ(after, before, before_bytes);
    nw_rig_remove(t, rig);
}

TEST(a_list_that_cannot_be_believed_stops_the_world) {
    nw_rig_t *const rig = nw_rig_new(t);
    REQUIRE(rig != nullptr);
    nw_world_write(t, rig, BLOCK_COUNT);
    uint64_t good_bytes = 0u;
    const uint8_t *const good = nw_slurp(t, rig->manifest, &good_bytes);
    save_world_manifest_header_t header;
    memcpy(&header, good, sizeof(header));
    const uint64_t names_at = sizeof(header) + (uint64_t)header.region_count * sizeof(save_world_region_t);
    const int32_t quiet = log_set_fd(open("/dev/null", O_WRONLY | O_CLOEXEC));

    /* A name with what no name has. */
    const uint8_t slash = '/';
    nw_manifest_forge(t, rig, names_at + sizeof(save_names_head_t), &slash, sizeof(slash));
    REQUIRE(save_init(rig->save));
    CHECK_EQ_U64(save_open(rig->save, rig->root, NW_SEED), SAVE_WORLD_ERROR_DAMAGED);
    CHECK_EQ_U64(file_write_atomic(rig->manifest, good, good_bytes), FILE_OK);
    /* A count that is not what the list holds. */
    const uint32_t more = BLOCK_COUNT + 1u;
    nw_manifest_forge(t, rig, names_at, &more, sizeof(more));
    CHECK_EQ_U64(save_open(rig->save, rig->root, NW_SEED), SAVE_WORLD_ERROR_DAMAGED);
    CHECK_EQ_U64(file_write_atomic(rig->manifest, good, good_bytes), FILE_OK);
    /* No list at all: the file stops where the regions do. */
    uint8_t *const cut = test_alloc(t, names_at);
    memcpy(cut, good, names_at);
    save_world_manifest_header_t cut_header = header;
    cut_header.regions_checksum = nw_fnv(cut + sizeof(header), names_at - sizeof(header));
    cut_header.checksum = nw_fnv(&cut_header, offsetof(save_world_manifest_header_t, checksum));
    memcpy(cut, &cut_header, sizeof(cut_header));
    CHECK_EQ_U64(file_write_atomic(rig->manifest, cut, names_at), FILE_OK);
    CHECK_EQ_U64(save_open(rig->save, rig->root, NW_SEED), SAVE_WORLD_ERROR_DAMAGED);
    CHECK_EQ_U64(file_write_atomic(rig->manifest, good, good_bytes), FILE_OK);
    /* A list that is not the one its hash says. */
    const uint64_t other_hash = header.table_hash ^ 1u;
    nw_manifest_forge(t, rig, offsetof(save_world_manifest_header_t, table_hash), &other_hash, sizeof(other_hash));
    CHECK_EQ_U64(save_open(rig->save, rig->root, NW_SEED), SAVE_WORLD_ERROR_TABLES);
    CHECK_EQ_U64(file_write_atomic(rig->manifest, good, good_bytes), FILE_OK);

    /* And the same table as this build's: both numberings are one, and what a record holds is this build's number. */
    save_deinit(rig->save);
    REQUIRE(nw_open_only(t, rig));
    CHECK_EQ_U64(rig->save->world.names_same, 1u);
    CHECK_EQ_U64(rig->save->world.names_count, BLOCK_COUNT);
    save_deinit(rig->save);
    const int32_t was_quiet = log_set_fd(quiet);
    (void)close(was_quiet);
    nw_rig_remove(t, rig);
}

/* A number is believed against the list the file has, not this build's table: past that it costs its section, as any bad record does. */
TEST(a_record_is_held_to_the_length_of_its_list) {
    uint16_t *const palette_slots = test_alloc(t, 65536u * sizeof(uint16_t));
    uint16_t *const states = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint16_t *const back = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const record = test_alloc(t, SAVE_WORLD_RECORD_BYTES_MAX);
    memset(palette_slots, 0, 65536u * sizeof(uint16_t));
    /* In a palette, with one past this build's table. */
    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
        states[i] = i < 7u ? (uint16_t)BLOCK_COUNT : 3u;
    }
    uint32_t bytes = save_world_record_encode(states, palette_slots, record);
    CHECK(!save_world_record_decode(record, bytes, back));
    CHECK(!save_world_record_decode_within(record, bytes, BLOCK_COUNT, back));
    REQUIRE(save_world_record_decode_within(record, bytes, BLOCK_COUNT + 1u, back));
    CHECK_MEM_EQ(back, states, SECTION_VOXELS * sizeof(uint16_t));
    /* And the 4096 numbers themselves, in the form a palette gives way to. */
    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
        states[i] = (uint16_t)(i % 1000u);
    }
    bytes = save_world_record_encode(states, palette_slots, record);
    REQUIRE(bytes == SAVE_WORLD_RECORD_BYTES_MAX);
    CHECK(!save_world_record_decode_within(record, bytes, 999u, back));
    REQUIRE(save_world_record_decode_within(record, bytes, 1000u, back));
    CHECK_MEM_EQ(back, states, SECTION_VOXELS * sizeof(uint16_t));
}

/* ---- The state beside each id (plan 005 phase 12) ------------------------------------------------------------------------ */

TEST(a_saved_world_keeps_every_facing_and_a_block_keeps_only_the_state_it_declares) {
    nw_rig_t *const rig = nw_rig_new(t);
    REQUIRE(rig != nullptr);
    REQUIRE(nw_session_begin(t, rig));
    world_edit_t *const edit = &rig->world->edit;
    /* Every block that turns, each way, one after another along the section, and then stone given a facing it does not have. */
    uint16_t *const written = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint32_t count = 0u;
    for (uint32_t id = 0u; id < BLOCK_COUNT; id++) {
        for (uint32_t facing = 0u; facing < BLOCK_FACINGS && block_state_turns(id); facing++) {
            written[count++] = block_state_make(id, facing, false, false);
        }
    }
    REQUIRE(count >= 4u * 6u);
    const uint32_t stone_at = count;
    written[count++] = block_state_make(BLOCK_ID_STONE, BLOCK_FACING_EAST, false, false);
    for (uint32_t voxel = 0u; voxel < count; voxel++) {
        int32_t x = 0, y = 0, z = 0;
        nw_voxel_place(0u, NW_SECTION_LOW, voxel, &x, &y, &z);
        CHECK_EQ_U64(world_edit_block_set(edit, x, y, z, written[voxel]), WORLD_EDIT_STATUS_CHANGED);
    }
    CHECK_EQ_U64(save_world_flush(&rig->save->world, edit), SAVE_WORLD_OK);
    nw_session_end(rig);

    REQUIRE(nw_open_only(t, rig));
    memset(rig->states, 0, NW_COLUMN_BYTES);
    (void)save_world_column_overlay(&rig->save->world, NW_COLUMN_X[0], 0, rig->states, rig->scratch);
    for (uint32_t voxel = 0u; voxel < count; voxel++) {
        const uint16_t expected = voxel == stone_at ? (uint16_t)BLOCK_ID_STONE : written[voxel];
        CHECK_EQ_U64(rig->states[(uint64_t)NW_SECTION_LOW * SECTION_VOXELS + voxel], expected);
    }
    CHECK_EQ_U64(atomic_load(&rig->save->world.sections_damaged), 0u);
    save_deinit(rig->save);
    nw_rig_remove(t, rig);
}

/* Each delta file of a world, as an older build would have written it: its header's version, and the checksum made to agree. */
static void nw_deltas_version_set(test_context_t *t, const nw_rig_t *rig, uint32_t version) {
    char directory[NW_PATH_BYTES];
    snprintf(directory, sizeof(directory), "%s/blocks", rig->root);
    DIR *const listing = opendir(directory);
    REQUIRE(listing != nullptr);
    uint32_t files = 0u;
    for (const struct dirent *entry = readdir(listing); entry != nullptr; entry = readdir(listing)) {
        if (strncmp(entry->d_name, "r.", 2u) != 0) {
            continue;
        }
        char path[2u * NW_PATH_BYTES];
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        uint64_t bytes = 0u;
        uint8_t *const file = nw_slurp(t, path, &bytes);
        save_world_delta_header_t header;
        memcpy(&header, file, sizeof(header));
        header.version = version;
        header.checksum = nw_fnv(&header, offsetof(save_world_delta_header_t, checksum));
        memcpy(file, &header, sizeof(header));
        CHECK_EQ_U64(file_write_atomic(path, file, bytes), FILE_OK);
        files++;
    }
    closedir(listing);
    CHECK_EQ_U64(files, NW_COLUMNS);
}

TEST(a_world_from_before_states_opens_with_every_block_as_it_was) {
    nw_rig_t *const rig = nw_rig_new(t);
    REQUIRE(rig != nullptr);
    nw_world_write(t, rig, BLOCK_COUNT);
    const uint32_t older = SAVE_WORLD_VERSION_FIRST_WITH_STATE - 1u;
    nw_manifest_forge(t, rig, offsetof(save_world_manifest_header_t, version), &older, sizeof(older));
    nw_deltas_version_set(t, rig, older);
    const char **const names = test_alloc(t, BLOCK_COUNT * sizeof(const char *));
    for (uint32_t id = 0u; id < BLOCK_COUNT; id++) {
        names[id] = block_defs[id].name;
    }
    /* Every block reads as it was saved, facing north as every block did, stairs and benches among them. */
    REQUIRE(nw_open_only(t, rig));
    CHECK_EQ_U64(rig->save->world.names_same, 1u);
    nw_world_check(t, rig, names, BLOCK_COUNT, nullptr);
    save_deinit(rig->save);

    /* A change in one region and a save: the manifest is the new version, the old file of the other region is read as it is. */
    int32_t x = 0, y = 0, z = 0;
    nw_voxel_place(0u, NW_SECTION_LOW, 5u, &x, &y, &z);
    const uint16_t turned = block_state_make(BLOCK_ID_OAK_STAIRS, BLOCK_FACING_WEST, false, false);
    nw_edit_and_save(t, rig, x, y, z, turned);
    uint64_t bytes = 0u;
    const uint8_t *const manifest = nw_slurp(t, rig->manifest, &bytes);
    save_world_manifest_header_t header;
    memcpy(&header, manifest, sizeof(header));
    CHECK_EQ_U64(header.version, SAVE_WORLD_VERSION);
    REQUIRE(nw_open_only(t, rig));
    memset(rig->states, 0, NW_COLUMN_BYTES);
    (void)save_world_column_overlay(&rig->save->world, NW_COLUMN_X[0], 0, rig->states, rig->scratch);
    CHECK_EQ_U64(rig->states[(uint64_t)NW_SECTION_LOW * SECTION_VOXELS + 5u], turned);
    memset(rig->states, 0, NW_COLUMN_BYTES);
    (void)save_world_column_overlay(&rig->save->world, NW_COLUMN_X[1], 0, rig->states, rig->scratch);
    for (uint32_t voxel = 0u; voxel < SECTION_VOXELS; voxel++) {
        CHECK_EQ_U64(rig->states[(uint64_t)NW_SECTION_HIGH * SECTION_VOXELS + voxel],
                     nw_voxel_state(1u, NW_SECTION_HIGH, voxel, BLOCK_COUNT));
    }
    CHECK_EQ_U64(atomic_load(&rig->save->world.sections_damaged), 0u);
    save_deinit(rig->save);
    nw_rig_remove(t, rig);
}

/* A world from a build with the four torches on walls: this build's list with one of their names where wall_torch is. Every voxel of
 * that place is then that torch, which is wall_torch facing its wall. */
TEST(each_old_torch_on_a_wall_reads_as_a_wall_torch_facing_its_wall) {
    static const char *const old_names[BLOCK_FACINGS] = {"torch_wall_south", "torch_wall_west", "torch_wall_north",
                                                         "torch_wall_east"};
    for (uint32_t facing = 0u; facing < BLOCK_FACINGS; facing++) {
        nw_rig_t *const rig = nw_rig_new(t);
        REQUIRE(rig != nullptr);
        nw_world_write(t, rig, BLOCK_COUNT);
        uint8_t *const list = test_alloc(t, SAVE_NAMES_TABLE_BYTES_MAX);
        uint64_t at = sizeof(save_names_head_t);
        for (uint32_t id = 0u; id < BLOCK_COUNT; id++) {
            const char *const name = id == BLOCK_ID_WALL_TORCH ? old_names[facing] : block_defs[id].name;
            memcpy(list + at, name, strlen(name) + 1u);
            at += strlen(name) + 1u;
        }
        const save_names_head_t head = {.count = BLOCK_COUNT, .bytes = (uint32_t)(at - sizeof(save_names_head_t))};
        memcpy(list, &head, sizeof(head));
        while ((at & 7u) != 0u) {
            list[at] = 0u;
            at++;
        }
        nw_manifest_list_set(t, rig, list, at);
        const int32_t quiet = log_set_fd(open("/dev/null", O_WRONLY | O_CLOEXEC));
        REQUIRE(nw_open_only(t, rig));
        /* Facing north is no state at all, so that one list is this build's numbering under another name. */
        CHECK_EQ_U64(rig->save->world.names_same, facing == BLOCK_FACING_NORTH ? 1u : 0u);
        uint32_t torches = 0u;
        for (uint32_t column = 0u; column < NW_COLUMNS; column++) {
            memset(rig->states, 0, NW_COLUMN_BYTES);
            (void)save_world_column_overlay(&rig->save->world, NW_COLUMN_X[column], 0, rig->states, rig->scratch);
            for (uint32_t voxel = 0u; voxel < SECTION_VOXELS; voxel++) {
                const uint16_t saved = nw_voxel_state(column, NW_SECTION_LOW, voxel, BLOCK_COUNT);
                const uint16_t expected =
                    saved == BLOCK_ID_WALL_TORCH ? block_state_make(BLOCK_ID_WALL_TORCH, facing, false, false) : saved;
                CHECK_EQ_U64(rig->states[(uint64_t)NW_SECTION_LOW * SECTION_VOXELS + voxel], expected);
                torches += saved == BLOCK_ID_WALL_TORCH ? 1u : 0u;
            }
        }
        CHECK(torches != 0u);
        save_deinit(rig->save);

        /* A wall torch placed by this build, facing east, is saved under its own name, and both read back as they are. */
        int32_t x = 0, y = 0, z = 0;
        nw_voxel_place(0u, NW_SECTION_LOW, 3u, &x, &y, &z);
        const uint16_t placed = block_state_make(BLOCK_ID_WALL_TORCH, BLOCK_FACING_EAST, false, false);
        nw_edit_and_save(t, rig, x, y, z, placed);
        REQUIRE(nw_open_only(t, rig));
        memset(rig->states, 0, NW_COLUMN_BYTES);
        (void)save_world_column_overlay(&rig->save->world, NW_COLUMN_X[0], 0, rig->states, rig->scratch);
        CHECK_EQ_U64(rig->states[(uint64_t)NW_SECTION_LOW * SECTION_VOXELS + 3u], placed);
        for (uint32_t voxel = 4u; voxel < SECTION_VOXELS; voxel++) {
            const uint16_t saved = nw_voxel_state(0u, NW_SECTION_LOW, voxel, BLOCK_COUNT);
            if (saved == BLOCK_ID_WALL_TORCH) {
                CHECK_EQ_U64(rig->states[(uint64_t)NW_SECTION_LOW * SECTION_VOXELS + voxel],
                             block_state_make(BLOCK_ID_WALL_TORCH, facing, false, false));
            }
        }
        save_deinit(rig->save);
        const int32_t was_quiet = log_set_fd(quiet);
        (void)close(was_quiet);
        nw_rig_remove(t, rig);
    }
}

TEST(a_list_too_long_for_a_state_number_is_refused) {
    nw_rig_t *const rig = nw_rig_new(t);
    REQUIRE(rig != nullptr);
    nw_world_write(t, rig, BLOCK_COUNT);
    /* This build's names, and then made-up ones up to one past what 12 bits hold. */
    uint8_t *const list = test_alloc(t, SAVE_NAMES_TABLE_BYTES_MAX);
    uint64_t at = sizeof(save_names_head_t);
    for (uint32_t id = 0u; id < SAVE_WORLD_NAMES_MAX + 1u; id++) {
        char made_up[32];
        snprintf(made_up, sizeof(made_up), "made_up_%" PRIu32, id);
        const char *const name = id < BLOCK_COUNT ? block_defs[id].name : made_up;
        memcpy(list + at, name, strlen(name) + 1u);
        at += strlen(name) + 1u;
    }
    const save_names_head_t head = {.count = SAVE_WORLD_NAMES_MAX + 1u,
                                    .bytes = (uint32_t)(at - sizeof(save_names_head_t))};
    memcpy(list, &head, sizeof(head));
    while ((at & 7u) != 0u) {
        list[at] = 0u;
        at++;
    }
    nw_manifest_list_set(t, rig, list, at);
    const int32_t quiet = log_set_fd(open("/dev/null", O_WRONLY | O_CLOEXEC));
    REQUIRE(save_init(rig->save));
    CHECK_EQ_U64(save_open(rig->save, rig->root, NW_SEED), SAVE_WORLD_ERROR_TABLES);
    save_deinit(rig->save);
    const int32_t was_quiet = log_set_fd(quiet);
    (void)close(was_quiet);
    nw_rig_remove(t, rig);
}

TEST_MAIN()
