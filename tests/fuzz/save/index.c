/* ctest fuzz_save. See README.md. */
#include "fuzz/support/index.h"

#include "run/physics/fixtures/test_world.h"

#include "entity/index.h"
#include "file/index.h"
#include "item/index.h"
#include "log/index.h"
#include "mob/index.h"
#include "mob/spawn/index.h"
#include "player/index.h"
#include "save/index.h"
#include "settings/persist/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "ui/menu/catalog/index.h"

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The first byte of an input says what the rest is. */
typedef enum fs_mode_t : uint8_t {
    FS_MODE_RECORD = 0,   /* one section's record */
    FS_MODE_STATE = 1,    /* a state.<generation> */
    FS_MODE_WORLD = 2,    /* uint32_t manifest bytes, the manifest, then one delta file */
    FS_MODE_META = 3,     /* a world.meta */
    FS_MODE_SETTINGS = 4, /* a settings.yaml */
    FS_MODE_COUNT = 5,
} fs_mode_t;

enum : uint64_t {
    FS_INPUT_BYTES = 8ull << 20,
    FS_HASH_START = 0xCBF29CE484222325ull,
    FS_HASH_PRIME = 0x100000001B3ull,
    FS_SEED = 9191u,
    FS_GENERATION = 5u,
    FS_COLUMN_VOXELS = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS,
    FS_FAR_LINE_VOXELS = (uint64_t)COLUMN_SECTIONS * SECTION_SIZE,
};
enum : uint32_t {
    /* Long enough for a mind to ask the way, have it answered and go, and for the seed's creeper to burst. */
    FS_TICKS = 8u,
    FS_NAME_BYTES = 2048u,
    FS_RECORD_LEVELS = 0x8000u,
};

typedef struct fs_context_t {
    test_context_t *borrowed_test; /* 0 */
    test_world_t w;                /* 8 */
    player_t *player;              /* 32 */
    item_t *item;                  /* 40 */
    entity_t *entity;              /* 48 */
    mob_t *mob;                    /* 56 */
    mob_spawn_t *spawn;            /* 64 */
    item_stack_t *held;            /* 72 */
    save_t *save;                  /* 80 */
    arena_t *state_arena;          /* 88 */
    uint8_t *file_back;            /* 96: FS_INPUT_BYTES: what a state was written as */
    char *root;                    /* 104 */
    char *blocks;                  /* 112 */
    fuzz_buffer_t bytes;           /* 120 */
    fuzz_buffer_t states;          /* 144 */
    fuzz_buffer_t states_again;    /* 168 */
    fuzz_buffer_t levels;          /* 192 */
    fuzz_buffer_t scratch;         /* 216 */
    fuzz_buffer_t far_scratch;     /* 240 */
    fuzz_buffer_t far_line;        /* 264 */
    fuzz_buffer_t palette_slots;   /* 288 */
    uint64_t exec_count;           /* 312 */
    const uint8_t *state_seed;     /* 320: a whole input, of a state the game wrote */
    uint64_t state_seed_bytes;     /* 328 */
} fs_context_t;
static_assert(sizeof(fs_context_t) == 336);

/* In a child there is no one to tell of a failed CHECK, so what seeds need is a crash like any other. */
#define FS_MUST(condition)                                                                                             \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fuzz_fail("making the seeds: %s", #condition);                                                             \
        }                                                                                                              \
    } while (false)

/* How far inputs got. */
enum : uint32_t {
    FS_COUNT_RECORD = 0u,
    FS_COUNT_STATE_OPENED = 1u,
    FS_COUNT_STATE_APPLIED = 2u,
    FS_COUNT_WORLD_OPENED = 3u,
    FS_COUNT_WORLD_SAVED = 4u,
    FS_COUNT_WORLD_REOPENED = 5u,
    FS_COUNT_META = 6u,
    FS_COUNT_SETTINGS_SET = 7u,
    FS_COUNT_SEARCHES = 8u,
    FS_COUNT_BLASTS = 9u,
};
static const char *const fs_count_names[] = {"records accepted",
                                             "states opened",
                                             "states applied and written back",
                                             "worlds opened",
                                             "worlds saved over",
                                             "worlds opened again",
                                             "world.meta accepted",
                                             "settings files that set something",
                                             "states whose minds searched for a way",
                                             "states a creeper burst in"};

static uint64_t fs_hash_more(uint64_t hash, const void *data, uint64_t bytes) {
    const uint8_t *const cursor = data;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ cursor[i]) * FS_HASH_PRIME;
    }
    return hash;
}

static uint64_t fs_hash(const void *data, uint64_t bytes) {
    return fs_hash_more(FS_HASH_START, data, bytes);
}

/* ---- Files ------------------------------------------------------------------------------------------------------------- */

static void fs_directory_empty(const char *directory) {
    DIR *const handle = opendir(directory);
    if (handle == nullptr) {
        return;
    }
    for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char path[FS_NAME_BYTES];
            (void)snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
            (void)unlink(path);
        }
    }
    (void)closedir(handle);
}

static void fs_file_put(const char *directory, const char *name, const uint8_t *bytes, uint64_t length) {
    char path[FS_NAME_BYTES];
    (void)snprintf(path, sizeof(path), "%s/%s", directory, name);
    const int32_t fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0 || write(fd, bytes, length) != (int64_t)length) {
        fuzz_fail("%s could not be written, which is this program's trouble and not what it tests", path);
    }
    (void)close(fd);
}

/* ---- Records --------------------------------------------------------------------------------------------------------- */

static void fs_record_execute(fs_context_t *fs, const uint8_t *body, uint64_t length) {
    const uint8_t *const record = fuzz_buffer_hold(&fs->bytes, body, length);
    uint16_t *const states = (uint16_t *)fuzz_buffer_fit(&fs->states, SECTION_VOXELS * sizeof(uint16_t));
    memset(states, 0xEE, SECTION_VOXELS * sizeof(uint16_t));
    if (!save_world_record_decode(record, length, states)) {
        for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
            if (states[i] != 0xEEEEu) {
                fuzz_fail("a refused record wrote voxel %" PRIu32, i);
            }
        }
        return;
    }
    fuzz_count(FS_COUNT_RECORD);
    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
        if (states[i] >= BLOCK_COUNT) {
            fuzz_fail("an accepted record gave state %" PRIu32 ", past the table", (uint32_t)states[i]);
        }
    }
    const uint8_t *const levels = save_world_record_levels(record, length);
    if (levels != nullptr) {
        for (uint32_t i = 0u; i < SAVE_WORLD_RECORD_LEVELS_BYTES; i++) {
            if ((levels[i] & 0x0Fu) > 8u || (levels[i] >> 4) > 8u) {
                fuzz_fail("an accepted record has a level past 8");
            }
        }
    }
    /* Whatever it was, an encoder's form of the same blocks must mean the same blocks. */
    uint8_t *const encoded = fuzz_buffer_fit(&fs->bytes, SAVE_WORLD_RECORD_BYTES_MAX);
    uint16_t *const slots = (uint16_t *)fuzz_buffer_fit(&fs->palette_slots, 65536u * sizeof(uint16_t));
    const uint32_t encoded_bytes = save_world_record_encode(states, slots, encoded);
    uint16_t *const again = (uint16_t *)fuzz_buffer_fit(&fs->states_again, SECTION_VOXELS * sizeof(uint16_t));
    if (!save_world_record_decode(encoded, encoded_bytes, again) ||
        memcmp(states, again, SECTION_VOXELS * sizeof(uint16_t)) != 0) {
        fuzz_fail("blocks that were read do not come back through the encoder");
    }
}

