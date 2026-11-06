/* See README.md. The weekly state's own file: written whole or not at all, read back only if every check holds. */
#include "save/climate/index.h"

#include "log/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint8_t SAVE_CLIMATE_MAGIC[8] = {'M', 'C', 'C', 'L', 'I', 'M', 'W', 'K'};
static const uint64_t SAVE_CLIMATE_HASH_START = 0xCBF29CE484222325ull;
static const uint64_t SAVE_CLIMATE_HASH_PRIME = 0x100000001B3ull;
static const char SAVE_CLIMATE_DIRECTORY[] = "climate";
static const char SAVE_CLIMATE_PREFIX[] = "week.";
static const char SAVE_CLIMATE_TMP[] = ".tmp";

uint64_t save_climate_hash(uint64_t hash, const void *data, uint64_t bytes) {
    const uint8_t *const cursor = data;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ cursor[i]) * SAVE_CLIMATE_HASH_PRIME;
    }
    return hash;
}

static bool save_climate_directory(const char *world_directory, char *out) {
    const int32_t n = snprintf(out, FILE_PATH_BYTES, "%s/%s", world_directory, SAVE_CLIMATE_DIRECTORY);
    return n > 0 && n < FILE_PATH_BYTES;
}

bool save_climate_path(const char *world_directory, uint64_t week, char *out) {
    const int32_t n = snprintf(out, FILE_PATH_BYTES, "%s/%s/%s%" PRIu64, world_directory, SAVE_CLIMATE_DIRECTORY,
                               SAVE_CLIMATE_PREFIX, week);
    return n > 0 && n + (int32_t)sizeof(SAVE_CLIMATE_TMP) < FILE_PATH_BYTES;
}

/* A week file's number from its name: exactly "week." and digits, with ".tmp" after if tmp is not nullptr. */
static bool save_climate_name_week(const char *name, uint64_t *out_week, bool *out_tmp) {
    const uint64_t prefix = sizeof(SAVE_CLIMATE_PREFIX) - 1u;
    if (strncmp(name, SAVE_CLIMATE_PREFIX, prefix) != 0) {
        return false;
    }
    const char *cursor = name + prefix;
    uint64_t week = 0u, digits = 0u;
    while (*cursor >= '0' && *cursor <= '9') {
        if (week > (UINT64_MAX - 9u) / 10u) {
            return false;
        }
        week = week * 10u + (uint64_t)(*cursor - '0');
        cursor++;
        digits++;
    }
    if (digits == 0u || digits > 19u) {
        return false;
    }
    *out_tmp = strcmp(cursor, SAVE_CLIMATE_TMP) == 0;
    if (*cursor != '\0' && !*out_tmp) {
        return false;
    }
    *out_week = week;
    return true;
}

/* Removes every week file and leftover .tmp but those of the weeks kept (keep_count of them). */
[[gnu::noinline]] static void save_climate_prune(const char *directory, const uint64_t *keep, uint32_t keep_count) {
    DIR *const handle = opendir(directory);
    if (handle == nullptr) {
        return;
    }
    char path[FILE_PATH_BYTES];
    bool removed = false;
    for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
        uint64_t week = 0u;
        bool tmp = false;
        if (!save_climate_name_week(entry->d_name, &week, &tmp)) {
            continue; /* not one of ours: never touched */
        }
        bool kept = false;
        for (uint32_t i = 0u; i < keep_count && !tmp; i++) {
            kept = kept || keep[i] == week;
        }
        const int32_t n = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (!kept && n > 0 && n < (int32_t)sizeof(path)) {
            removed = file_remove(path) == FILE_OK || removed;
        }
    }
    closedir(handle);
    if (removed) {
        (void)file_sync_dir(directory);
    }
}

/* The newest weeks on disk, at most count of them, newest first; with newest_most, none past it. */
[[gnu::noinline]] static uint32_t save_climate_weeks(const char *directory, uint64_t newest_most, uint64_t *out,
                                                     uint32_t count) {
    DIR *const handle = opendir(directory);
    if (handle == nullptr) {
        return 0u;
    }
    uint32_t found = 0u;
    for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
        uint64_t week = 0u;
        bool tmp = false;
        if (!save_climate_name_week(entry->d_name, &week, &tmp) || tmp || week > newest_most) {
            continue;
        }
        /* Into the newest-first list, if it belongs. */
        uint32_t at = found;
        while (at > 0u && out[at - 1u] < week) {
            at--;
        }
        if (at >= count || (at > 0u && out[at - 1u] == week)) {
            continue;
        }
        const uint32_t last = found < count ? found : count - 1u;
        for (uint32_t i = last; i > at; i--) {
            out[i] = out[i - 1u];
        }
        out[at] = week;
        found = found < count ? found + 1u : count;
    }
    closedir(handle);
    return found;
}

/* Each of the steps below holds a path or two of FILE_PATH_BYTES: kept out of one another's frames, so none passes the
 * frame limit once the link inlines what it can. */
