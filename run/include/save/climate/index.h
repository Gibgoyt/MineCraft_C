/* See run/src/save/climate/README.md. */
#ifndef MC_RUN_SAVE_CLIMATE_INDEX_H
#define MC_RUN_SAVE_CLIMATE_INDEX_H

#include "climate/weather/index.h"
#include "file/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* Plan 004 phase 07: the weekly state in its own file, <world>/climate/week.<W>, the store after census week W. */

enum : uint32_t {
    /* Raised by any change an older reader could not read. A file of another version is refused, and the census starts again
     * from spin-up, and says so. */
    SAVE_CLIMATE_VERSION = 1u,
    SAVE_CLIMATE_KEEP = 2u, /* week files kept: the newest and the one before, until the state file has caught up */
};

typedef struct save_climate_header_t {
    uint8_t magic[8];      /* 0: "MCCLIMWK" */
    uint64_t seed;         /* 8 */
    uint64_t week;         /* 16: W */
    uint64_t tiles_bytes;  /* 24: tile_count * sizeof(climate_week_tile_t) */
    uint64_t tiles_hash;   /* 32: FNV-1a of those bytes */
    uint32_t version;      /* 40: SAVE_CLIMATE_VERSION */
    uint32_t header_bytes; /* 44 */
    uint32_t tile_count;   /* 48 */
    uint8_t _pad0[4];      /* 52 */
    uint64_t checksum;     /* 56: FNV-1a of the 56 bytes before this */
} save_climate_header_t;
static_assert(sizeof(save_climate_header_t) == 64);
static_assert(alignof(save_climate_header_t) == 8);
static_assert(offsetof(save_climate_header_t, magic) == 0);
static_assert(offsetof(save_climate_header_t, seed) == 8);
static_assert(offsetof(save_climate_header_t, week) == 16);
static_assert(offsetof(save_climate_header_t, tiles_bytes) == 24);
static_assert(offsetof(save_climate_header_t, tiles_hash) == 32);
static_assert(offsetof(save_climate_header_t, version) == 40);
static_assert(offsetof(save_climate_header_t, header_bytes) == 44);
static_assert(offsetof(save_climate_header_t, tile_count) == 48);
static_assert(offsetof(save_climate_header_t, _pad0) == 52);
static_assert(offsetof(save_climate_header_t, checksum) == 56);

/* What opening came to. */
typedef enum save_climate_status_t : uint8_t {
    SAVE_CLIMATE_LOADED = 0,  /* a file was read whole and believed */
    SAVE_CLIMATE_NONE = 1,    /* no week file at or before the week asked for: a world from before, or a new one */
    SAVE_CLIMATE_REFUSED = 2, /* the newest such file failed a check: left alone on disk; the store is empty */
} save_climate_status_t;

/* "<world_directory>/climate/week.<week>" into out (FILE_PATH_BYTES). False if it does not fit. */
bool save_climate_path(const char *world_directory, uint64_t week, char *out);

/* The store, every tile in (tile_y, tile_x) order with its padding zero, as week.<store->week>, replacing it whole or not at
 * all (written as week.<W>.tmp, synced, renamed, the directory synced), then every week file but the newest
 * SAVE_CLIMATE_KEEP removed. sorted and scratch hold store->tile_count each. Nothing may add to the store meanwhile. */
[[nodiscard]] file_status_t save_climate_write(const char *world_directory, uint64_t seed,
                                               const climate_week_store_t *store, uint32_t *sorted, uint32_t *scratch);

/* Empties the store, then reads the newest week.<W> with W <= newest_week into it, if there is one, and checks everything:
 * magic, version, seed, the header's checksum, sizes, the tiles' hash, tiles in strict (tile_y, tile_x) order, flags, every
 * number finite, soil in 0 to 150, snow, ice, runoff and flood at least 0, the day kinds, every padding byte zero, each tile
 * at week W. A file that fails is refused and left alone, and the store emptied. On success the store is at week W, every
 * tile not fresh (its side data comes back from the seed at the next census), and every other name that is exactly
 * week.<digits> or that with .tmp is removed, and no other name. *out_week is W, or 0 with none. Logs what it did. */
save_climate_status_t save_climate_open(const char *world_directory, uint64_t seed, uint64_t newest_week,
                                        climate_week_store_t *store, uint64_t *out_week);

/* FNV-1a 64, as the file's hashes. */
uint64_t save_climate_hash(uint64_t hash, const void *data, uint64_t bytes);

#endif