/* ---- world.meta and settings.yaml ------------------------------------------------------------------------------------- */

static void fs_meta_execute(fs_context_t *fs, const uint8_t *body, uint64_t length) {
    const char *const text = (const char *)fuzz_buffer_hold(&fs->bytes, body, length);
    ui_menu_world_t world;
    memset(&world, 0xEE, sizeof(world));
    if (!ui_menu_catalog_meta_parse(text, length, &world)) {
        return;
    }
    fuzz_count(FS_COUNT_META);
    if (memchr(world.name, '\0', sizeof(world.name)) == nullptr) {
        fuzz_fail("an accepted world.meta left a name with no end");
    }
}

static void fs_settings_execute(fs_context_t *fs, const uint8_t *body, uint64_t length) {
    const uint8_t *const text = fuzz_buffer_hold(&fs->bytes, body, length);
    settings_values_t values;
    settings_persist_defaults(&values);
    settings_persist_report_t report = {0};
    settings_persist_parse(text, length, &values, &report);
    if (report.applied != 0u) {
        fuzz_count(FS_COUNT_SETTINGS_SET);
    }
    settings_values_t held = values;
    settings_persist_clamp(&held);
    if (memcmp(&held, &values, sizeof(values)) != 0) {
        fuzz_fail("a settings file got a value past its range past the parser");
    }
    /* What it makes of them must be what those values are, to a parser with different ones. */
    uint8_t *const out = fuzz_buffer_fit(&fs->bytes, SETTINGS_TEXT_BYTES);
    uint64_t written = 0u;
    if (!settings_persist_write(&values, out, SETTINGS_TEXT_BYTES, &written)) {
        fuzz_fail("settings_persist_write refused its own buffer");
    }
    settings_values_t back;
    settings_persist_defaults(&back);
    back.fov_degrees = values.fov_degrees == SETTINGS_FOV_MIN ? SETTINGS_FOV_MAX : SETTINGS_FOV_MIN;
    back.vsync = (uint8_t)(values.vsync != 0u ? 0u : 1u);
    settings_persist_report_t back_report = {0};
    settings_persist_parse(out, written, &back, &back_report);
    if (memcmp(&back, &values, sizeof(values)) != 0 || back_report.problems != 0u) {
        fuzz_fail("settings written do not read back as they were");
    }
}

/* ---- State ------------------------------------------------------------------------------------------------------------- */

static void fs_parts_open(fs_context_t *fs) {
    player_spawn(fs->player, 3.5f64, 4.5f64, 1.0f64);
    item_open(fs->item);
    item_world_begin(fs->item, &fs->w.world->edit);
    entity_open(fs->entity, 41u);
    mob_spawn_start(fs->spawn, &fs->entity->arena_world, 43u);
    mob_open(fs->mob, fs->w.world, 47u);
    memset(fs->held, 0, sizeof(*fs->held));
}

static void fs_parts_close(fs_context_t *fs) {
    mob_close(fs->mob);
    entity_close(fs->entity);
    item_close(fs->item);
}

static save_state_parts_t fs_parts(const fs_context_t *fs) {
    return (save_state_parts_t){
        .borrowed_player = fs->player,
        .borrowed_item = fs->item,
        .borrowed_entity = fs->entity,
        .borrowed_mob = fs->mob,
        .borrowed_spawn = fs->spawn,
        .borrowed_held = fs->held,
        .time = {.world_ticks = 0u, .day_units = 0u, .yaw = 0.0f32, .pitch = 0.0f32, ._pad0 = {0}}};
}

static void fs_no_spill(void *context, const item_stack_t *stack, const int32_t cell[3]) {
    (void)context;
    (void)stack;
    (void)cell;
}

/* What goes on in a game loaded from this, in the order of game_sim_tick. The searches run on this thread, as mob_init was given no job system: see README.md. */
static void fs_state_ticks(fs_context_t *fs) {
    entity_t *const entity = fs->entity;
    player_t *const player = fs->player;
    mob_t *const mob = fs->mob;
    const uint64_t searches_before = mob->path.searches;
    for (uint32_t tick = 0u; tick < FS_TICKS; tick++) {
        const player_movement_t *const movement = &player->movement;
        const bool target = player->survival.state != PLAYER_SURVIVAL_DEAD && player->survival.placing == 0u;
        const float64_t height =
            movement->crouched != 0u ? PLAYER_MOVEMENT_HEIGHT_CROUCHED : PLAYER_MOVEMENT_HEIGHT_STANDING;
        item_world_tick(fs->item, &fs->w.world->edit, fs_no_spill, nullptr);
        mob_tick(mob);
        const mob_zombie_view_t zombie_view = {
            .position = {movement->position[0], movement->position[1], movement->position[2]},
            .eye = movement->eye_height,
            .half_width = PLAYER_MOVEMENT_WIDTH * 0.5f64,
            .height = height,
            .alive = (uint8_t)(target ? 1u : 0u),
        };
        const float32_t blow = mob_minds_tick(mob, entity, &zombie_view);
        if (blow > 0.0f32) {
            (void)player_survival_hurt(&player->survival, blow, PLAYER_SURVIVAL_CAUSE_ATTACK);
        }
        const mob_spawn_view_t view = {.position = {fs->player->movement.position[0], fs->player->movement.position[1],
                                                    fs->player->movement.position[2]},
                                       .day_fraction = fs->player->survival.day_fraction};
        mob_spawn_tick(fs->spawn, &entity->store, entity->borrowed_physics, fs->w.map, &view);
        const mob_skeleton_view_t skeleton_view = {
            .position = {movement->position[0], movement->position[1], movement->position[2]},
            .eye_height = movement->eye_height,
            .height = height,
            .alive = (uint8_t)(target ? 1u : 0u),
        };
        mob_skeleton_tick(&mob->skeleton, &mob->path, entity, fs->w.map, &skeleton_view);
        const mob_creeper_scene_t scene = {
            .borrowed_entity = entity,
            .borrowed_path = &mob->path,
            .borrowed_edit = &fs->w.world->edit,
            .borrowed_movement = &player->movement,
            .borrowed_survival = &player->survival,
        };
        mob_creeper_tick(&mob->creeper, &scene);
        entity_tick(entity, fs->w.map);
        mob_combat_tick(&mob->combat, &entity->store);
        projectile_tick(&entity->projectiles, &entity->store, &entity->grid, fs->w.map, nullptr, nullptr, nullptr);
        player_tick(fs->player, fs->w.map, (int64_t)tick * PLAYER_TICK_NS);
        player_survive_tick(fs->player, fs->w.map);
    }
    if (mob->path.searches != searches_before) {
        fuzz_count(FS_COUNT_SEARCHES);
    }
    /* The floor, as it was, or a few thousand blasts on nothing is left to stand on. */
    if (entity->explosion.blast_count != 0u) {
        fuzz_count(FS_COUNT_BLASTS);
        test_world_fill(fs->borrowed_test, &fs->w, -16, -16, 0, 31, 31, 0, BLOCK_ID_STONE);
    }
}