[[gnu::noinline]] static file_status_t save_climate_directory_make(const char *world_directory) {
    char directory[FILE_PATH_BYTES];
    if (!save_climate_directory(world_directory, directory)) {
        return FILE_ERROR_PATH_TOO_LONG;
    }
    return file_make_dirs(directory);
}

[[gnu::noinline]] static void save_climate_directory_tidy(const char *world_directory) {
    char directory[FILE_PATH_BYTES];
    if (!save_climate_directory(world_directory, directory)) {
        return;
    }
    (void)file_sync_dir(directory);
    uint64_t keep[SAVE_CLIMATE_KEEP];
    const uint32_t kept = save_climate_weeks(directory, UINT64_MAX, keep, SAVE_CLIMATE_KEEP);
    save_climate_prune(directory, keep, kept);
}

[[gnu::noinline]] static file_status_t save_climate_stream(const char *tmp, const char *path,
                                                           const save_climate_header_t *header,
                                                           const climate_week_store_t *store, const uint32_t *sorted) {
    file_t file;
    file_status_t status = file_open(&file, tmp, FILE_MODE_WRITE);
    if (status == FILE_OK) {
        status = file_write_all(&file, header, sizeof(*header));
    }
    for (uint32_t i = 0u; i < store->tile_count && status == FILE_OK; i++) {
        status = file_write_all(&file, &store->borrowed_tiles[sorted[i]], sizeof(climate_week_tile_t));
    }
    if (status == FILE_OK) {
        status = file_sync(&file);
    }
    const file_status_t closed = file_close(&file);
    status = status == FILE_OK ? closed : status;
    if (status == FILE_OK) {
        status = file_rename(tmp, path);
    }
    if (status != FILE_OK) {
        (void)file_remove(tmp);
    }
    return status;
}

file_status_t save_climate_write(const char *world_directory, uint64_t seed, const climate_week_store_t *store,
                                 uint32_t *sorted, uint32_t *scratch) {
    char path[FILE_PATH_BYTES], tmp[FILE_PATH_BYTES];
    if (!save_climate_path(world_directory, store->week, path) ||
        snprintf(tmp, sizeof(tmp), "%.4000s%s", path, SAVE_CLIMATE_TMP) >= (int32_t)sizeof(tmp)) {
        return FILE_ERROR_PATH_TOO_LONG;
    }
    file_status_t status = save_climate_directory_make(world_directory);
    if (status != FILE_OK) {
        return status;
    }
    climate_week_store_sorted(store, sorted, scratch);
    save_climate_header_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, SAVE_CLIMATE_MAGIC, sizeof(header.magic));
    header.seed = seed;
    header.week = store->week;
    header.tile_count = store->tile_count;
    header.tiles_bytes = (uint64_t)store->tile_count * sizeof(climate_week_tile_t);
    header.version = SAVE_CLIMATE_VERSION;
    header.header_bytes = (uint32_t)sizeof(header);
    uint64_t hash = SAVE_CLIMATE_HASH_START;
    for (uint32_t i = 0u; i < store->tile_count; i++) {
        hash = save_climate_hash(hash, &store->borrowed_tiles[sorted[i]], sizeof(climate_week_tile_t));
    }
    header.tiles_hash = hash;
    header.checksum = save_climate_hash(SAVE_CLIMATE_HASH_START, &header, offsetof(save_climate_header_t, checksum));
    status = save_climate_stream(tmp, path, &header, store, sorted);
    if (status != FILE_OK) {
        return status;
    }
    save_climate_directory_tidy(world_directory);
    return FILE_OK;
}

static bool save_climate_zero(const uint8_t *bytes, uint64_t count) {
    for (uint64_t i = 0u; i < count; i++) {
        if (bytes[i] != 0u) {
            return false;
        }
    }
    return true;
}

static bool save_climate_cell_ok(const climate_week_t *cell) {
    const float32_t numbers[11] = {cell->t_mean,  cell->t_min,   cell->t_max,     cell->rain_mm,
                                   cell->snow_mm, cell->soil_mm, cell->runoff_mm, cell->wind[0],
                                   cell->wind[1], cell->ice_cm,  cell->flood};
    for (uint32_t i = 0u; i < 11u; i++) {
        if (!isfinite(numbers[i])) {
            return false;
        }
    }
    if (cell->soil_mm < 0.0f32 || cell->soil_mm > CLIMATE_SOIL_MM_MAX || cell->snow_mm < 0.0f32 ||
        cell->ice_cm < 0.0f32 || cell->runoff_mm < 0.0f32 || cell->flood < 0.0f32 || cell->rain_mm < 0.0f32) {
        return false;
    }
    for (uint32_t i = 0u; i < CLIMATE_WEEK_DAYS; i++) {
        if (cell->day_kind[i] >= CLIMATE_DAY_KIND_COUNT) {
            return false;
        }
    }
    return save_climate_zero(cell->_pad0, sizeof(cell->_pad0));
}

