/* See README.md. */
#include "save/world/index.h"

#include "assert/index.h"
#include "log/index.h"
#include "tables/blocks/index.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>

static const uint8_t SAVE_WORLD_MANIFEST_MAGIC[8] = {'M', 'C', 'W', 'O', 'R', 'L', 'D', 'M'};
static const uint8_t SAVE_WORLD_DELTA_MAGIC[8] = {'M', 'C', 'W', 'D', 'E', 'L', 'T', 'A'};
static const char SAVE_WORLD_MANIFEST_NAME[] = "manifest";
static const char SAVE_WORLD_MANIFEST_TEMPORARY_NAME[] = "manifest.tmp"; /* what file_write_atomic calls it */
static const char SAVE_WORLD_LOCK_NAME[] = "lock";
static const char SAVE_WORLD_TEMPORARY_SUFFIX[] = ".tmp";

enum : uint64_t {
    SAVE_WORLD_HASH_START = 0xCBF29CE484222325ull,
    SAVE_WORLD_HASH_PRIME = 0x100000001B3ull,
    SAVE_WORLD_TABLE_BYTES = SAVE_WORLD_REGION_COLUMNS * sizeof(save_world_column_entry_t),
    SAVE_WORLD_RECORDS_START = sizeof(save_world_delta_header_t) + SAVE_WORLD_TABLE_BYTES,
    SAVE_WORLD_MANIFEST_BYTES_MAX =
        sizeof(save_world_manifest_header_t) + SAVE_WORLD_REGIONS_MAX * sizeof(save_world_region_t),
};
enum : uint32_t {
    SAVE_WORLD_PLACE_SECTIONS = 128u,
    /* How many times a reader follows a file replaced under it before it calls that damage. Each is a whole save happening between two
     * of its own steps, so even a second is rare. */
    SAVE_WORLD_READ_TRIES = 64u,
};
static_assert(SAVE_WORLD_PENDING_BYTES < UINT32_MAX);
static_assert(SAVE_WORLD_ENTRY_BYTES_MAX < UINT16_MAX);
static_assert((uint32_t)COLUMN_SECTIONS <= (uint32_t)SAVE_WORLD_PLACE_SECTIONS);

/* FNV-1a. It finds rot and torn writes, and keeps nobody out: no save holds a secret, and they are the player's to edit. */
static uint64_t save_world_hash_more(uint64_t hash, const void *data, uint64_t bytes) {
    const uint8_t *const cursor = data;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ cursor[i]) * SAVE_WORLD_HASH_PRIME;
    }
    return hash;
}

static uint64_t save_world_hash(const void *data, uint64_t bytes) {
    return save_world_hash_more(SAVE_WORLD_HASH_START, data, bytes);
}

static uint32_t save_world_record_checksum(const uint8_t *record, uint64_t bytes) {
    const uint64_t hash = save_world_hash(record, bytes);
    return (uint32_t)(hash ^ (hash >> 32));
}

const char *save_world_status_name(save_world_status_t status) {
    switch (status) {
    case SAVE_WORLD_OK:
        return "ok";
    case SAVE_WORLD_FULL:
        return "no room for more until a save";
    case SAVE_WORLD_ERROR_FILE:
        return "the disk refused";
    case SAVE_WORLD_ERROR_LOCKED:
        return "already open elsewhere";
    case SAVE_WORLD_ERROR_DAMAGED:
        return "damaged";
    case SAVE_WORLD_ERROR_VERSION:
        return "from another version";
    case SAVE_WORLD_ERROR_SEED:
        return "made on another seed";
    case SAVE_WORLD_ERROR_TABLES:
        return "made with other blocks";
    case SAVE_WORLD_ERROR_TOO_MANY:
        return "too many regions";
    case SAVE_WORLD_ERROR_PATH:
        return "path too long";
    }
    return "?";
}

uint64_t save_world_table_hash(void) {
    uint64_t hash = SAVE_WORLD_HASH_START;
    for (uint32_t i = 0u; i < BLOCK_COUNT; i++) {
        /* With its NUL, so that "ab", "c" is not "a", "bc". */
        hash = save_world_hash_more(hash, block_defs[i].name, strlen(block_defs[i].name) + 1u);
    }
    return hash;
}

static void save_world_step(const save_world_t *save, save_world_step_t step) {
    if (save->step_hook != nullptr) {
        save->step_hook(save->borrowed_step_context, step);
    }
}

void save_world_levels_from(save_world_t *save, const world_tick_t *tick) {
    save->borrowed_tick = tick;
}

void save_world_step_hook_set(save_world_t *save, save_world_step_hook_t hook, void *context) {
    save->step_hook = hook;
    save->borrowed_step_context = context;
}

/* ---- What has been captured ----------------------------------------------------------------------------------------- */

static uint32_t save_world_pending_slot_start(int32_t chunk_x, int32_t chunk_y, uint32_t section) {
    const uint32_t mixed =
        ((uint32_t)chunk_x * 0x9E3779B1u) ^ ((uint32_t)chunk_y * 0x85EBCA77u) ^ (section * 0xC2B2AE3Du);
    return (mixed ^ (mixed >> 15)) & (SAVE_WORLD_PENDING_HASH_SLOTS - 1u);
}

/* That section's ref, or nullptr. With the mutex held, or by the one thread that writes this set. */
static save_world_pending_ref_t *save_world_pending_find(const save_world_pending_t *pending, int32_t chunk_x,
                                                         int32_t chunk_y, uint32_t section) {
    if (pending->count == 0u) {
        return nullptr;
    }
    uint32_t slot = save_world_pending_slot_start(chunk_x, chunk_y, section);
    while (pending->borrowed_hash[slot] != 0u) {
        save_world_pending_ref_t *const ref = &pending->borrowed_refs[pending->borrowed_hash[slot] - 1u];
        if (ref->chunk_x == chunk_x && ref->chunk_y == chunk_y && ref->section == section) {
            return ref;
        }
        slot = (slot + 1u) & (SAVE_WORLD_PENDING_HASH_SLOTS - 1u);
    }
    return nullptr;
}

static void save_world_pending_clear(save_world_pending_t *pending) {
    if (pending->count != 0u) {
        memset(pending->borrowed_hash, 0, SAVE_WORLD_PENDING_HASH_SLOTS * sizeof(uint32_t));
    }
    pending->count = 0u;
    pending->used = 0u;
}

/* One section of a live column into the set being captured to. False if there is no room, and then nothing changed. */
static bool save_world_capture_section(save_world_t *save, const column_t *column, uint32_t column_slot,
                                       uint32_t section) {
    save_world_pending_t *const pending = &save->pending[save->active];
    if (pending->count >= SAVE_WORLD_PENDING_SECTIONS ||
        pending->used + SAVE_WORLD_ENTRY_BYTES_MAX > SAVE_WORLD_PENDING_BYTES) {
        return false;
    }
    section_blocks_export(&column->sections[section], save->borrowed_states);
    /* Past used, no reader looks: so this needs no lock, and only the moment it comes to be seen does. */
    uint8_t *const record = pending->borrowed_bytes + pending->used;
    uint32_t bytes = save_world_record_encode(save->borrowed_states, save->borrowed_states + SECTION_VOXELS, record);
    const uint8_t *const levels =
        save->borrowed_tick != nullptr ? world_tick_section_levels(save->borrowed_tick, column_slot, section) : nullptr;
    if (levels != nullptr) {
        uint16_t bits = 0u;
        memcpy(&bits, record, sizeof(bits));
        bits |= SAVE_WORLD_RECORD_HAS_LEVELS;
        memcpy(record, &bits, sizeof(bits));
        memcpy(record + bytes, levels, SAVE_WORLD_RECORD_LEVELS_BYTES);
        bytes += SAVE_WORLD_RECORD_LEVELS_BYTES;
    }
    const uint32_t offset = (uint32_t)pending->used;
    save_world_pending_ref_t *const found = save_world_pending_find(pending, column->chunk_x, column->chunk_y, section);
    (void)pthread_mutex_lock(&save->mutex);
    if (found != nullptr) {
        /* The newer replaces it. The older's bytes stay, unused, until this set has been written. */
        found->offset = offset;
        found->bytes = (uint16_t)bytes;
    } else {
        uint32_t slot = save_world_pending_slot_start(column->chunk_x, column->chunk_y, section);
        while (pending->borrowed_hash[slot] != 0u) {
            slot = (slot + 1u) & (SAVE_WORLD_PENDING_HASH_SLOTS - 1u);
        }
        pending->borrowed_refs[pending->count] = (save_world_pending_ref_t){.chunk_x = column->chunk_x,
                                                                            .chunk_y = column->chunk_y,
                                                                            .offset = offset,
                                                                            .bytes = (uint16_t)bytes,
                                                                            .section = (uint8_t)section,
                                                                            ._pad0 = {0}};
        pending->count++;
        pending->borrowed_hash[slot] = pending->count;
    }
    pending->used += bytes;
    (void)pthread_mutex_unlock(&save->mutex);
    return true;
}