static void fs_state_execute(fs_context_t *fs, const uint8_t *body, uint64_t length) {
    /* The bytes alone, watched by ASan. */
    const uint8_t *const held = fuzz_buffer_hold(&fs->bytes, body, length);
    (void)save_state_check(held, length, FS_SEED, FS_GENERATION);

    /* The real way in, from a file: anything else that gets past that has been believed. */
    fs_directory_empty(fs->root);
    char name[64];
    (void)snprintf(name, sizeof(name), "state.%" PRIu64, (uint64_t)FS_GENERATION);
    fs_file_put(fs->root, name, body, length);
    arena_reset(fs->state_arena);
    save_state_t state;
    const save_world_status_t opened = save_state_open(&state, fs->state_arena, fs->root, FS_SEED, FS_GENERATION);
    if (opened != SAVE_WORLD_OK) {
        return;
    }
    fuzz_count(FS_COUNT_STATE_OPENED);
    if (state.kept == 0u) {
        fuzz_fail("a file that was there and passed was not kept");
    }
    fs_parts_open(fs);
    save_state_parts_t parts = fs_parts(fs);
    float64_t feet[COLLISION_AXES] = {0.0f64, 0.0f64, 0.0f64};
    float32_t yaw = 0.0f32;
    float32_t pitch = 0.0f32;
    (void)save_state_kept_start(&state, feet, &yaw, &pitch);
    if (!save_state_apply(&state, &parts)) {
        fuzz_fail("a file that passed would not apply");
    }
    fs_state_ticks(fs);

    /* What the game then saves, it must itself believe. */
    save_state_capture(&state, &parts);
    if (save_state_write(&state, FS_GENERATION + 1u) != SAVE_WORLD_OK) {
        fuzz_fail("a state that was loaded could not be written");
    }
    char path[FS_NAME_BYTES];
    (void)snprintf(path, sizeof(path), "%s/state.%" PRIu64, fs->root, (uint64_t)FS_GENERATION + 1u);
    uint64_t written = 0u;
    if (file_read_whole(path, fs->file_back, FS_INPUT_BYTES, &written) != FILE_OK) {
        fuzz_fail("a state just written could not be read");
    }
    const save_world_status_t again = save_state_check(fs->file_back, written, FS_SEED, FS_GENERATION + 1u);
    if (again != SAVE_WORLD_OK) {
        /* Once more with its log on, as that says which part. */
        (void)log_set_fd(STDERR_FILENO);
        (void)save_state_check(fs->file_back, written, FS_SEED, FS_GENERATION + 1u);
        fuzz_fail("the game wrote a state it then refuses: %s", save_world_status_name(again));
    }
    fuzz_count(FS_COUNT_STATE_APPLIED);
    save_state_close(&state);
    fs_parts_close(fs);
}

/* Lengths and every hash, so that what is inside is what gets looked at. */
static void fs_state_fix(fs_context_t *fs, uint8_t *body, uint64_t length, fuzz_rng_t *rng) {
    (void)fs;
    save_state_header_t header;
    if (length < sizeof(header)) {
        return;
    }
    memcpy(&header, body, sizeof(header));
    if (fuzz_rng_below(rng, 8u) != 0u) {
        header.seed = FS_SEED;
        header.generation = FS_GENERATION;
        header.table_hash = save_state_table_hash();
        header.version = SAVE_STATE_VERSION;
        header.header_bytes = sizeof(header);
        memcpy(header.magic, "MCSTATE1", sizeof(header.magic));
    }
    header.sections_bytes = length - sizeof(header);
    header.sections_hash = fs_hash(body + sizeof(header), length - sizeof(header));
    header.checksum = fs_hash(&header, offsetof(save_state_header_t, checksum));
    memcpy(body, &header, sizeof(header));
}

/* ---- Blocks ------------------------------------------------------------------------------------------------------------ */

/* After an open that was taken: everything a game would read of that world, all of it into memory ASan watches. */
static void fs_world_read(fs_context_t *fs, int32_t region_x, int32_t region_y) {
    save_world_t *const world = &fs->save->world;
    uint16_t *const states = (uint16_t *)fuzz_buffer_fit(&fs->states, FS_COLUMN_VOXELS * sizeof(uint16_t));
    uint8_t *const scratch = fuzz_buffer_fit(&fs->scratch, SAVE_WORLD_OVERLAY_SCRATCH_BYTES);
    uint8_t *const levels = fuzz_buffer_fit(&fs->levels, SAVE_WORLD_OVERLAY_LEVELS_BYTES);
    /* A manifest may name any region, and what it says is not this program's to overflow on. */
    const uint32_t first_x = (uint32_t)region_x * SAVE_WORLD_REGION_SIDE;
    const uint32_t first_y = (uint32_t)region_y * SAVE_WORLD_REGION_SIDE;
    for (uint32_t local = 0u; local < SAVE_WORLD_REGION_COLUMNS; local++) {
        const int32_t chunk_x = (int32_t)(first_x + local % SAVE_WORLD_REGION_SIDE);
        const int32_t chunk_y = (int32_t)(first_y + local / SAVE_WORLD_REGION_SIDE);
        column_mask_t levels_mask = COLUMN_MASK_NONE;
        const column_mask_t mask =
            save_world_column_overlay_levels(world, chunk_x, chunk_y, states, levels, &levels_mask, scratch);
        if (column_mask_any(column_mask_without(levels_mask, mask))) {
            fuzz_fail("levels for a section that was not read");
        }
        for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
            if (!column_mask_has(mask, section)) {
                continue;
            }
            for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
                if (states[(uint64_t)section * SECTION_VOXELS + i] >= BLOCK_COUNT) {
                    fuzz_fail("a saved section gave a state past the table");
                }
            }
        }
    }
    uint8_t *const far_scratch = fuzz_buffer_fit(&fs->far_scratch, SAVE_WORLD_FAR_SCRATCH_BYTES);
    memset(far_scratch, 0, 64u);
    uint16_t *const line = (uint16_t *)fuzz_buffer_fit(&fs->far_line, FS_FAR_LINE_VOXELS * sizeof(uint16_t));
    for (uint32_t i = 0u; i < 8u; i++) {
        save_world_far_line(world, (int32_t)(first_x * COLUMN_SIZE + i * 7u), (int32_t)(first_y * COLUMN_SIZE + i * 3u),
                            line, far_scratch);
    }
}