/* Reads and checks one file into the store, which is empty. False, having said why, if anything fails. */
[[gnu::noinline]] static bool save_climate_read(const char *path, uint64_t seed, uint64_t week,
                                                climate_week_store_t *store) {
    file_t file;
    if (file_open(&file, path, FILE_MODE_READ) != FILE_OK) {
        LOG_WARN("save: the weekly state %s cannot be opened", path);
        return false;
    }
    save_climate_header_t header;
    uint64_t size = 0u;
    bool ok = file_size(&file, &size) == FILE_OK && file_read_exact(&file, &header, sizeof(header)) == FILE_OK;
    const char *why = ok ? nullptr : "it is shorter than its header";
    if (ok && (memcmp(header.magic, SAVE_CLIMATE_MAGIC, sizeof(header.magic)) != 0 ||
               header.checksum !=
                   save_climate_hash(SAVE_CLIMATE_HASH_START, &header, offsetof(save_climate_header_t, checksum)))) {
        ok = false;
        why = "its header is not whole";
    }
    if (ok && header.version != SAVE_CLIMATE_VERSION) {
        ok = false;
        why = "it is of another version";
    }
    if (ok && (header.seed != seed || header.week != week)) {
        ok = false;
        why = "it is of another world or week";
    }
    if (ok && (header.header_bytes != sizeof(header) || header.tile_count > CLIMATE_WEEK_TILES_MAX ||
               !save_climate_zero(header._pad0, sizeof(header._pad0)) ||
               header.tiles_bytes != (uint64_t)header.tile_count * sizeof(climate_week_tile_t) ||
               size != sizeof(header) + header.tiles_bytes)) {
        ok = false;
        why = "its sizes do not agree";
    }
    uint64_t hash = SAVE_CLIMATE_HASH_START;
    int32_t last_x = 0, last_y = 0;
    for (uint32_t i = 0u; ok && i < header.tile_count; i++) {
        /* Read into the slot the tile goes in: a tile is more than a thread's frame may hold. */
        climate_week_tile_t *const slot = &store->borrowed_tiles[store->tile_count];
        if (file_read_exact(&file, slot, sizeof(*slot)) != FILE_OK) {
            ok = false;
            why = "it ends early";
            break;
        }
        hash = save_climate_hash(hash, slot, sizeof(*slot));
        const bool ordered = i == 0u || slot->tile_y > last_y || (slot->tile_y == last_y && slot->tile_x > last_x);
        if (!ordered || slot->week != (uint32_t)week || slot->flags == 0u ||
            (slot->flags & ~(uint32_t)CLIMATE_WEEK_TILE_FLAGS) != 0u ||
            !save_climate_zero(slot->_pad0, sizeof(slot->_pad0))) {
            ok = false;
            why = "a tile is out of order or not whole";
            break;
        }
        for (uint32_t c = 0u; c < CLIMATE_TILE_CELL_COUNT && ok; c++) {
            ok = save_climate_cell_ok(&slot->cells[c]);
        }
        if (!ok) {
            why = "a number is out of its range";
            break;
        }
        last_x = slot->tile_x;
        last_y = slot->tile_y;
        if (!climate_week_store_adopt(store)) {
            ok = false;
            why = "the store would not take it";
            break;
        }
    }
    if (ok && hash != header.tiles_hash) {
        ok = false;
        why = "its tiles' hash does not match";
    }
    (void)file_close(&file);
    if (!ok) {
        LOG_WARN("save: the weekly state %s is refused, as %s; it is left as it is, and the census starts from spin-up",
                 path, why);
    }
    return ok;
}

save_climate_status_t save_climate_open(const char *world_directory, uint64_t seed, uint64_t newest_week,
                                        climate_week_store_t *store, uint64_t *out_week) {
    climate_week_store_empty(store);
    *out_week = 0u;
    char directory[FILE_PATH_BYTES], path[FILE_PATH_BYTES];
    uint64_t newest = 0u;
    if (!save_climate_directory(world_directory, directory) ||
        save_climate_weeks(directory, newest_week, &newest, 1u) == 0u ||
        !save_climate_path(world_directory, newest, path)) {
        LOG_INFO("save: no weekly state at or before week %" PRIu64 ": every tile starts from spin-up", newest_week);
        return SAVE_CLIMATE_NONE;
    }
    if (!save_climate_read(path, seed, newest, store)) {
        climate_week_store_empty(store);
        return SAVE_CLIMATE_REFUSED;
    }
    store->week = newest;
    *out_week = newest;
    const uint64_t keep[1] = {newest};
    save_climate_prune(directory, keep, 1u);
    LOG_INFO("save: the weekly state of week %" PRIu64 " is back, %" PRIu32 " tiles", newest, store->tile_count);
    return SAVE_CLIMATE_LOADED;
}