/* Tells the far field that what it has of this column may be out of date. By the sim, or whichever one thread is capturing. */
static void save_world_far_note(save_world_t *save, int32_t chunk_x, int32_t chunk_y) {
    save_world_far_t *const far = save->borrowed_far;
    const uint32_t head = atomic_load_explicit(&far->head, memory_order_relaxed);
    /* Acquire pairs with the release in save_world_far_next: the reader has finished with what it has moved past. */
    const uint32_t tail = atomic_load_explicit(&far->tail, memory_order_acquire);
    if (head - tail >= SAVE_WORLD_FAR_RING) {
        atomic_fetch_add_explicit(&far->lost, 1u, memory_order_relaxed);
        return;
    }
    const uint32_t place = head & (SAVE_WORLD_FAR_RING - 1u);
    far->ring[place * 2u] = chunk_x;
    far->ring[place * 2u + 1u] = chunk_y;
    /* Release gives the reader the pair just written. */
    atomic_store_explicit(&far->head, head + 1u, memory_order_release);
}

bool save_world_far_next(save_world_t *save, int32_t *out_chunk_x, int32_t *out_chunk_y) {
    save_world_far_t *const far = save->borrowed_far;
    const uint32_t tail = atomic_load_explicit(&far->tail, memory_order_relaxed);
    /* Acquire pairs with the release in save_world_far_note. */
    if (tail == atomic_load_explicit(&far->head, memory_order_acquire)) {
        return false;
    }
    const uint32_t place = tail & (SAVE_WORLD_FAR_RING - 1u);
    *out_chunk_x = far->ring[place * 2u];
    *out_chunk_y = far->ring[place * 2u + 1u];
    atomic_store_explicit(&far->tail, tail + 1u, memory_order_release);
    return true;
}

bool save_world_far_lost_take(save_world_t *save) {
    /* Only a flag: whatever is done about it reads the sections themselves, under their own lock. */
    return atomic_exchange_explicit(&save->borrowed_far->lost, 0u, memory_order_relaxed) != 0u;
}

/* What of mask it could not capture. */
static column_mask_t save_world_capture_mask(save_world_t *save, const column_t *column, uint32_t slot,
                                             column_mask_t mask) {
    const column_mask_t asked = mask;
    while (column_mask_any(mask)) {
        const uint32_t section = column_mask_lowest(mask);
        if (!save_world_capture_section(save, column, slot, section)) {
            break;
        }
        mask = column_mask_drop_lowest(mask);
    }
    if (!column_mask_equal(mask, asked)) {
        save_world_far_note(save, column->chunk_x, column->chunk_y);
    }
    return mask;
}

save_world_status_t save_world_capture_dirty(save_world_t *save, world_edit_t *edit) {
    world_edit_dirty_t dirty;
    while (world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_SAVE, &dirty)) {
        const column_t *const column = &edit->borrowed_map->borrowed_columns[dirty.slot];
        const column_mask_t left = save_world_capture_mask(save, column, dirty.slot, dirty.section_mask);
        if (column_mask_any(left)) {
            dirty.section_mask = left;
            world_edit_dirty_restore(edit, &dirty, WORLD_EDIT_CONSUMER_SAVE);
            return SAVE_WORLD_FULL;
        }
    }
    return SAVE_WORLD_OK;
}

save_world_status_t save_world_capture_column(save_world_t *save, world_edit_t *edit, uint32_t slot) {
    const column_mask_t mask = world_edit_dirty_peek(edit, slot, WORLD_EDIT_CONSUMER_SAVE);
    if (!column_mask_any(mask)) {
        return SAVE_WORLD_OK;
    }
    const column_mask_t left = save_world_capture_mask(save, &edit->borrowed_map->borrowed_columns[slot], slot, mask);
    world_edit_dirty_clear(edit, slot, WORLD_EDIT_CONSUMER_SAVE, column_mask_without(mask, left));
    return !column_mask_any(left) ? SAVE_WORLD_OK : SAVE_WORLD_FULL;
}

bool save_world_commit_begin(save_world_t *save) {
    const uint8_t other = save->active == 0u ? 1u : 0u;
    if (save->pending[other].count != 0u) {
        return true; /* the last write failed, and still has it */
    }
    if (save->pending[save->active].count == 0u) {
        return false;
    }
    (void)pthread_mutex_lock(&save->mutex);
    save->active = other;
    (void)pthread_mutex_unlock(&save->mutex);
    return true;
}

/* ---- Names ---------------------------------------------------------------------------------------------------------- */

static bool save_world_path_of(const save_world_t *save, char out[FILE_PATH_BYTES], const char *name) {
    const int32_t written = snprintf(out, FILE_PATH_BYTES, "%s/%s", save->borrowed_directory, name);
    return written > 0 && written < FILE_PATH_BYTES;
}

static bool save_world_delta_path(const save_world_t *save, char out[FILE_PATH_BYTES], const char *prefix,
                                  int32_t region_x, int32_t region_y, uint64_t generation, const char *suffix) {
    const int32_t written = snprintf(out, FILE_PATH_BYTES, "%s/%s.%" PRId32 ".%" PRId32 ".%" PRIu64 "%s",
                                     save->borrowed_directory, prefix, region_x, region_y, generation, suffix);
    return written > 0 && written < FILE_PATH_BYTES;
}

/* ---- The manifest --------------------------------------------------------------------------------------------------- */

static int32_t save_world_region_order(int32_t left_x, int32_t left_y, int32_t right_x, int32_t right_y) {
    if (left_x != right_x) {
        return left_x < right_x ? -1 : 1;
    }
    if (left_y != right_y) {
        return left_y < right_y ? -1 : 1;
    }
    return 0;
}

static const save_world_region_t *save_world_region_find(const save_world_region_t *regions, uint32_t count,
                                                         int32_t region_x, int32_t region_y) {
    uint32_t low = 0u;
    uint32_t high = count;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2u;
        const int32_t order =
            save_world_region_order(region_x, region_y, regions[middle].region_x, regions[middle].region_y);
        if (order == 0) {
            return &regions[middle];
        }
        if (order < 0) {
            high = middle;
        } else {
            low = middle + 1u;
        }
    }
    return nullptr;
}

static uint64_t save_world_manifest_build(const save_world_t *save, const save_world_region_t *regions,
                                          uint32_t region_count, uint64_t generation) {
    save_world_manifest_header_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, SAVE_WORLD_MANIFEST_MAGIC, sizeof(header.magic));
    header.seed = save->seed;
    header.generation = generation;
    header.table_hash = save->table_hash;
    const uint64_t regions_bytes = (uint64_t)region_count * sizeof(save_world_region_t);
    uint8_t *const out = save->borrowed_manifest_bytes;
    uint8_t *const out_regions = out + sizeof(header);
    for (uint32_t i = 0u; i < region_count; i++) {
        save_world_region_t region = regions[i];
        region.damaged = 0u;
        memcpy(out_regions + (uint64_t)i * sizeof(region), &region, sizeof(region));
    }
    header.regions_checksum = save_world_hash(out_regions, regions_bytes);
    header.version = SAVE_WORLD_VERSION;
    header.header_bytes = sizeof(header);
    header.region_count = region_count;
    header.checksum = save_world_hash(&header, offsetof(save_world_manifest_header_t, checksum));
    memcpy(out, &header, sizeof(header));
    return sizeof(header) + regions_bytes;
}