static void fs_world_execute(fs_context_t *fs, const uint8_t *body, uint64_t length) {
    if (length < 4u) {
        return;
    }
    const uint64_t manifest_bytes = fuzz_get_u32(body);
    if (manifest_bytes > length - 4u) {
        return;
    }
    const uint8_t *const manifest = body + 4u;
    const uint8_t *const delta = manifest + manifest_bytes;
    const uint64_t delta_bytes = length - 4u - manifest_bytes;
    fs_directory_empty(fs->blocks);
    fs_directory_empty(fs->root);
    if (mkdir(fs->blocks, 0755) != 0 && access(fs->blocks, F_OK) != 0) {
        fuzz_fail("%s could not be made", fs->blocks);
    }
    /* The file the manifest's first region names, or a name for nothing, which an open removes. */
    int32_t region_x = 0;
    int32_t region_y = 0;
    uint64_t generation = 0u;
    if (manifest_bytes >= sizeof(save_world_manifest_header_t) + sizeof(save_world_region_t)) {
        save_world_region_t region;
        memcpy(&region, manifest + sizeof(save_world_manifest_header_t), sizeof(region));
        region_x = region.region_x;
        region_y = region.region_y;
        generation = region.generation;
    }
    char name[128];
    (void)snprintf(name, sizeof(name), "r.%" PRId32 ".%" PRId32 ".%" PRIu64, region_x, region_y, generation);
    fs_file_put(fs->blocks, "manifest", manifest, manifest_bytes);
    fs_file_put(fs->blocks, name, delta, delta_bytes);

    save_t *const save = fs->save;
    if (save_open(save, fs->root, FS_SEED) != SAVE_WORLD_OK) {
        return;
    }
    fuzz_count(FS_COUNT_WORLD_OPENED);
    fs_world_read(fs, region_x, region_y);
    const uint64_t damaged_first = save->world.sections_damaged;

    /* A save over what is there, in the region the map can reach: this is what reads the old file to make the new one. */
    fs->exec_count++;
    const uint16_t state = (fs->exec_count & 1u) != 0u ? BLOCK_ID_STONE : BLOCK_ID_DIRT;
    (void)world_edit_block_set(&fs->w.world->edit, 5, 5, 1, state);
    save_world_status_t status = save_world_capture_dirty(&save->world, &fs->w.world->edit);
    if (status == SAVE_WORLD_OK && save_world_commit_begin(&save->world)) {
        status = save_world_commit_write(&save->world);
    }
    save_close(save);
    if (status != SAVE_WORLD_OK) {
        return;
    }

    fuzz_count(FS_COUNT_WORLD_SAVED);
    /* The game must always be able to open what it just saved. A save drops what it could not read and keeps the old file, so of the region it wrote, nothing is left to find damaged. */
    const save_world_status_t reopened = save_open(save, fs->root, FS_SEED);
    if (reopened != SAVE_WORLD_OK) {
        fuzz_fail("the game wrote a world it then refuses: %s", save_world_status_name(reopened));
    }
    fuzz_count(FS_COUNT_WORLD_REOPENED);
    fs_world_read(fs, 0, 0);
    if (save->world.sections_damaged != 0u) {
        fuzz_fail("a save found %" PRIu64 " damaged sections, and left %" PRIu64 " in what it wrote", damaged_first,
                  (uint64_t)save->world.sections_damaged);
    }
    save_close(save);
}

/* Every hash and length that says a file is whole, from the inside out, for as much as can be found of it. */
static void fs_world_fix(uint8_t *body, uint64_t length, fuzz_rng_t *rng) {
    if (length < 4u) {
        return;
    }
    const uint64_t manifest_bytes = fuzz_get_u32(body);
    if (manifest_bytes > length - 4u) {
        return;
    }
    uint8_t *const manifest = body + 4u;
    uint8_t *const delta = manifest + manifest_bytes;
    const uint64_t delta_bytes = length - 4u - manifest_bytes;
    save_world_region_t region;
    memset(&region, 0, sizeof(region));
    const bool has_region = manifest_bytes >= sizeof(save_world_manifest_header_t) + sizeof(region);
    if (has_region) {
        memcpy(&region, manifest + sizeof(save_world_manifest_header_t), sizeof(region));
    }

    save_world_delta_header_t delta_header;
    const uint64_t table_end = sizeof(delta_header) + SAVE_WORLD_REGION_COLUMNS * sizeof(save_world_column_entry_t);
    if (delta_bytes >= table_end) {
        for (uint32_t column = 0u; column < SAVE_WORLD_REGION_COLUMNS; column++) {
            uint8_t *const entry_at =
                delta + sizeof(delta_header) + (uint64_t)column * sizeof(save_world_column_entry_t);
            save_world_column_entry_t entry;
            memcpy(&entry, entry_at, sizeof(entry));
            const uint64_t count = (uint64_t)column_mask_count(entry.section_mask);
            const uint64_t entries_bytes = count * sizeof(save_world_section_entry_t);
            if (!column_mask_any(entry.section_mask) || entry.entries_offset > delta_bytes ||
                entries_bytes > delta_bytes - entry.entries_offset) {
                continue;
            }
            uint8_t *const entries = delta + entry.entries_offset;
            for (uint64_t i = 0u; i < count; i++) {
                save_world_section_entry_t section;
                memcpy(&section, entries + i * sizeof(section), sizeof(section));
                if (section.offset <= delta_bytes && section.bytes <= delta_bytes - section.offset) {
                    const uint64_t hash = fs_hash(delta + section.offset, section.bytes);
                    section.checksum = (uint32_t)(hash ^ (hash >> 32));
                    memcpy(entries + i * sizeof(section), &section, sizeof(section));
                }
            }
            entry.checksum =
                fs_hash_more(fs_hash(&entry, offsetof(save_world_column_entry_t, checksum)), entries, entries_bytes);
            memcpy(entry_at, &entry, sizeof(entry));
        }
    }
    if (delta_bytes >= sizeof(delta_header)) {
        memcpy(&delta_header, delta, sizeof(delta_header));
        if (fuzz_rng_below(rng, 16u) != 0u) {
            memcpy(delta_header.magic, "MCWDELTA", sizeof(delta_header.magic));
            delta_header.seed = FS_SEED;
            delta_header.version = SAVE_WORLD_VERSION;
            delta_header.header_bytes = sizeof(delta_header);
            delta_header.file_bytes = delta_bytes;
            if (has_region) {
                delta_header.generation = region.generation;
                delta_header.region_x = region.region_x;
                delta_header.region_y = region.region_y;
                delta_header.section_count = region.section_count;
            }
        }
        delta_header.checksum = fs_hash(&delta_header, offsetof(save_world_delta_header_t, checksum));
        memcpy(delta, &delta_header, sizeof(delta_header));
    }

    save_world_manifest_header_t header;
    if (manifest_bytes >= sizeof(header)) {
        memcpy(&header, manifest, sizeof(header));
        if (fuzz_rng_below(rng, 16u) != 0u) {
            memcpy(header.magic, "MCWORLDM", sizeof(header.magic));
            header.seed = FS_SEED;
            header.table_hash = save_world_table_hash();
            header.version = SAVE_WORLD_VERSION;
            header.header_bytes = sizeof(header);
            header.region_count = (uint32_t)((manifest_bytes - sizeof(header)) / sizeof(save_world_region_t));
            if (has_region) {
                region.file_bytes = delta_bytes;
                region.damaged = 0u;
                memcpy(manifest + sizeof(header), &region, sizeof(region));
            }
        }
        header.regions_checksum = fs_hash(manifest + sizeof(header), manifest_bytes - sizeof(header));
        header.checksum = fs_hash(&header, offsetof(save_world_manifest_header_t, checksum));
        memcpy(manifest, &header, sizeof(header));
    }
}

/* ---- The target -------------------------------------------------------------------------------------------------------- */

static void fs_execute(void *context, const uint8_t *input, uint64_t length) {
    fs_context_t *const fs = context;
    if (length == 0u) {
        return;
    }
    const uint8_t *const body = input + 1u;
    const uint64_t body_bytes = length - 1u;
    switch (input[0] % FS_MODE_COUNT) {
    case FS_MODE_RECORD:
        fs_record_execute(fs, body, body_bytes);
        break;
    case FS_MODE_STATE:
        fs_state_execute(fs, body, body_bytes);
        break;
    case FS_MODE_WORLD:
        fs_world_execute(fs, body, body_bytes);
        break;
    case FS_MODE_META:
        fs_meta_execute(fs, body, body_bytes);
        break;
    default:
        fs_settings_execute(fs, body, body_bytes);
        break;
    }
}

/* One value of any width, in place: the kind of thing a file that passes its hash could hold. */
static void fs_poke(uint8_t *bytes, uint64_t length, fuzz_rng_t *rng) {
    static const uint32_t words[] = {0u,
                                     1u,
                                     0xFFFFFFFFu,
                                     0x7FFFFFFFu,
                                     0x80000000u,
                                     0x7FC00000u /* NaN */,
                                     0x7F800000u /* infinity */,
                                     0xFF800000u,
                                     0x7149F2CAu /* 1e30 */,
                                     0xF149F2CAu,
                                     4096u,
                                     65535u,
                                     65536u};
    static const uint64_t longs[] = {0u,
                                     1u,
                                     UINT64_MAX,
                                     0x7FF8000000000000ull /* NaN */,
                                     0x7FF0000000000000ull /* infinity */,
                                     0xFFF0000000000000ull,
                                     0x41CDCD6500000000ull /* 1e9 */,
                                     0x41CDCD6500200000ull /* just past it */,
                                     0x46293E5939A08CEAull /* 1e30 */};
    if (length == 0u) {
        return;
    }
    const uint64_t at = fuzz_rng_below(rng, length);
    switch (fuzz_rng_below(rng, 4u)) {
    case 0u:
        bytes[at] ^= (uint8_t)(1u << fuzz_rng_below(rng, 8u));
        break;
    case 1u:
        bytes[at] = (uint8_t)fuzz_rng_next(rng);
        break;
    case 2u: {
        /* On a field's boundary, as nearly all of these structs have four byte fields. */
        const uint64_t aligned = at & ~(uint64_t)3u;
        if (length - aligned >= sizeof(uint32_t)) {
            fuzz_put_u32(bytes + aligned, words[fuzz_rng_below(rng, sizeof(words) / sizeof(words[0]))]);
        }
        break;
    }
    default: {
        const uint64_t aligned = at & ~(uint64_t)7u;
        if (length - aligned >= sizeof(uint64_t)) {
            fuzz_put_u64(bytes + aligned, longs[fuzz_rng_below(rng, sizeof(longs) / sizeof(longs[0]))]);
        }
        break;
    }
    }
}

/* A state is a list of sections of exact sizes, most of whose bytes are tables that must be zero, so a change of length or a byte thrown anywhere is refused and
 * nothing behind the check is ever reached. So: a section, each the same, and a change inside it that keeps every length. A record is the same in smaller. */
static bool fs_mutate(void *context, uint8_t *input, uint64_t *length, uint64_t capacity, fuzz_rng_t *rng) {
    const fs_context_t *const fs = context;
    if (*length < 2u || fuzz_rng_below(rng, 8u) == 0u) {
        return false;
    }
    const uint32_t kind = input[0] % FS_MODE_COUNT;
    /* An input that finds a new way to be refused goes in the corpus like any other, and nothing made from it gets in. So mostly from one that does. */
    if (kind == FS_MODE_STATE && fs->state_seed_bytes != 0u && fs->state_seed_bytes <= capacity &&
        fuzz_rng_below(rng, 4u) != 0u) {
        memcpy(input, fs->state_seed, fs->state_seed_bytes);
        *length = fs->state_seed_bytes;
    }
    uint8_t *const body = input + 1u;
    const uint64_t bytes = *length - 1u;
    const uint64_t pokes = 1u + fuzz_rng_below(rng, 3u);
    if (kind == FS_MODE_RECORD) {
        for (uint64_t i = 0u; i < pokes; i++) {
            fs_poke(body, bytes, rng);
        }
        return true;
    }
    if (kind != FS_MODE_STATE) {
        return false;
    }
    uint64_t starts[SAVE_STATE_SECTION_COUNT];
    uint64_t sizes[SAVE_STATE_SECTION_COUNT];
    uint32_t found = 0u;
    uint64_t at = sizeof(save_state_header_t);
    while (found < SAVE_STATE_SECTION_COUNT && bytes >= at + sizeof(save_state_section_t)) {
        const uint64_t size = fuzz_get_u64(body + at);
        at += sizeof(save_state_section_t);
        if (size > bytes - at) {
            break;
        }
        starts[found] = at;
        sizes[found] = size;
        found++;
        const uint64_t padded = (size + 7u) & ~(uint64_t)7u;
        if (padded > bytes - at) {
            break;
        }
        at += padded;
    }
    if (found == 0u) {
        return false;
    }
    for (uint64_t i = 0u; i < pokes; i++) {
        const uint64_t which = fuzz_rng_below(rng, found);
        fs_poke(body + starts[which], sizes[which], rng);
    }
    return true;
}