/* What is in borrowed_manifest_bytes, as read, into the current manifest. */
static save_world_status_t save_world_manifest_parse(save_world_t *save, uint64_t bytes) {
    save_world_manifest_header_t header;
    if (bytes < sizeof(header)) {
        LOG_ERROR("save: the manifest of %s is %" PRIu64 " bytes, too short for its header", save->borrowed_directory,
                  bytes);
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    memcpy(&header, save->borrowed_manifest_bytes, sizeof(header));
    if (memcmp(header.magic, SAVE_WORLD_MANIFEST_MAGIC, sizeof(header.magic)) != 0 ||
        header.checksum != save_world_hash(&header, offsetof(save_world_manifest_header_t, checksum))) {
        LOG_ERROR("save: the manifest of %s has a bad header", save->borrowed_directory);
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    /* All of these are read only once the checksum says they are what was written. */
    if (header.version != SAVE_WORLD_VERSION) {
        LOG_ERROR("save: %s is in version %" PRIu32 " of the format, and this game reads version %" PRIu32,
                  save->borrowed_directory, header.version, (uint32_t)SAVE_WORLD_VERSION);
        return SAVE_WORLD_ERROR_VERSION;
    }
    if (header.seed != save->seed) {
        LOG_ERROR("save: %s holds changes to the world of seed %" PRIu64 ", and was opened as seed %" PRIu64,
                  save->borrowed_directory, header.seed, save->seed);
        return SAVE_WORLD_ERROR_SEED;
    }
    if (header.table_hash != save->table_hash) {
        LOG_ERROR("save: %s was saved with another block table, so its numbers would mean the wrong blocks",
                  save->borrowed_directory);
        return SAVE_WORLD_ERROR_TABLES;
    }
    if (header.header_bytes != sizeof(header) || header.region_count > SAVE_WORLD_REGIONS_MAX) {
        LOG_ERROR("save: the manifest of %s has a bad header", save->borrowed_directory);
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    const uint64_t regions_bytes = (uint64_t)header.region_count * sizeof(save_world_region_t);
    if (bytes != sizeof(header) + regions_bytes ||
        header.regions_checksum != save_world_hash(save->borrowed_manifest_bytes + sizeof(header), regions_bytes)) {
        LOG_ERROR("save: the manifest of %s has a bad list of regions", save->borrowed_directory);
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    save_world_region_t *const regions = save->borrowed_manifest[0];
    memcpy(regions, save->borrowed_manifest_bytes + sizeof(header), regions_bytes);
    for (uint32_t i = 0u; i < header.region_count; i++) {
        const bool sorted = i == 0u || save_world_region_order(regions[i - 1u].region_x, regions[i - 1u].region_y,
                                                               regions[i].region_x, regions[i].region_y) < 0;
        if (!sorted || regions[i].generation > header.generation || regions[i].damaged != 0u) {
            LOG_ERROR("save: the manifest of %s has a bad list of regions", save->borrowed_directory);
            return SAVE_WORLD_ERROR_DAMAGED;
        }
    }
    save->generation = header.generation;
    save->region_count = header.region_count;
    save->current = 0u;
    return SAVE_WORLD_OK;
}

/* Every file the manifest names, at the size it says. A crash cannot do this to a save, since a manifest comes after what it names has
 * reached the disk: so it is something else, and worth stopping for before anything is written. */
static save_world_status_t save_world_manifest_check_files(const save_world_t *save) {
    char path[FILE_PATH_BYTES];
    const save_world_region_t *const regions = save->borrowed_manifest[save->current];
    for (uint32_t i = 0u; i < save->region_count; i++) {
        if (!save_world_delta_path(save, path, "r", regions[i].region_x, regions[i].region_y, regions[i].generation,
                                   "")) {
            return SAVE_WORLD_ERROR_PATH;
        }
        uint64_t bytes = 0u;
        const file_status_t status = file_path_size(path, &bytes);
        if (status != FILE_OK) {
            LOG_ERROR("save: %s, which the manifest needs: %s", path, file_status_name(status));
            return SAVE_WORLD_ERROR_DAMAGED;
        }
        if (bytes != regions[i].file_bytes) {
            LOG_ERROR("save: %s is %" PRIu64 " bytes, and the manifest says %" PRIu64, path, bytes,
                      regions[i].file_bytes);
            return SAVE_WORLD_ERROR_DAMAGED;
        }
    }
    return SAVE_WORLD_OK;
}

/* Whether this is a name a delta file gets, and which it is. */
static bool save_world_delta_name_parse(const char *name, int32_t *out_x, int32_t *out_y, uint64_t *out_generation) {
    int32_t used = 0;
    return sscanf(name, "r.%" SCNd32 ".%" SCNd32 ".%" SCNu64 "%n", out_x, out_y, out_generation, &used) == 3 &&
           name[used] == '\0';
}

/* Whether this is what a save writes before a rename: r.<x>.<y>.<g>.tmp or manifest.tmp. */
static bool save_world_name_is_temporary(const char *name) {
    if (strcmp(name, SAVE_WORLD_MANIFEST_TEMPORARY_NAME) == 0) {
        return true;
    }
    const uint64_t bytes = strlen(name);
    const uint64_t suffix = sizeof(SAVE_WORLD_TEMPORARY_SUFFIX) - 1u;
    if (bytes <= suffix || bytes >= 128u || strcmp(name + bytes - suffix, SAVE_WORLD_TEMPORARY_SUFFIX) != 0) {
        return false;
    }
    char without[128];
    memcpy(without, name, bytes - suffix);
    without[bytes - suffix] = '\0';
    int32_t region_x = 0;
    int32_t region_y = 0;
    uint64_t generation = 0u;
    return save_world_delta_name_parse(without, &region_x, &region_y, &generation);
}

/* Goes through the directory. With remove_them, takes away what an interrupted save left: temporary files, and delta files the manifest does
 * not name. Only names this code makes: anything else in there is someone's, and a file kept as damaged is the player's. has_deltas says
 * whether it saw any delta files, named or not. */
static save_world_status_t save_world_directory_tidy(const save_world_t *save, bool remove_them, bool *has_deltas) {
    DIR *const directory = opendir(save->borrowed_directory);
    if (directory == nullptr) {
        LOG_ERROR("save: could not look in %s: %s", save->borrowed_directory, strerror(errno));
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    *has_deltas = false;
    char path[FILE_PATH_BYTES];
    const save_world_region_t *const regions = save->borrowed_manifest[save->current];
    for (const struct dirent *entry = readdir(directory); entry != nullptr; entry = readdir(directory)) {
        const char *const name = entry->d_name;
        int32_t region_x = 0;
        int32_t region_y = 0;
        uint64_t generation = 0u;
        const bool delta = save_world_delta_name_parse(name, &region_x, &region_y, &generation);
        bool leftover = save_world_name_is_temporary(name);
        if (delta) {
            *has_deltas = true;
            const save_world_region_t *const named =
                save_world_region_find(regions, save->region_count, region_x, region_y);
            leftover = named == nullptr || named->generation != generation;
        }
        if (!remove_them || !leftover || !save_world_path_of(save, path, name)) {
            continue;
        }
        const file_status_t status = file_remove(path);
        if (status != FILE_OK) {
            LOG_WARN("save: could not remove %s, left by a save that did not finish: %s", path,
                     file_status_name(status));
        } else {
            LOG_INFO("save: removed %s, left by a save that did not finish", path);
        }
    }
    (void)closedir(directory);
    return SAVE_WORLD_OK;
}

/* ---- Opening and closing ---------------------------------------------------------------------------------------------- */

static void save_world_pending_take(save_world_pending_t *pending, arena_t *arena) {
    pending->borrowed_bytes = arena_alloc(arena, SAVE_WORLD_PENDING_BYTES, 8u);
    pending->borrowed_refs = arena_alloc_array(arena, SAVE_WORLD_PENDING_SECTIONS, sizeof(save_world_pending_ref_t),
                                               alignof(save_world_pending_ref_t));
    pending->borrowed_hash =
        arena_alloc_zeroed(arena, SAVE_WORLD_PENDING_HASH_SLOTS * sizeof(uint32_t), alignof(uint32_t));
    pending->used = 0u;
    pending->count = 0u;
}

static save_world_status_t save_world_file_failed(save_world_t *save, const char *doing, const char *path,
                                                  file_status_t status) {
    LOG_ERROR("save: could not %s %s: %s", doing, path, file_status_name(status));
    save->file_status = (uint8_t)status;
    return SAVE_WORLD_ERROR_FILE;
}

/* A world with no manifest: new, or one whose first save never got that far. */
static save_world_status_t save_world_open_new(save_world_t *save, const char *path) {
    bool has_deltas = false;
    save_world_status_t status = save_world_directory_tidy(save, false, &has_deltas);
    if (status != SAVE_WORLD_OK) {
        return status;
    }
    /* The first thing a world writes is a manifest, so deltas with none mean it has been lost, and they are what is left. */
    if (has_deltas) {
        LOG_ERROR("save: %s has delta files and no manifest", save->borrowed_directory);
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    status = save_world_directory_tidy(save, true, &has_deltas);
    if (status != SAVE_WORLD_OK) {
        return status;
    }
    const uint64_t bytes = save_world_manifest_build(save, nullptr, 0u, 0u);
    const file_status_t written = file_write_atomic(path, save->borrowed_manifest_bytes, bytes);
    return written == FILE_OK ? SAVE_WORLD_OK : save_world_file_failed(save, "write", path, written);
}

static save_world_status_t save_world_open_files(save_world_t *save, const char *world_directory) {
    char path[FILE_PATH_BYTES];
    const int32_t written = snprintf(save->borrowed_directory, FILE_PATH_BYTES, "%s/blocks", world_directory);
    if (written <= 0 || written >= FILE_PATH_BYTES) {
        LOG_ERROR("save: the path of a world in %s is too long", world_directory);
        return SAVE_WORLD_ERROR_PATH;
    }
    file_status_t status = file_make_dirs(save->borrowed_directory);
    if (status == FILE_OK) {
        status = file_sync_dir(world_directory);
    }
    if (status != FILE_OK) {
        return save_world_file_failed(save, "make", save->borrowed_directory, status);
    }
    if (!save_world_path_of(save, path, SAVE_WORLD_LOCK_NAME)) {
        return SAVE_WORLD_ERROR_PATH;
    }
    status = file_open(&save->lock, path, FILE_MODE_READ_WRITE);
    if (status != FILE_OK) {
        return save_world_file_failed(save, "open", path, status);
    }
    /* Advisory, and gone with the process however it dies, so a kill never leaves a world locked. */
    if (flock(save->lock.fd, LOCK_EX | LOCK_NB) != 0) {
        LOG_ERROR("save: %s is open in another game", save->borrowed_directory);
        return SAVE_WORLD_ERROR_LOCKED;
    }
    if (!save_world_path_of(save, path, SAVE_WORLD_MANIFEST_NAME)) {
        return SAVE_WORLD_ERROR_PATH;
    }
    uint64_t bytes = 0u;
    status = file_read_whole(path, save->borrowed_manifest_bytes, SAVE_WORLD_MANIFEST_BYTES_MAX, &bytes);
    if (status == FILE_ERROR_NOT_FOUND) {
        return save_world_open_new(save, path);
    }
    if (status == FILE_ERROR_TOO_LARGE) {
        LOG_ERROR("save: %s is larger than any manifest", path);
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    if (status != FILE_OK) {
        return save_world_file_failed(save, "read", path, status);
    }
    const save_world_status_t parsed = save_world_manifest_parse(save, bytes);
    if (parsed != SAVE_WORLD_OK) {
        return parsed;
    }
    const save_world_status_t checked = save_world_manifest_check_files(save);
    if (checked != SAVE_WORLD_OK) {
        return checked;
    }
    bool has_deltas = false;
    return save_world_directory_tidy(save, true, &has_deltas);
}

save_world_status_t save_world_open(save_world_t *save, arena_t *arena, const char *world_directory, uint64_t seed) {
    memset(save, 0, sizeof(*save));
    save->lock.fd = -1;
    save_world_pending_take(&save->pending[0], arena);
    save_world_pending_take(&save->pending[1], arena);
    save->borrowed_manifest[0] = arena_alloc_array(arena, SAVE_WORLD_REGIONS_MAX, sizeof(save_world_region_t), 8u);
    save->borrowed_manifest[1] = arena_alloc_array(arena, SAVE_WORLD_REGIONS_MAX, sizeof(save_world_region_t), 8u);
    save->borrowed_written = arena_alloc_array(arena, SAVE_WORLD_PENDING_SECTIONS, sizeof(save_world_region_t), 8u);
    save->borrowed_replaced = arena_alloc_array(arena, SAVE_WORLD_PENDING_SECTIONS, sizeof(save_world_region_t), 8u);
    save->borrowed_keys = arena_alloc_array(arena, SAVE_WORLD_PENDING_SECTIONS, sizeof(save_world_sort_key_t), 8u);
    save->borrowed_table_old = arena_alloc(arena, SAVE_WORLD_TABLE_BYTES, 8u);
    save->borrowed_table_new = arena_alloc(arena, SAVE_WORLD_TABLE_BYTES, 8u);
    save->borrowed_manifest_bytes = arena_alloc(arena, SAVE_WORLD_MANIFEST_BYTES_MAX, 8u);
    save->borrowed_record = arena_alloc(arena, SAVE_WORLD_ENTRY_BYTES_MAX, 8u);
    save->borrowed_states = arena_alloc_zeroed(arena, (SECTION_VOXELS + 65536u) * sizeof(uint16_t), alignof(uint16_t));
    save->borrowed_far = arena_alloc_zeroed(arena, sizeof(save_world_far_t), alignof(save_world_far_t));
    save->borrowed_directory = arena_alloc(arena, FILE_PATH_BYTES, 1u);
    save->seed = seed;
    save->table_hash = save_world_table_hash();
    const save_world_status_t status = save_world_open_files(save, world_directory);
    if (status != SAVE_WORLD_OK) {
        (void)file_close(&save->lock);
        return status;
    }
    const int32_t mutex = pthread_mutex_init(&save->mutex, nullptr);
    ASSERT_ALWAYS(mutex == 0);
    atomic_init(&save->sections_loaded, 0u);
    atomic_init(&save->sections_damaged, 0u);
    save->open = 1u;
    LOG_INFO("save: %s, at save %" PRIu64 ", with %" PRIu32 " regions changed from seed %" PRIu64,
             save->borrowed_directory, save->generation, save->region_count, seed);
    return SAVE_WORLD_OK;
}

void save_world_close(save_world_t *save) {
    if (save->open == 0u) {
        return;
    }
    const uint64_t unsaved = (uint64_t)save->pending[0].count + save->pending[1].count;
    if (unsaved != 0u) {
        LOG_WARN("save: closing %s with %" PRIu64 " sections never written", save->borrowed_directory, unsaved);
    }
    (void)pthread_mutex_destroy(&save->mutex);
    /* It wrote nothing, and closing is what gives up the lock. */
    (void)file_close(&save->lock);
    memset(save, 0, sizeof(*save));
    save->lock.fd = -1;
}

/* ---- Writing one region ---------------------------------------------------------------------------------------------- */

/* What one region's next file has come to so far. */
typedef struct save_world_delta_writer_t {
    const save_world_pending_t *borrowed_pending; /* 0 */
    const save_world_sort_key_t *borrowed_keys;   /* 8: this region's, in order */
    const file_t *borrowed_old_file;              /* 16 */
    const file_t *borrowed_new_file;              /* 24 */
    uint64_t old_bytes;                           /* 32: 0 with no old file */
    uint64_t at;                                  /* 40: the end of the new file */
    uint32_t key_count;                           /* 48 */
    uint32_t key;                                 /* 52: the next not yet written */
    uint32_t section_count;                       /* 56 */
    uint32_t damaged;                             /* 60: what of the old file could not be read */
    int32_t region_x;                             /* 64 */
    int32_t region_y;                             /* 68 */
} save_world_delta_writer_t;
static_assert(sizeof(save_world_delta_writer_t) == 72);

static bool save_world_delta_header_good(const save_world_t *save, const save_world_delta_header_t *header,
                                         const save_world_region_t *region) {
    return memcmp(header->magic, SAVE_WORLD_DELTA_MAGIC, sizeof(header->magic)) == 0 &&
           header->checksum == save_world_hash(header, offsetof(save_world_delta_header_t, checksum)) &&
           header->version == SAVE_WORLD_VERSION && header->header_bytes == sizeof(*header) &&
           header->seed == save->seed && header->generation == region->generation &&
           header->file_bytes == region->file_bytes && header->section_count == region->section_count &&
           header->region_x == region->region_x && header->region_y == region->region_y;
}

/* Whether a run of bytes is inside a file of that size, with no overflow. */
static bool save_world_within(uint64_t offset, uint64_t bytes, uint64_t file_bytes) {
    return offset <= file_bytes && bytes <= file_bytes - offset;
}

/* A column's entries, read and checked. False if they are not what its table entry says: then its records cannot be found, and either *out_status
 * says why, or it is damage. */
static bool save_world_entries_read(const file_t *file, uint64_t file_bytes, const save_world_column_entry_t *column,
                                    save_world_section_entry_t *out, file_status_t *out_status) {
    *out_status = FILE_OK;
    if (column_mask_any(column_mask_without(column->section_mask, COLUMN_MASK_ALL))) {
        return false;
    }
    const uint64_t bytes = (uint64_t)column_mask_count(column->section_mask) * sizeof(save_world_section_entry_t);
    if (!save_world_within(column->entries_offset, bytes, file_bytes)) {
        return false;
    }
    const file_status_t status = file_read_at(file, column->entries_offset, out, bytes);
    if (status != FILE_OK) {
        *out_status = status == FILE_ERROR_END_OF_FILE ? FILE_OK : status;
        return false;
    }
    const uint64_t hash =
        save_world_hash_more(save_world_hash(column, offsetof(save_world_column_entry_t, checksum)), out, bytes);
    return hash == column->checksum;
}

/* One section of the old file into the record buffer. False, with *status FILE_OK, if it is damaged. */
static bool save_world_old_record_read(const save_world_t *save, const save_world_delta_writer_t *writer,
                                       const save_world_section_entry_t *entry, file_status_t *status) {
    *status = FILE_OK;
    if (entry->bytes > SAVE_WORLD_ENTRY_BYTES_MAX ||
        !save_world_within(entry->offset, entry->bytes, writer->old_bytes)) {
        return false;
    }
    const file_status_t read =
        file_read_at(writer->borrowed_old_file, entry->offset, save->borrowed_record, entry->bytes);
    if (read != FILE_OK) {
        *status = read == FILE_ERROR_END_OF_FILE ? FILE_OK : read;
        return false;
    }
    if (save_world_record_checksum(save->borrowed_record, entry->bytes) != entry->checksum) {
        return false;
    }
    /* A good checksum says only that it is what was written. A record no reader will take stays damaged for ever if it is carried over, so it goes, and its old file is kept. */
    uint16_t palette[SAVE_WORLD_PALETTE_MAX];
    uint32_t bits = 0u;
    uint32_t palette_count = 0u;
    return save_world_record_valid(save->borrowed_record, entry->bytes, &bits, &palette_count, palette);
}

/* One column of the new file: at local, its old sections with the set's over them. */
static file_status_t save_world_delta_column(const save_world_t *save, save_world_delta_writer_t *writer,
                                             uint32_t local) {
    save_world_section_entry_t old_entries[COLUMN_SECTIONS];
    save_world_section_entry_t new_entries[COLUMN_SECTIONS];
    const save_world_column_entry_t *const old_column = &save->borrowed_table_old[local];
    column_mask_t old_mask = old_column->section_mask;
    file_status_t status = FILE_OK;
    if (column_mask_any(old_mask) &&
        !save_world_entries_read(writer->borrowed_old_file, writer->old_bytes, old_column, old_entries, &status)) {
        if (status != FILE_OK) {
            return status;
        }
        writer->damaged += column_mask_count(column_mask_and(old_mask, COLUMN_MASK_ALL));
        old_mask = COLUMN_MASK_NONE;
        LOG_ERROR("save: column %" PRIu32 " of region %" PRId32 ", %" PRId32 " is past reading", local,
                  writer->region_x, writer->region_y);
    }
    column_mask_t new_mask = COLUMN_MASK_NONE;
    uint32_t new_count = 0u;
    uint32_t old_index = 0u;
    for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
        const bool in_old = column_mask_has(old_mask, section);
        const uint32_t place = local * SAVE_WORLD_PLACE_SECTIONS + section;
        const bool in_set = writer->key < writer->key_count && writer->borrowed_keys[writer->key].place == place;
        const uint8_t *record = nullptr;
        uint32_t bytes = 0u;
        uint32_t checksum = 0u;
        if (in_set) {
            const save_world_pending_t *const pending = writer->borrowed_pending;
            const save_world_pending_ref_t *const ref = &pending->borrowed_refs[writer->borrowed_keys[writer->key].ref];
            record = pending->borrowed_bytes + ref->offset;
            bytes = ref->bytes;
            checksum = save_world_record_checksum(record, bytes);
            writer->key++;
        } else if (in_old) {
            const save_world_section_entry_t *const entry = &old_entries[old_index];
            if (save_world_old_record_read(save, writer, entry, &status)) {
                record = save->borrowed_record;
                bytes = entry->bytes;
                checksum = entry->checksum;
            } else if (status != FILE_OK) {
                return status;
            } else {
                writer->damaged++;
                LOG_ERROR("save: section %" PRIu32 " of column %" PRIu32 " in region %" PRId32 ", %" PRId32
                          " is past reading",
                          section, local, writer->region_x, writer->region_y);
            }
        }
        old_index += in_old ? 1u : 0u;
        if (record == nullptr) {
            continue;
        }
        status = file_write_at(writer->borrowed_new_file, writer->at, record, bytes);
        if (status != FILE_OK) {
            return status;
        }
        new_entries[new_count] =
            (save_world_section_entry_t){.offset = writer->at, .bytes = bytes, .checksum = checksum};
        new_count++;
        new_mask = column_mask_or(new_mask, column_mask_of(section));
        writer->at += bytes;
    }
    if (new_count == 0u) {
        return FILE_OK;
    }
    const uint64_t entries_bytes = (uint64_t)new_count * sizeof(save_world_section_entry_t);
    status = file_write_at(writer->borrowed_new_file, writer->at, new_entries, entries_bytes);
    if (status != FILE_OK) {
        return status;
    }
    save_world_column_entry_t *const column = &save->borrowed_table_new[local];
    column->section_mask = new_mask;
    column->entries_offset = writer->at;
    column->checksum = save_world_hash_more(save_world_hash(column, offsetof(save_world_column_entry_t, checksum)),
                                            new_entries, entries_bytes);
    writer->at += entries_bytes;
    writer->section_count += new_count;
    return FILE_OK;
}

/* The new file's whole contents, up to its temporary name's being synced. */
static file_status_t save_world_delta_contents(const save_world_t *save, save_world_delta_writer_t *writer,
                                               uint64_t generation) {
    memset(save->borrowed_table_new, 0, SAVE_WORLD_TABLE_BYTES);
    writer->at = SAVE_WORLD_RECORDS_START;
    for (uint32_t local = 0u; local < SAVE_WORLD_REGION_COLUMNS; local++) {
        const file_status_t status = save_world_delta_column(save, writer, local);
        if (status != FILE_OK) {
            return status;
        }
    }
    ASSERT_ALWAYS(writer->key == writer->key_count);
    save_world_step(save, SAVE_WORLD_STEP_DELTA_RECORDS);
    save_world_delta_header_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, SAVE_WORLD_DELTA_MAGIC, sizeof(header.magic));
    header.seed = save->seed;
    header.generation = generation;
    header.file_bytes = writer->at;
    header.version = SAVE_WORLD_VERSION;
    header.header_bytes = sizeof(header);
    header.region_x = writer->region_x;
    header.region_y = writer->region_y;
    header.section_count = writer->section_count;
    header.checksum = save_world_hash(&header, offsetof(save_world_delta_header_t, checksum));
    file_status_t status =
        file_write_at(writer->borrowed_new_file, sizeof(header), save->borrowed_table_new, SAVE_WORLD_TABLE_BYTES);
    if (status == FILE_OK) {
        status = file_write_at(writer->borrowed_new_file, 0u, &header, sizeof(header));
    }
    return status == FILE_OK ? file_sync(writer->borrowed_new_file) : status;
}

/* Opens the file being replaced and reads its table, leaving zeros there if it is past reading, which goes down as damage. A failure of the disk,
 * which may pass, is returned instead: nothing should be given up for that. */
static file_status_t save_world_old_open(const save_world_t *save, const save_world_region_t *old, file_t *file,
                                         save_world_delta_writer_t *writer) {
    memset(save->borrowed_table_old, 0, SAVE_WORLD_TABLE_BYTES);
    if (old == nullptr) {
        return FILE_OK;
    }
    char path[FILE_PATH_BYTES];
    if (!save_world_delta_path(save, path, "r", old->region_x, old->region_y, old->generation, "")) {
        return FILE_ERROR_PATH_TOO_LONG;
    }
    file_status_t status = file_open(file, path, FILE_MODE_READ);
    if (status == FILE_ERROR_NOT_FOUND) {
        writer->damaged += old->section_count;
        LOG_ERROR("save: %s has gone, with what it held", path);
        return FILE_OK;
    }
    if (status != FILE_OK) {
        return status;
    }
    writer->old_bytes = old->file_bytes;
    save_world_delta_header_t header;
    status = file_read_at(file, 0u, &header, sizeof(header));
    if (status == FILE_OK) {
        status = file_read_at(file, sizeof(header), save->borrowed_table_old, SAVE_WORLD_TABLE_BYTES);
    }
    if (status == FILE_OK && save_world_delta_header_good(save, &header, old)) {
        return FILE_OK;
    }
    memset(save->borrowed_table_old, 0, SAVE_WORLD_TABLE_BYTES);
    if (status != FILE_OK && status != FILE_ERROR_END_OF_FILE) {
        return status;
    }
    writer->damaged += old->section_count;
    LOG_ERROR("save: %s is past reading, and is kept beside what replaces it", path);
    return FILE_OK;
}

/* One region's next file, under its temporary name and then its own: its old file's sections, with the set's over them. keys are the set's,
 * sorted, for this region. */
static save_world_status_t save_world_delta_write(save_world_t *save, const save_world_pending_t *pending,
                                                  const save_world_sort_key_t *keys, uint32_t key_count,
                                                  const save_world_region_t *old, uint64_t generation,
                                                  save_world_region_t *out) {
    char path[FILE_PATH_BYTES];
    char temporary[FILE_PATH_BYTES];
    if (!save_world_delta_path(save, path, "r", keys[0].region_x, keys[0].region_y, generation, "") ||
        !save_world_delta_path(save, temporary, "r", keys[0].region_x, keys[0].region_y, generation,
                               SAVE_WORLD_TEMPORARY_SUFFIX)) {
        return SAVE_WORLD_ERROR_PATH;
    }
    file_t old_file = {.fd = -1, ._pad0 = {0}};
    file_t new_file = {.fd = -1, ._pad0 = {0}};
    save_world_delta_writer_t writer = {.borrowed_pending = pending,
                                        .borrowed_keys = keys,
                                        .borrowed_old_file = &old_file,
                                        .borrowed_new_file = &new_file,
                                        .old_bytes = 0u,
                                        .at = 0u,
                                        .key_count = key_count,
                                        .key = 0u,
                                        .section_count = 0u,
                                        .damaged = 0u,
                                        .region_x = keys[0].region_x,
                                        .region_y = keys[0].region_y};
    file_status_t status = save_world_old_open(save, old, &old_file, &writer);
    if (status == FILE_OK) {
        status = file_open(&new_file, temporary, FILE_MODE_WRITE);
    }
    if (status == FILE_OK) {
        status = save_world_delta_contents(save, &writer, generation);
    }
    const file_status_t closed = file_close(&new_file);
    status = status == FILE_OK ? closed : status;
    /* It was only read. */
    (void)file_close(&old_file);
    if (status == FILE_OK) {
        save_world_step(save, SAVE_WORLD_STEP_DELTA_SYNCED);
        status = file_rename(temporary, path);
    }
    if (status != FILE_OK) {
        (void)file_remove(temporary);
        return save_world_file_failed(save, "write", temporary, status);
    }
    save_world_step(save, SAVE_WORLD_STEP_DELTA_RENAMED);
    *out = (save_world_region_t){.region_x = writer.region_x,
                                 .region_y = writer.region_y,
                                 .generation = generation,
                                 .file_bytes = writer.at,
                                 .section_count = writer.section_count,
                                 .damaged = writer.damaged};
    return SAVE_WORLD_OK;
}

/* ---- One save -------------------------------------------------------------------------------------------------------- */

static int32_t save_world_key_compare(const void *left_raw, const void *right_raw) {
    const save_world_sort_key_t *const left = left_raw;
    const save_world_sort_key_t *const right = right_raw;
    const int32_t order = save_world_region_order(left->region_x, left->region_y, right->region_x, right->region_y);
    if (order != 0) {
        return order;
    }
    /* No two keys share a place: a set holds a section once. */
    return left->place < right->place ? -1 : (left->place > right->place ? 1 : 0);
}

/* The next manifest's regions, into the one not current: both lists are sorted. False if there are too many. */
static bool save_world_manifest_merge(const save_world_t *save, uint32_t written_count, uint8_t next,
                                      uint32_t *out_count) {
    const save_world_region_t *const old = save->borrowed_manifest[save->current];
    const save_world_region_t *const written = save->borrowed_written;
    save_world_region_t *const out = save->borrowed_manifest[next];
    uint32_t old_at = 0u;
    uint32_t written_at = 0u;
    uint32_t count = 0u;
    while (old_at < save->region_count || written_at < written_count) {
        if (count == SAVE_WORLD_REGIONS_MAX) {
            return false;
        }
        int32_t order = 0;
        if (old_at == save->region_count) {
            order = 1;
        } else if (written_at == written_count) {
            order = -1;
        } else {
            order = save_world_region_order(old[old_at].region_x, old[old_at].region_y, written[written_at].region_x,
                                            written[written_at].region_y);
        }
        if (order < 0) {
            out[count] = old[old_at];
            old_at++;
        } else {
            out[count] = written[written_at];
            written_at++;
            old_at += order == 0 ? 1u : 0u;
        }
        count++;
    }
    *out_count = count;
    return true;
}

/* A save that did not happen leaves no file behind it. */
static void save_world_written_remove(const save_world_t *save, uint32_t count, uint64_t generation) {
    char path[FILE_PATH_BYTES];
    for (uint32_t i = 0u; i < count; i++) {
        if (save_world_delta_path(save, path, "r", save->borrowed_written[i].region_x,
                                  save->borrowed_written[i].region_y, generation, "")) {
            (void)file_remove(path);
        }
    }
}

/* Every file this save has replaced. A crash before this is what save_world_open tidies up. */
static void save_world_replaced_remove(const save_world_t *save, uint32_t count) {
    char path[FILE_PATH_BYTES];
    char kept[FILE_PATH_BYTES];
    for (uint32_t i = 0u; i < count; i++) {
        const save_world_region_t *const replaced = &save->borrowed_replaced[i];
        if (!save_world_delta_path(save, path, "r", replaced->region_x, replaced->region_y, replaced->generation, "")) {
            continue;
        }
        if (replaced->damaged != 0u) {
            /* It may be all there is of what could not be read. */
            if (save_world_delta_path(save, kept, "damaged", replaced->region_x, replaced->region_y,
                                      replaced->generation, "") &&
                file_rename(path, kept) != FILE_OK) {
                LOG_WARN("save: could not keep %s as %s", path, kept);
            }
        } else if (file_remove(path) != FILE_OK) {
            LOG_WARN("save: could not remove %s, which the next open will try again", path);
        }
    }
    /* Only tidiness is lost without this: the next open removes whatever comes back. */
    (void)file_sync_dir(save->borrowed_directory);
}

/* Replaces the manifest: this one rename is the save happening. On failure, leaves no new delta file unless the new manifest may be what is there. */
static save_world_status_t save_world_manifest_commit(save_world_t *save, uint8_t next, uint32_t next_count,
                                                      uint32_t written_count, uint64_t generation) {
    char path[FILE_PATH_BYTES];
    if (!save_world_path_of(save, path, SAVE_WORLD_MANIFEST_NAME)) {
        save_world_written_remove(save, written_count, generation);
        return SAVE_WORLD_ERROR_PATH;
    }
    const uint64_t bytes = save_world_manifest_build(save, save->borrowed_manifest[next], next_count, generation);
    const file_status_t status = file_write_atomic(path, save->borrowed_manifest_bytes, bytes);
    if (status == FILE_OK) {
        return SAVE_WORLD_OK;
    }
    /* file_write_atomic can fail on its last sync with the new manifest in place, which must keep the files it names: so they go only if it is
     * the old one that is there. A next try writes the same names with the same contents. */
    uint64_t on_disk = 0u;
    if (file_read_whole(path, save->borrowed_manifest_bytes, SAVE_WORLD_MANIFEST_BYTES_MAX, &on_disk) == FILE_OK &&
        on_disk >= sizeof(save_world_manifest_header_t)) {
        save_world_manifest_header_t header;
        memcpy(&header, save->borrowed_manifest_bytes, sizeof(header));
        if (header.generation == save->generation) {
            save_world_written_remove(save, written_count, generation);
        }
    }
    return save_world_file_failed(save, "write", path, status);
}

save_world_status_t save_world_commit_write(save_world_t *save) {
    ASSERT_ALWAYS(save->open != 0u);
    save_world_pending_t *const pending = &save->pending[save->active == 0u ? 1u : 0u];
    if (pending->count == 0u) {
        return SAVE_WORLD_OK;
    }
    /* In an order that decides what goes where: the same changes make the same files, whatever order they came in. */
    for (uint32_t i = 0u; i < pending->count; i++) {
        const save_world_pending_ref_t *const ref = &pending->borrowed_refs[i];
        const uint32_t local = (((uint32_t)ref->chunk_y & (SAVE_WORLD_REGION_SIDE - 1u)) << SAVE_WORLD_REGION_SHIFT) |
                               ((uint32_t)ref->chunk_x & (SAVE_WORLD_REGION_SIDE - 1u));
        save->borrowed_keys[i] = (save_world_sort_key_t){.region_x = ref->chunk_x >> SAVE_WORLD_REGION_SHIFT,
                                                         .region_y = ref->chunk_y >> SAVE_WORLD_REGION_SHIFT,
                                                         .place = local * SAVE_WORLD_PLACE_SECTIONS + ref->section,
                                                         .ref = i};
    }
    qsort(save->borrowed_keys, pending->count, sizeof(save_world_sort_key_t), save_world_key_compare);
    /* Only an edited manifest gets here. The count going round would make the regions it keeps newer than the manifest, which is refused: a world that will not open. So the world stays as it is, and the player is told a save failed. */
    if (save->generation == UINT64_MAX) {
        LOG_ERROR("save: the manifest of %s is at the last generation there is, so this save was not made",
                  save->borrowed_directory);
        return SAVE_WORLD_ERROR_TOO_MANY;
    }
    const uint64_t generation = save->generation + 1u;
    uint32_t written_count = 0u;
    uint32_t replaced_count = 0u;
    for (uint32_t start = 0u; start < pending->count;) {
        const save_world_sort_key_t *const first = &save->borrowed_keys[start];
        uint32_t end = start + 1u;
        while (end < pending->count && save->borrowed_keys[end].region_x == first->region_x &&
               save->borrowed_keys[end].region_y == first->region_y) {
            end++;
        }
        const save_world_region_t *const old = save_world_region_find(
            save->borrowed_manifest[save->current], save->region_count, first->region_x, first->region_y);
        save_world_region_t *const written = &save->borrowed_written[written_count];
        const save_world_status_t status =
            save_world_delta_write(save, pending, first, end - start, old, generation, written);
        if (status != SAVE_WORLD_OK) {
            save_world_written_remove(save, written_count, generation);
            return status;
        }
        written_count++;
        if (old != nullptr) {
            save->borrowed_replaced[replaced_count] = *old;
            save->borrowed_replaced[replaced_count].damaged = written->damaged;
            replaced_count++;
        }
        start = end;
    }
    /* The new files must be on the disk before anything refers to them. */
    const file_status_t synced = file_sync_dir(save->borrowed_directory);
    if (synced != FILE_OK) {
        save_world_written_remove(save, written_count, generation);
        return save_world_file_failed(save, "sync", save->borrowed_directory, synced);
    }
    save_world_step(save, SAVE_WORLD_STEP_DELTAS_DONE);
    const uint8_t next = save->current == 0u ? 1u : 0u;
    uint32_t next_count = 0u;
    if (!save_world_manifest_merge(save, written_count, next, &next_count)) {
        LOG_ERROR("save: a world may change no more than %" PRIu32 " regions, so this save was not made",
                  (uint32_t)SAVE_WORLD_REGIONS_MAX);
        save_world_written_remove(save, written_count, generation);
        return SAVE_WORLD_ERROR_TOO_MANY;
    }
    const save_world_status_t committed = save_world_manifest_commit(save, next, next_count, written_count, generation);
    if (committed != SAVE_WORLD_OK) {
        return committed;
    }
    save_world_step(save, SAVE_WORLD_STEP_MANIFEST_DONE);
    const uint32_t sections = pending->count;
    /* Both at once for a reader, who looks at the sets and only then at the manifest: what it misses in one it finds in the other. */
    (void)pthread_mutex_lock(&save->mutex);
    save->current = next;
    save->region_count = next_count;
    save->generation = generation;
    save_world_pending_clear(pending);
    (void)pthread_mutex_unlock(&save->mutex);
    save->sections_written += sections;
    save->saves_done++;
    save_world_replaced_remove(save, replaced_count);
    save_world_step(save, SAVE_WORLD_STEP_REPLACED_GONE);
    return SAVE_WORLD_OK;
}

save_world_status_t save_world_flush(save_world_t *save, world_edit_t *edit) {
    for (;;) {
        const save_world_status_t captured = save_world_capture_dirty(save, edit);
        while (save_world_commit_begin(save)) {
            const save_world_status_t written = save_world_commit_write(save);
            if (written != SAVE_WORLD_OK) {
                return written;
            }
        }
        if (captured == SAVE_WORLD_OK) {
            return SAVE_WORLD_OK;
        }
    }
}

/* ---- Reading --------------------------------------------------------------------------------------------------------- */

/* A section just read: its levels too, if it has any and they are wanted. */
static void save_world_overlay_levels_take(const uint8_t *record, uint64_t bytes, uint32_t section, uint8_t *levels,
                                           column_mask_t *levels_mask) {
    const uint8_t *const held = save_world_record_levels(record, bytes);
    if (levels != nullptr && held != nullptr) {
        memcpy(levels + (uint64_t)section * SAVE_WORLD_RECORD_LEVELS_BYTES, held, SAVE_WORLD_RECORD_LEVELS_BYTES);
        *levels_mask = column_mask_or(*levels_mask, column_mask_of(section));
    }
}

/* The records of one column of an open file, past what is already in *mask. Returns how many it could not read. */
static uint64_t save_world_overlay_records(const file_t *file, const save_world_region_t *region,
                                           column_mask_t section_mask, const save_world_section_entry_t *entries,
                                           column_mask_t *mask, uint16_t *states, uint8_t *levels,
                                           column_mask_t *levels_mask, uint8_t *scratch) {
    uint64_t damaged = 0u;
    uint32_t index = 0u;
    for (column_mask_t left = section_mask; column_mask_any(left); left = column_mask_drop_lowest(left)) {
        const uint32_t section = column_mask_lowest(left);
        const save_world_section_entry_t *const entry = &entries[index];
        index++;
        if (column_mask_has(*mask, section)) {
            continue;
        }
        const bool good = entry->bytes <= SAVE_WORLD_ENTRY_BYTES_MAX &&
                          save_world_within(entry->offset, entry->bytes, region->file_bytes) &&
                          file_read_at(file, entry->offset, scratch, entry->bytes) == FILE_OK &&
                          save_world_record_checksum(scratch, entry->bytes) == entry->checksum &&
                          save_world_record_decode(scratch, entry->bytes, states + (uint64_t)section * SECTION_VOXELS);
        if (good) {
            *mask = column_mask_or(*mask, column_mask_of(section));
            save_world_overlay_levels_take(scratch, entry->bytes, section, levels, levels_mask);
        } else {
            damaged++;
            LOG_ERROR("save: section %" PRIu32 " of a column in region %" PRId32 ", %" PRId32 " is past reading",
                      section, region->region_x, region->region_y);
        }
    }
    return damaged;
}

/* What this file has of a column, past what is already in *mask. False if a save has replaced it, so look again. */
static bool save_world_overlay_file(save_world_t *save, const save_world_region_t *region, uint32_t local,
                                    column_mask_t *mask, uint16_t *states, uint8_t *levels, column_mask_t *levels_mask,
                                    uint8_t *scratch) {
    char path[FILE_PATH_BYTES];
    file_t file = {.fd = -1, ._pad0 = {0}};
    uint64_t damaged = 0u;
    if (!save_world_delta_path(save, path, "r", region->region_x, region->region_y, region->generation, "")) {
        damaged = 1u;
    } else {
        const file_status_t status = file_open(&file, path, FILE_MODE_READ);
        if (status == FILE_ERROR_NOT_FOUND) {
            return false;
        }
        if (status != FILE_OK) {
            LOG_ERROR("save: could not read %s: %s", path, file_status_name(status));
            damaged = 1u;
        }
    }
    if (damaged == 0u) {
        save_world_delta_header_t header;
        save_world_column_entry_t column = {.section_mask = COLUMN_MASK_NONE, .entries_offset = 0u, .checksum = 0u};
        save_world_section_entry_t entries[COLUMN_SECTIONS];
        file_status_t entries_status = FILE_OK;
        const bool good = file_read_at(&file, 0u, &header, sizeof(header)) == FILE_OK &&
                          save_world_delta_header_good(save, &header, region) &&
                          file_read_at(&file, sizeof(header) + (uint64_t)local * sizeof(column), &column,
                                       sizeof(column)) == FILE_OK &&
                          (!column_mask_any(column.section_mask) ||
                           save_world_entries_read(&file, region->file_bytes, &column, entries, &entries_status));
        if (!good) {
            LOG_ERROR("save: column %" PRIu32 " of %s is past reading, so what generates there is used", local, path);
            damaged = 1u;
        } else if (column_mask_any(column.section_mask)) {
            damaged = save_world_overlay_records(&file, region, column.section_mask, entries, mask, states, levels,
                                                 levels_mask, scratch);
        }
    }
    /* It was only read. */
    (void)file_close(&file);
    if (damaged != 0u) {
        atomic_fetch_add_explicit(&save->sections_damaged, damaged, memory_order_relaxed);
    }
    return true;
}

static column_mask_t save_world_overlay_read(save_world_t *save, int32_t chunk_x, int32_t chunk_y, uint16_t *states,
                                             uint8_t *levels, column_mask_t *out_levels_mask, uint8_t *scratch) {
    column_mask_t levels_mask = COLUMN_MASK_NONE;
    if (out_levels_mask != nullptr) {
        *out_levels_mask = COLUMN_MASK_NONE;
    }
    if (save->open == 0u) {
        return COLUMN_MASK_NONE;
    }
    const int32_t region_x = chunk_x >> SAVE_WORLD_REGION_SHIFT;
    const int32_t region_y = chunk_y >> SAVE_WORLD_REGION_SHIFT;
    const uint32_t local = (((uint32_t)chunk_y & (SAVE_WORLD_REGION_SIDE - 1u)) << SAVE_WORLD_REGION_SHIFT) |
                           ((uint32_t)chunk_x & (SAVE_WORLD_REGION_SIDE - 1u));
    column_mask_t mask = COLUMN_MASK_NONE;
    /* What is on its way first, the newest of it first, and only then the file: a section moves that way and no other, and a column not in the
     * map gets no newer. So a section in neither set when they were looked at is in whichever file is current by the time that is looked at. */
    (void)pthread_mutex_lock(&save->mutex);
    for (uint32_t which = 0u; which < 2u; which++) {
        const save_world_pending_t *const pending =
            &save->pending[which == 0u ? save->active : (save->active == 0u ? 1u : 0u)];
        for (uint32_t section = 0u; section < COLUMN_SECTIONS && pending->count != 0u; section++) {
            if (column_mask_has(mask, section)) {
                continue;
            }
            const save_world_pending_ref_t *const ref = save_world_pending_find(pending, chunk_x, chunk_y, section);
            if (ref != nullptr) {
                /* It came from a section in memory, and so it is good. */
                const bool good = save_world_record_decode(pending->borrowed_bytes + ref->offset, ref->bytes,
                                                           states + (uint64_t)section * SECTION_VOXELS);
                ASSERT_ALWAYS(good);
                mask = column_mask_or(mask, column_mask_of(section));
                save_world_overlay_levels_take(pending->borrowed_bytes + ref->offset, ref->bytes, section, levels,
                                               &levels_mask);
            }
        }
    }
    (void)pthread_mutex_unlock(&save->mutex);
    for (uint32_t tries = 0u;; tries++) {
        save_world_region_t region;
        bool found = false;
        (void)pthread_mutex_lock(&save->mutex);
        const save_world_region_t *const entry =
            save_world_region_find(save->borrowed_manifest[save->current], save->region_count, region_x, region_y);
        if (entry != nullptr) {
            region = *entry;
            found = true;
        }
        (void)pthread_mutex_unlock(&save->mutex);
        if (!found || save_world_overlay_file(save, &region, local, &mask, states, levels, &levels_mask, scratch)) {
            break;
        }
        if (tries + 1u == SAVE_WORLD_READ_TRIES) {
            LOG_ERROR("save: the file of region %" PRId32 ", %" PRId32 " keeps going missing", region_x, region_y);
            atomic_fetch_add_explicit(&save->sections_damaged, 1u, memory_order_relaxed);
            break;
        }
    }
    if (out_levels_mask != nullptr) {
        *out_levels_mask = levels_mask;
    }
    return mask;
}

column_mask_t save_world_column_overlay_levels(save_world_t *save, int32_t chunk_x, int32_t chunk_y, uint16_t *states,
                                               uint8_t *levels, column_mask_t *out_levels_mask, uint8_t *scratch) {
    const column_mask_t mask =
        save_world_overlay_read(save, chunk_x, chunk_y, states, levels, out_levels_mask, scratch);
    if (column_mask_any(mask)) {
        atomic_fetch_add_explicit(&save->sections_loaded, (uint64_t)column_mask_count(mask), memory_order_relaxed);
    }
    return mask;
}

column_mask_t save_world_column_overlay(save_world_t *save, int32_t chunk_x, int32_t chunk_y, uint16_t *states,
                                        uint8_t *scratch) {
    return save_world_column_overlay_levels(save, chunk_x, chunk_y, states, nullptr, nullptr, scratch);
}

/* ---- For the far field ----------------------------------------------------------------------------------------------- */

/* What save_world_far_line keeps at the start of its scratch. */
typedef struct save_world_far_cache_t {
    column_mask_t mask; /* 0: the sections it holds */
    int32_t chunk_x;    /* 16 */
    int32_t chunk_y;    /* 20 */
    uint8_t filled;     /* 24: zero bytes say nothing has been read yet */
    uint8_t _pad0[39];  /* 25 */
} save_world_far_cache_t;
static_assert(sizeof(save_world_far_cache_t) == 64);

void save_world_far_line(void *borrowed_save, int32_t x, int32_t y, uint16_t *out, uint8_t *scratch) {
    save_world_far_cache_t *const cache = (save_world_far_cache_t *)(void *)scratch;
    uint16_t *const states = (uint16_t *)(void *)(scratch + sizeof(*cache));
    const int32_t chunk_x = column_chunk_of(x), chunk_y = column_chunk_of(y);
    if (cache->filled == 0u || cache->chunk_x != chunk_x || cache->chunk_y != chunk_y) {
        cache->mask = save_world_overlay_read(borrowed_save, chunk_x, chunk_y, states, nullptr, nullptr,
                                              scratch + sizeof(*cache) +
                                                  (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS * sizeof(uint16_t));
        cache->chunk_x = chunk_x;
        cache->chunk_y = chunk_y;
        cache->filled = 1u;
    }
    const uint32_t local_x = column_local_of(x), local_y = column_local_of(y);
    for (column_mask_t left = cache->mask; column_mask_any(left); left = column_mask_drop_lowest(left)) {
        const uint32_t section = column_mask_lowest(left);
        for (uint32_t z = 0u; z < SECTION_SIZE; z++) {
            out[section * SECTION_SIZE + z] =
                states[(uint64_t)section * SECTION_VOXELS + section_voxel_index(local_x, local_y, z)];
        }
    }
}