static void fs_fix(void *context, uint8_t *input, uint64_t *length, uint64_t capacity, fuzz_rng_t *rng) {
    (void)capacity;
    fs_context_t *const fs = context;
    /* One in eight goes in as it came, so that the checks themselves are what is tried. */
    if (*length < 2u || fuzz_rng_below(rng, 8u) == 0u) {
        return;
    }
    if (input[0] % FS_MODE_COUNT == FS_MODE_STATE) {
        fs_state_fix(fs, input + 1u, *length - 1u, rng);
    } else if (input[0] % FS_MODE_COUNT == FS_MODE_WORLD) {
        fs_world_fix(input + 1u, *length - 1u, rng);
    }
}

/* Records of each form: one state, each palette width, direct, and with levels. */
static void fs_seeds_records(test_context_t *t, fs_context_t *fs, fuzz_corpus_t *corpus) {
    uint16_t *const states = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint16_t *const slots = test_alloc(t, 65536u * sizeof(uint16_t));
    uint8_t *const seed = test_alloc(t, 1u + SAVE_WORLD_ENTRY_BYTES_MAX);
    (void)fs;
    static const uint32_t kinds[] = {1u, 2u, 3u, 4u, 16u, 200u, 257u, 4096u};
    for (uint32_t k = 0u; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
            states[i] = (uint16_t)((i % kinds[k]) % BLOCK_COUNT);
        }
        seed[0] = FS_MODE_RECORD;
        const uint32_t bytes = save_world_record_encode(states, slots, seed + 1u);
        fuzz_seed_add(corpus, seed, 1u + bytes);
        /* The same, ending in levels. */
        uint16_t bits = 0u;
        memcpy(&bits, seed + 1u, sizeof(bits));
        bits |= FS_RECORD_LEVELS;
        memcpy(seed + 1u, &bits, sizeof(bits));
        memset(seed + 1u + bytes, 0x35, SAVE_WORLD_RECORD_LEVELS_BYTES);
        fuzz_seed_add(corpus, seed, 1u + bytes + SAVE_WORLD_RECORD_LEVELS_BYTES);
    }
}

/* Texts of the two small files. */
static void fs_seeds_texts(fuzz_corpus_t *corpus) {
    uint8_t seed[SETTINGS_TEXT_BYTES + 1u];
    static const char meta[] = "minecraft_c world 1\nseed 12345678901234567890\nname My World\n";
    seed[0] = FS_MODE_META;
    memcpy(seed + 1u, meta, sizeof(meta) - 1u);
    fuzz_seed_add(corpus, seed, sizeof(meta));

    settings_values_t values;
    settings_persist_defaults(&values);
    uint64_t written = 0u;
    seed[0] = FS_MODE_SETTINGS;
    if (settings_persist_write(&values, seed + 1u, SETTINGS_TEXT_BYTES, &written)) {
        fuzz_seed_add(corpus, seed, 1u + written);
    }
}

/* A world that has really been saved: its manifest and its one delta file. */
static void fs_seeds_world(test_context_t *t, fs_context_t *fs, fuzz_corpus_t *corpus) {
    save_t *const save = fs->save;
    fs_directory_empty(fs->blocks);
    fs_directory_empty(fs->root);
    FS_MUST(save_open(save, fs->root, FS_SEED) == SAVE_WORLD_OK);
    /* Sections of different forms, in several columns. */
    for (int32_t i = 0; i < 300; i++) {
        const uint16_t state = (uint16_t)(i % 7 == 0 ? BLOCK_ID_DIRT : BLOCK_ID_STONE);
        (void)world_edit_block_set(&fs->w.world->edit, i % 40, (i * 3) % 40, 1 + i % 30, state);
    }
    FS_MUST(save_world_capture_dirty(&save->world, &fs->w.world->edit) == SAVE_WORLD_OK);
    FS_MUST(save_world_commit_begin(&save->world));
    FS_MUST(save_world_commit_write(&save->world) == SAVE_WORLD_OK);
    const save_world_region_t region = save->world.borrowed_manifest[save->world.current][0];
    save_close(save);

    char path[FS_NAME_BYTES];
    uint64_t manifest_bytes = 0u;
    uint64_t delta_bytes = 0u;
    (void)snprintf(path, sizeof(path), "%s/manifest", fs->blocks);
    const uint8_t *const manifest = fuzz_file_read(t, path, FS_INPUT_BYTES, &manifest_bytes);
    (void)snprintf(path, sizeof(path), "%s/r.%" PRId32 ".%" PRId32 ".%" PRIu64, fs->blocks, region.region_x,
                   region.region_y, region.generation);
    const uint8_t *const delta = fuzz_file_read(t, path, FS_INPUT_BYTES, &delta_bytes);
    FS_MUST(manifest != nullptr && delta != nullptr);
    const uint64_t seed_bytes = 1u + 4u + manifest_bytes + delta_bytes;
    uint8_t *const seed = test_alloc(t, seed_bytes);
    seed[0] = FS_MODE_WORLD;
    fuzz_put_u32(seed + 1u, (uint32_t)manifest_bytes);
    memcpy(seed + 5u, manifest, manifest_bytes);
    memcpy(seed + 5u + manifest_bytes, delta, delta_bytes);

    /* The fix must leave a good file as it found it, or it is breaking what it is meant to mend. */
    uint8_t *const fixed = test_alloc(t, seed_bytes);
    memcpy(fixed, seed, seed_bytes);
    fuzz_rng_t rng = {.state = 3u};
    fs_world_fix(fixed + 1u, seed_bytes - 1u, &rng);
    if (memcmp(fixed, seed, seed_bytes) != 0) {
        fuzz_fail("fs_world_fix changed a world the game wrote: it no longer matches the format");
    }
    fuzz_seed_add(corpus, seed, seed_bytes);
}

/* A state with something in every part. */
/* The seed's numbers all at what save_state_check takes and no further, so that what the game does with the most it believes gets looked at: places and speeds by turns,
 * with signs that send things outward or apart. A random poke hardly ever lands in that range. */
static void fs_seed_at_limits(fs_context_t *fs, fuzz_corpus_t *corpus, uint8_t *seed, uint64_t bytes, uint32_t kind) {
    uint8_t *const file = seed + 1u;
    const float64_t sign = (kind & 1u) != 0u ? -1.0f64 : 1.0f64;
    const float64_t speed = sign * 1024.0f64;
    const float64_t place = sign * 1000000000.0f64;
    uint64_t size = 0u;
    uint8_t *data = (uint8_t *)save_state_section_find(file, bytes, SAVE_STATE_SECTION_PLAYER, &size);
    FS_MUST(data != nullptr);
    save_state_player_t player;
    memcpy(&player, data, sizeof(player));
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        if (kind < 2u) {
            player.movement.velocity[a] = speed;
        } else {
            player.movement.position[a] = place;
            player.movement.previous[a] = place;
        }
    }
    player.movement.fall_distance = 1000000.0f64;
    player.movement.landed_fall = 1000000.0f64;
    player.movement.eye_height = 1024.0f64 * sign;
    player.survival.air =
        (kind & 1u) != 0u ? -(int32_t)PLAYER_SURVIVAL_DROWN_EVERY_TICKS : (int32_t)PLAYER_SURVIVAL_AIR_MAX;
    player.survival.exhaustion = 1000000.0f32;
    player.survival.armor_points = 1000000.0f32 * (float32_t)sign;
    memcpy(data, &player, sizeof(player));

    data = (uint8_t *)save_state_section_find(file, bytes, SAVE_STATE_SECTION_DROPS, &size);
    FS_MUST(data != nullptr);
    item_drop_t drop;
    memcpy(&drop, data + sizeof(save_state_drops_t), sizeof(drop));
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        if (kind < 2u) {
            drop.velocity[a] = speed;
        } else {
            drop.position[a] = -place;
        }
    }
    memcpy(data + sizeof(save_state_drops_t), &drop, sizeof(drop));

    data = (uint8_t *)save_state_section_find(file, bytes, SAVE_STATE_SECTION_ARROWS, &size);
    FS_MUST(data != nullptr);
    projectile_t arrow;
    memcpy(&arrow, data + sizeof(save_state_arrows_t), sizeof(arrow));
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        arrow.velocity[a] = speed;
        if (kind >= 2u) {
            arrow.position[a] = place;
        }
    }
    memcpy(data + sizeof(save_state_arrows_t), &arrow, sizeof(arrow));

    /* In the order of the file: six arrays of place, five of speed, two of fall. */
    data = (uint8_t *)save_state_section_find(file, bytes, SAVE_STATE_SECTION_BODIES, &size);
    FS_MUST(data != nullptr);
    save_state_bodies_t head;
    memcpy(&head, data, sizeof(head));
    uint8_t *const arrays = data + sizeof(head) + 3ull * ENTITY_STORE_CAPACITY * sizeof(uint32_t);
    for (uint32_t field = kind < 2u ? 6u : 0u; field < (kind < 2u ? 13u : 6u); field++) {
        const float64_t value = field < 6u ? (field % 2u == 0u ? place : -place) : (field < 11u ? speed : 1000000.0f64);
        for (uint32_t i = 0u; i < head.count; i++) {
            memcpy(arrays + (uint64_t)field * head.count * sizeof(value) + (uint64_t)i * sizeof(value), &value,
                   sizeof(value));
        }
    }
    fuzz_rng_t rng = {.state = 3u};
    fs_state_fix(fs, file, bytes, &rng);
    FS_MUST(save_state_check(file, bytes, FS_SEED, FS_GENERATION) == SAVE_WORLD_OK);
    fuzz_seed_add(corpus, seed, bytes + 1u);
}

static void fs_seeds_state(test_context_t *t, fs_context_t *fs, fuzz_corpus_t *corpus) {
    fs_directory_empty(fs->root);
    arena_reset(fs->state_arena);
    save_state_t state;
    FS_MUST(save_state_open(&state, fs->state_arena, fs->root, FS_SEED, FS_GENERATION) == SAVE_WORLD_OK);
    fs_parts_open(fs);
    item_t *const item = fs->item;
    (void)inventory_add(&item->inventory, ITEM_ID_DIRT, 40u);
    (void)inventory_add(&item->inventory, ITEM_ID_COAL, 5u);
    item_stack_t sword = item_stack_make(ITEM_ID_DIAMOND_SWORD, 1u);
    sword.wear = 9u;
    *inventory_slot(&item->inventory, 20u) = sword;
    item->crafting_hands.cells[0] = item_stack_make(ITEM_ID_DIRT, 2u);
    *fs->held = item_stack_make(ITEM_ID_DIRT, 3u);
    container_chest_t *const chest = container_open(&item->chests, 7, 8, 1);
    *container_slot(chest, 26u) = item_stack_make(ITEM_ID_STONE, 12u);
    smelting_furnace_t *const furnace = smelting_open(&item->furnaces, 9, 9, 1);
    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_RAW_IRON, 2u);
    furnace->burn_ticks = 17u;
    entity_t *const entity = fs->entity;
    const float64_t drop_at[3] = {6.5f64, 6.5f64, 3.0f64};
    item_drop_spawn(&entity->item_drops, 1u, 70u, drop_at);
    const float64_t feet[3] = {10.5f64, 12.5f64, 1.0f64};
    const entity_handle_t first = mob_spawn_body(fs->spawn, &entity->store, MOB_ID_ZOMBIE, feet, 0.5f32);
    const float64_t feet2[3] = {12.5f64, 12.5f64, 3.0f64};
    (void)mob_spawn_body(fs->spawn, &entity->store, MOB_ID_SKELETON, feet2, 1.5f32);
    /* By the player, its fuse nearly run, so that what a blast does is met by most inputs made from this one. */
    const float64_t feet3[3] = {5.5f64, 5.5f64, 1.0f64};
    const entity_handle_t third = mob_spawn_body(fs->spawn, &entity->store, MOB_ID_CREEPER, feet3, 2.5f32);
    mob_creeper_body_t *const lit = &fs->mob->creeper.borrowed_bodies[entity_handle_slot(third)];
    lit->owner = third;
    lit->state = MOB_CREEPER_STATE_CHASING;
    lit->lit = 1u;
    lit->fuse = MOB_CREEPER_FUSE_TICKS - 4u;
    const float64_t origin[3] = {2.5f64, 2.5f64, 5.0f64};
    const float64_t along[3] = {1.0f64, 0.25f64, 0.1f64};
    projectile_fire(&entity->projectiles, third, origin, along, 1.6f64, 1.0f64);
    /* Already after the player, so that it asks the way at once and is not one in ten a tick. Without an owner a mind is taken as a new one's, and starts over. */
    mob_zombie_body_t *const chasing = &fs->mob->zombie.borrowed_bodies[entity_handle_slot(first)];
    chasing->owner = first;
    chasing->chasing = 1u;
    chasing->unseen = 3u;

    save_state_parts_t parts = fs_parts(fs);
    parts.time.world_ticks = 1234u;
    save_state_capture(&state, &parts);
    FS_MUST(save_state_write(&state, FS_GENERATION) == SAVE_WORLD_OK);
    save_state_close(&state);
    fs_parts_close(fs);

    char path[FS_NAME_BYTES];
    (void)snprintf(path, sizeof(path), "%s/state.%" PRIu64, fs->root, (uint64_t)FS_GENERATION);
    uint64_t bytes = 0u;
    const uint8_t *const file = fuzz_file_read(t, path, FS_INPUT_BYTES, &bytes);
    FS_MUST(file != nullptr);
    uint8_t *const seed = test_alloc(t, bytes + 1u);
    seed[0] = FS_MODE_STATE;
    memcpy(seed + 1u, file, bytes);

    uint8_t *const fixed = test_alloc(t, bytes + 1u);
    memcpy(fixed, seed, bytes + 1u);
    fuzz_rng_t rng = {.state = 3u};
    fs_state_fix(fs, fixed + 1u, bytes, &rng);
    if (memcmp(fixed, seed, bytes + 1u) != 0) {
        fuzz_fail("fs_state_fix changed a state the game wrote: it no longer matches the format");
    }
    fuzz_seed_add(corpus, seed, bytes + 1u);
    fs->state_seed = seed;
    fs->state_seed_bytes = bytes + 1u;
    for (uint32_t kind = 0u; kind < 4u; kind++) {
        uint8_t *const limits = test_alloc(t, bytes + 1u);
        memcpy(limits, seed, bytes + 1u);
        fs_seed_at_limits(fs, corpus, limits, bytes, kind);
    }
    fs_directory_empty(fs->root);
}

static void fs_seeds(test_context_t *t, void *context, fuzz_corpus_t *corpus) {
    fs_context_t *const fs = context;
    fs_seeds_records(t, fs, corpus);
    fs_seeds_texts(corpus);
    fs_seeds_world(t, fs, corpus);
    fs_seeds_state(t, fs, corpus);
}

TEST(save) {
    fs_context_t *const fs = test_world_aligned(t, sizeof(*fs));
    fs->borrowed_test = t;
    REQUIRE(test_world_begin(t, &fs->w));
    test_world_fill(t, &fs->w, -16, -16, 0, 31, 31, 0, BLOCK_ID_STONE);
    fs->player = test_world_aligned(t, sizeof(player_t));
    fs->item = test_world_aligned(t, sizeof(item_t));
    fs->entity = test_world_aligned(t, sizeof(entity_t));
    fs->mob = test_world_aligned(t, sizeof(mob_t));
    fs->spawn = test_alloc(t, sizeof(mob_spawn_t));
    fs->held = test_alloc(t, sizeof(item_stack_t));
    fs->save = test_world_aligned(t, sizeof(save_t));
    player_init(fs->player, fs->w.physics);
    REQUIRE(item_init(fs->item));
    REQUIRE(entity_init(fs->entity, fs->w.physics));
    REQUIRE(mob_init(fs->mob, nullptr));
    REQUIRE(save_init(fs->save));
    fs->state_arena = test_world_aligned(t, sizeof(arena_t));
    arena_init(fs->state_arena, "fuzz state", test_world_aligned(t, SAVE_STATE_ARENA_BYTES), SAVE_STATE_ARENA_BYTES);
    fs->file_back = test_alloc(t, FS_INPUT_BYTES);

    /* On tmpfs if there is any, for files written and thrown away many times a second. */
    fs->root = test_alloc(t, FS_NAME_BYTES);
    fs->blocks = test_alloc(t, FS_NAME_BYTES);
    (void)snprintf(fs->root, FS_NAME_BYTES, "/tmp/mc_fuzz_save_XXXXXX");
    REQUIRE(mkdtemp(fs->root) != nullptr);
    (void)snprintf(fs->blocks, FS_NAME_BYTES, "%s/blocks", fs->root);
    REQUIRE(mkdir(fs->blocks, 0755) == 0);

    fs->bytes = fuzz_buffer_make(t, FS_INPUT_BYTES);
    fs->states = fuzz_buffer_make(t, FS_COLUMN_VOXELS * sizeof(uint16_t));
    fs->states_again = fuzz_buffer_make(t, SECTION_VOXELS * sizeof(uint16_t));
    fs->levels = fuzz_buffer_make(t, SAVE_WORLD_OVERLAY_LEVELS_BYTES);
    fs->scratch = fuzz_buffer_make(t, SAVE_WORLD_OVERLAY_SCRATCH_BYTES);
    fs->far_scratch = fuzz_buffer_make(t, SAVE_WORLD_FAR_SCRATCH_BYTES);
    fs->far_line = fuzz_buffer_make(t, FS_FAR_LINE_VOXELS * sizeof(uint16_t));
    fs->palette_slots = fuzz_buffer_make(t, 65536u * sizeof(uint16_t));
    /* An input is meant to be wrong, so every line a wrong one logs would only be noise. */
    const int32_t quiet = open("/dev/null", O_WRONLY | O_CLOEXEC);
    REQUIRE(quiet >= 0);
    const int32_t previous = log_set_fd(quiet);

    const fuzz_target_t target = {.name = "save",
                                  .borrowed_context = fs,
                                  .borrowed_tokens = nullptr,
                                  .input_capacity = FS_INPUT_BYTES,
                                  .seeds = fs_seeds,
                                  .fix = fs_fix,
                                  .mutate = fs_mutate,
                                  .execute = fs_execute,
                                  .borrowed_count_names = fs_count_names,
                                  .token_count = 0u,
                                  .count_name_count = (uint32_t)(sizeof(fs_count_names) / sizeof(fs_count_names[0])),
                                  .kind_count = FS_MODE_COUNT,
                                  ._pad0 = {0}};
    fuzz_run(t, &target);

    (void)log_set_fd(previous);
    (void)close(quiet);
    arena_deinit(fs->state_arena);
    save_deinit(fs->save);
    mob_deinit(fs->mob);
    entity_deinit(fs->entity);
    item_deinit(fs->item);
    player_deinit(fs->player);
    test_world_end(&fs->w);
    fs_directory_empty(fs->blocks);
    (void)rmdir(fs->blocks);
    fs_directory_empty(fs->root);
    (void)rmdir(fs->root);
}

TEST_MAIN()
