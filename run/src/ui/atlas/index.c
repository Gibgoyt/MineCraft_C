/* See README.md. The Atlas's part of ui: the store of what is known of the ground, its lock and its files, the view and its
 * keys, the drawing and saving jobs, and each frame's fixed parts. Its memory is the region ui_t owns, given at
 * ui_atlas_store_start; nothing here touches the heap. */
#include "ui/atlas/index.h"

#include "assert/index.h"
#include "climate/layer/index.h"
#include "climate/weather/index.h"
#include "file/index.h"
#include "log/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

enum : uint64_t {
    ATLAS_MS = 1000000u, /* ns */
};

/* ---- The store ------------------------------------------------------------------------------------------------------ */

/* A ticket lock: each taker waits its turn, yielding the CPU, so nobody waits behind a stream of others. Acquire on taking
 * pairs with the release on giving back: everything the last holder wrote is the next one's to see. */
static void atlas_lock(ui_atlas_store_t *store) {
    const uint32_t mine = atomic_fetch_add_explicit(&store->ticket_next, 1u, memory_order_relaxed);
    while (atomic_load_explicit(&store->ticket_serving, memory_order_acquire) != mine) {
        (void)sched_yield();
    }
}
static void atlas_unlock(ui_atlas_store_t *store) {
    atomic_fetch_add_explicit(&store->ticket_serving, 1u, memory_order_release);
}

static void atlas_fill_none(uint32_t *pixels) {
    for (uint32_t k = 0u; k < UI_ATLAS_TILE_PIXELS; k++) {
        pixels[k] = UI_ATLAS_PIXEL_NONE;
    }
}

/* Under the lock: the slot holding a tile, or -1. */
static int32_t atlas_find(const ui_atlas_store_t *store, uint32_t level, int32_t tile_x, int32_t tile_y) {
    for (uint32_t i = 0u; i < UI_ATLAS_SLOTS; i++) {
        const ui_atlas_slot_t *const slot = &store->borrowed_slots[i];
        if (slot->state == UI_ATLAS_SLOT_LIVE && slot->level == level && slot->tile_x == tile_x &&
            slot->tile_y == tile_y) {
            return (int32_t)i;
        }
    }
    return -1;
}

static void atlas_touch(ui_atlas_store_t *store, ui_atlas_slot_t *slot) {
    slot->last_used = ++store->use_clock;
}

/* Under the lock: a slot for a tile not in memory, filled with not generated: an empty one, or else the least recently used
 * that is not dirty. -1 if every slot is dirty. */
static int32_t atlas_take(ui_atlas_store_t *store, uint32_t level, int32_t tile_x, int32_t tile_y) {
    int32_t best = -1;
    for (uint32_t i = 0u; i < UI_ATLAS_SLOTS; i++) {
        const ui_atlas_slot_t *const slot = &store->borrowed_slots[i];
        if (slot->state == UI_ATLAS_SLOT_EMPTY) {
            best = (int32_t)i;
            break;
        }
        if (slot->dirty == 0u && (best < 0 || slot->last_used < store->borrowed_slots[best].last_used)) {
            best = (int32_t)i;
        }
    }
    if (best < 0) {
        return -1;
    }
    ui_atlas_slot_t *const slot = &store->borrowed_slots[best];
    atlas_fill_none(slot->borrowed_pixels);
    slot->tile_x = tile_x;
    slot->tile_y = tile_y;
    slot->level = (uint8_t)level;
    slot->state = UI_ATLAS_SLOT_LIVE;
    slot->dirty = 0u;
    slot->merge = store->borrowed_directory != nullptr ? 1u : 0u;
    slot->parents_marked = 0u;
    slot->revision = store->revision;
    atlas_touch(store, slot);
    return best;
}

/* Under the lock: a mip into the stale list once. */
static void atlas_stale_push(ui_atlas_store_t *store, uint32_t level, int32_t tile_x, int32_t tile_y) {
    for (uint32_t k = 0u; k < store->stale_count; k++) {
        const ui_atlas_tile_key_t *const key = &store->borrowed_stale[k];
        if (key->level == level && key->tile_x == tile_x && key->tile_y == tile_y) {
            return;
        }
    }
    if (store->stale_count == UI_ATLAS_STALE_MAX) {
        store->stale_overflow = 1u;
        return;
    }
    store->borrowed_stale[store->stale_count++] = (ui_atlas_tile_key_t){tile_x, tile_y, (uint8_t)level, {0}};
}

/* Under the lock: a level-0 tile's seven ancestors into the stale list. */
static void atlas_stale_ancestors(ui_atlas_store_t *store, int32_t tile_x, int32_t tile_y) {
    for (uint32_t level = 1u; level < UI_ATLAS_LEVELS; level++) {
        atlas_stale_push(store, level, (int32_t)floor((float64_t)tile_x / (float64_t)(1u << level)),
                         (int32_t)floor((float64_t)tile_y / (float64_t)(1u << level)));
    }
}

static void atlas_path(const ui_atlas_store_t *store, uint32_t level, int32_t tile_x, int32_t tile_y, char *out) {
    char name[UI_ATLAS_FILE_NAME_BYTES];
    ui_atlas_tile_file_name(level, tile_x, tile_y, name);
    (void)snprintf(out, FILE_PATH_BYTES, "%s/%s", store->borrowed_directory, name);
}

/* Not under the lock: a tile's file into out. False if there is none, or it was refused (counted in *refused). */
static bool atlas_file_read(const ui_atlas_store_t *store, uint32_t level, int32_t tile_x, int32_t tile_y,
                            uint8_t *bytes, uint32_t *out, bool *refused) {
    *refused = false;
    if (store->borrowed_directory == nullptr) {
        return false;
    }
    char path[FILE_PATH_BYTES];
    atlas_path(store, level, tile_x, tile_y, path);
    uint64_t length = 0u;
    if (file_read_whole(path, bytes, UI_ATLAS_FILE_BYTES_MAX, &length) != FILE_OK) {
        return false; /* none yet, which is most tiles; anything else and the tile is as if never saved */
    }
    const ui_atlas_decode_t status = ui_atlas_tile_decode(bytes, length, store->seed, level, tile_x, tile_y, out);
    if (status != UI_ATLAS_DECODE_OK) {
        *refused = true;
        if (store->refused_said == 0u) {
            LOG_WARN("atlas: %s is ignored, as %s; it is left where it is", path, ui_atlas_decode_name(status));
        }
        return false;
    }
    return true;
}

/* Under the lock: a refused file counted, and said once. */
static void atlas_refused_note(ui_atlas_store_t *store, bool refused) {
    if (refused) {
        store->files_refused++;
        store->refused_said = 1u;
    }
}

static void atlas_merge(uint32_t *slot, const uint32_t *file) {
    for (uint32_t k = 0u; k < UI_ATLAS_TILE_PIXELS; k++) {
        slot[k] = ui_atlas_pixel_generated(slot[k]) ? slot[k] : file[k];
    }
}

/* What atlas_ensure found. */
typedef enum atlas_have_t : uint8_t {
    ATLAS_HAVE_SLOT = 0,    /* in a slot, whose index is returned; the lock is held */
    ATLAS_HAVE_FILE = 1,    /* no slot to be had, but its file is in the file buffer; the lock is held */
    ATLAS_HAVE_NOTHING = 2, /* neither; the lock is held */
} atlas_have_t;

/* A tile in a slot, read from its file if it is not in memory, merged with its file if made before the file was read.
 * Returns with the lock held. bytes (UI_ATLAS_FILE_BYTES_MAX) and file (a tile) are the caller's, used outside the lock. */
static atlas_have_t atlas_ensure(ui_atlas_store_t *store, uint32_t level, int32_t tile_x, int32_t tile_y,
                                 uint8_t *bytes, uint32_t *file, int32_t *out_slot) {
    atlas_lock(store);
    int32_t index = atlas_find(store, level, tile_x, tile_y);
    if (index >= 0 && store->borrowed_slots[index].merge == 0u) {
        atlas_touch(store, &store->borrowed_slots[index]);
        *out_slot = index;
        return ATLAS_HAVE_SLOT;
    }
    atlas_unlock(store);
    bool refused = false;
    const bool have_file = atlas_file_read(store, level, tile_x, tile_y, bytes, file, &refused);
    atlas_lock(store);
    atlas_refused_note(store, refused);
    index = atlas_find(store, level, tile_x, tile_y);
    if (index < 0) {
        index = atlas_take(store, level, tile_x, tile_y);
        if (index < 0) {
            return have_file ? ATLAS_HAVE_FILE : ATLAS_HAVE_NOTHING;
        }
        if (have_file) {
            memcpy(store->borrowed_slots[index].borrowed_pixels, file, UI_ATLAS_TILE_BYTES);
        }
    } else if (store->borrowed_slots[index].merge != 0u && have_file) {
        atlas_merge(store->borrowed_slots[index].borrowed_pixels, file);
    }
    store->borrowed_slots[index].merge = 0u;
    atlas_touch(store, &store->borrowed_slots[index]);
    *out_slot = index;
    return ATLAS_HAVE_SLOT;
}

bool ui_atlas_store_start(ui_atlas_t *atlas, uint8_t *region, uint64_t bytes, const char *directory, uint64_t seed,
                          const uint16_t *colours, job_system_t *system, const climate_t *climate,
                          const ui_font_t *font) {
    if (bytes < UI_ATLAS_REGION_BYTES) {
        LOG_ERROR("atlas: its region is %" PRIu64 " bytes, not %" PRIu64, bytes, (uint64_t)UI_ATLAS_REGION_BYTES);
        return false;
    }
    memset(atlas, 0, sizeof(*atlas)); /* the lock too: ticket 0 is served first */
    job_group_init(&atlas->draw_group);
    job_group_init(&atlas->save_group);
    uint8_t *at = region;
    ui_atlas_store_t *const store = &atlas->store;
    uint32_t *const tiles = (uint32_t *)(void *)at;
    at += (uint64_t)UI_ATLAS_SLOTS * UI_ATLAS_TILE_BYTES;
    for (uint32_t k = 0u; k < 2u; k++) {
        atlas->borrowed_pictures[k] = (uint32_t *)(void *)at;
        at += UI_ATLAS_PICTURE_BYTES;
    }
    atlas->borrowed_field = (float32_t *)(void *)at;
    at += UI_ATLAS_PICTURE_BYTES;
    for (uint32_t k = 0u; k < 2u; k++) {
        atlas->borrowed_greys[k] = at;
        at += UI_ATLAS_PICTURE_BYTES / 4u;
    }
    atlas->borrowed_overlay_scratch = at;
    at += UI_ATLAS_OVERLAY_SCRATCH_BYTES;
    at = region + (((uint64_t)(at - region) + 63u) & ~63ull);
    arena_init(&atlas->arena_draw, "atlas_draw", at, UI_ATLAS_DRAW_ARENA_BYTES);
    at += UI_ATLAS_DRAW_ARENA_BYTES;
    arena_init(&atlas->arena_save, "atlas_save", at, UI_ATLAS_SAVE_ARENA_BYTES);
    at += UI_ATLAS_SAVE_ARENA_BYTES;
    arena_init(&atlas->arena_band, "atlas_band", at, UI_ATLAS_BAND_ARENA_BYTES);
    at += UI_ATLAS_BAND_ARENA_BYTES;
    store->borrowed_slots = (ui_atlas_slot_t *)(void *)at;
    at += (uint64_t)UI_ATLAS_SLOTS * sizeof(ui_atlas_slot_t);
    store->borrowed_stale = (ui_atlas_tile_key_t *)(void *)at;
    at += (uint64_t)UI_ATLAS_STALE_MAX * sizeof(ui_atlas_tile_key_t);
    for (uint32_t i = 0u; i < UI_ATLAS_SLOTS; i++) {
        store->borrowed_slots[i] = (ui_atlas_slot_t){.borrowed_pixels = tiles + (uint64_t)i * UI_ATLAS_TILE_PIXELS};
    }
    store->borrowed_colours = colours;
    store->seed = seed;
    if (directory != nullptr) {
        const uint64_t length = strlen(directory);
        if (length + 1u + UI_ATLAS_FILE_NAME_BYTES >= FILE_PATH_BYTES) {
            LOG_WARN("atlas: the world's path is too long, so the Atlas keeps nothing on disk");
        } else if (file_make_dirs(directory) != FILE_OK) {
            LOG_WARN("atlas: %s cannot be made, so the Atlas keeps nothing on disk", directory);
        } else {
            store->borrowed_directory = (char *)at;
            memcpy(store->borrowed_directory, directory, length + 1u);
        }
    }
    at += FILE_PATH_BYTES;
    ASSERT_ALWAYS((uint64_t)(at - region) <= bytes);
    atlas->borrowed_job = system;
    atlas->borrowed_climate = climate;
    atlas->borrowed_font = font;
    atlas->wanted.zoom = UI_ATLAS_ZOOM_DEFAULT;
    atlas->open = 1u;
    return true;
}

void ui_atlas_store_stop(ui_atlas_t *atlas) {
    if (atlas->open == 0u) {
        return;
    }
    ui_atlas_draw_wait(atlas);
    if (atlas->borrowed_job != nullptr && atlas->saving_busy != 0u) {
        job_group_wait(atlas->borrowed_job, &atlas->save_group);
    }
    atlas->saving_busy = 0u;
    arena_reset(&atlas->arena_save);
    ui_atlas_save_now(atlas, &atlas->arena_save);
    if (atlas->store.columns_lost != 0u) {
        LOG_WARN("atlas: %" PRIu64 " column writes were dropped with every tile waiting to be saved",
                 atlas->store.columns_lost);
    }
    arena_deinit(&atlas->arena_draw);
    arena_deinit(&atlas->arena_save);
    arena_deinit(&atlas->arena_band);
    job_group_deinit(&atlas->draw_group);
    job_group_deinit(&atlas->save_group);
    memset(atlas, 0, sizeof(*atlas));
}

void ui_atlas_column_write(ui_atlas_t *atlas, int32_t chunk_x, int32_t chunk_y, const uint32_t *pixels) {
    if (atlas == nullptr || atlas->open == 0u) {
        return;
    }
    ui_atlas_store_t *const store = &atlas->store;
    const int32_t tile_x = ui_atlas_tile_of((int64_t)chunk_x * UI_ATLAS_COLUMN_SIDE, 0u);
    const int32_t tile_y = ui_atlas_tile_of((int64_t)chunk_y * UI_ATLAS_COLUMN_SIDE, 0u);
    const int64_t asked_ns = time_now_ns();
    atlas_lock(store);
    const uint64_t waited = (uint64_t)(time_now_ns() - asked_ns);
    store->wait_ns_most = waited > store->wait_ns_most ? waited : store->wait_ns_most;
    int32_t index = atlas_find(store, 0u, tile_x, tile_y);
    index = index >= 0 ? index : atlas_take(store, 0u, tile_x, tile_y);
    if (index < 0) {
        store->columns_lost++;
        const bool say = store->lost_said == 0u;
        store->lost_said = 1u;
        atlas_unlock(store);
        if (say) {
            LOG_WARN("atlas: every tile in memory waits to be saved, so column %" PRId32 ", %" PRId32
                     " is not drawn until it loads again",
                     chunk_x, chunk_y);
        }
        return;
    }
    ui_atlas_slot_t *const slot = &store->borrowed_slots[index];
    ui_atlas_tile_column_put(slot->borrowed_pixels, chunk_x, chunk_y, pixels);
    slot->dirty = store->borrowed_directory != nullptr ? 1u : 0u; /* with no files, nothing waits: it may be evicted */
    slot->revision = ++store->revision;
    atlas_touch(store, slot);
    if (slot->parents_marked == 0u) {
        atlas_stale_ancestors(store, tile_x, tile_y);
        slot->parents_marked = 1u;
    }
    atlas_unlock(store);
}

/* One mip made again from its four children, which must be fresh, into its slot, dirty. */
static void atlas_mip_make(ui_atlas_t *atlas, uint32_t level, int32_t tile_x, int32_t tile_y, uint32_t *child,
                           uint32_t *parent, uint32_t *file, uint8_t *bytes) {
    ui_atlas_store_t *const store = &atlas->store;
    for (uint32_t quarter = 0u; quarter < 4u; quarter++) {
        const int32_t cx = 2 * tile_x + (int32_t)(quarter & 1u), cy = 2 * tile_y + (int32_t)(quarter >> 1u);
        int32_t index = -1;
        const atlas_have_t have = atlas_ensure(store, level - 1u, cx, cy, bytes, file, &index);
        const uint32_t *from = nullptr;
        if (have == ATLAS_HAVE_SLOT) {
            memcpy(child, store->borrowed_slots[index].borrowed_pixels, UI_ATLAS_TILE_BYTES);
            from = child;
        } else if (have == ATLAS_HAVE_FILE) {
            from = file;
        }
        atlas_unlock(store);
        ui_atlas_tile_halve(from, quarter, parent);
    }
    atlas_lock(store);
    int32_t index = atlas_find(store, level, tile_x, tile_y);
    index = index >= 0 ? index : atlas_take(store, level, tile_x, tile_y);
    if (index >= 0) {
        ui_atlas_slot_t *const slot = &store->borrowed_slots[index];
        memcpy(slot->borrowed_pixels, parent, UI_ATLAS_TILE_BYTES);
        slot->merge = 0u; /* the children made all of it */
        slot->dirty = store->borrowed_directory != nullptr ? 1u : 0u;
        slot->revision = store->revision;
        atlas_touch(store, slot);
    }
    atlas_unlock(store);
}

void ui_atlas_mips_make(ui_atlas_t *atlas, uint32_t level, arena_t *scratch) {
    if (atlas->open == 0u || level == 0u) {
        return;
    }
    ui_atlas_store_t *const store = &atlas->store;
    uint32_t *const child = arena_alloc(scratch, UI_ATLAS_TILE_BYTES, 64u);
    uint32_t *const parent = arena_alloc(scratch, UI_ATLAS_TILE_BYTES, 64u);
    uint32_t *const file = arena_alloc(scratch, UI_ATLAS_TILE_BYTES, 64u);
    uint8_t *const bytes = arena_alloc(scratch, UI_ATLAS_FILE_BYTES_MAX, 64u);
    atlas_lock(store);
    if (store->stale_overflow != 0u) {
        /* The list filled: every ancestor of every level-0 tile in memory is made again, as many as fit at a time. */
        store->stale_overflow = 0u;
        for (uint32_t i = 0u; i < UI_ATLAS_SLOTS; i++) {
            const ui_atlas_slot_t *const slot = &store->borrowed_slots[i];
            if (slot->state == UI_ATLAS_SLOT_LIVE && slot->level == 0u) {
                atlas_stale_ancestors(store, slot->tile_x, slot->tile_y);
            }
        }
    }
    atlas_unlock(store);
    for (uint32_t l = 1u; l <= level && l < UI_ATLAS_LEVELS; l++) {
        ui_atlas_tile_key_t keys[UI_ATLAS_STALE_MAX];
        uint32_t count = 0u;
        atlas_lock(store);
        uint32_t kept = 0u;
        for (uint32_t k = 0u; k < store->stale_count; k++) {
            const ui_atlas_tile_key_t key = store->borrowed_stale[k];
            if (key.level == l) {
                keys[count++] = key;
            } else {
                store->borrowed_stale[kept++] = key;
            }
        }
        store->stale_count = kept;
        if (l == 1u && count > 0u) {
            /* Their ancestors are on their way: the next write to a level-0 tile marks them again. */
            for (uint32_t i = 0u; i < UI_ATLAS_SLOTS; i++) {
                store->borrowed_slots[i].parents_marked = 0u;
            }
        }
        atlas_unlock(store);
        for (uint32_t k = 0u; k < count; k++) {
            atlas_mip_make(atlas, l, keys[k].tile_x, keys[k].tile_y, child, parent, file, bytes);
        }
    }
}

/* Every dirty tile written. A tile made before its file was read is merged with it first. */
static void atlas_write_dirty(ui_atlas_t *atlas, uint32_t *tile, uint32_t *file, uint8_t *encoded) {
    ui_atlas_store_t *const store = &atlas->store;
    if (store->borrowed_directory == nullptr) {
        return;
    }
    for (uint32_t i = 0u; i < UI_ATLAS_SLOTS; i++) {
        atlas_lock(store);
        ui_atlas_slot_t *const slot = &store->borrowed_slots[i];
        if (slot->state != UI_ATLAS_SLOT_LIVE || slot->dirty == 0u) {
            atlas_unlock(store);
            continue;
        }
        const uint32_t level = slot->level;
        const int32_t tile_x = slot->tile_x, tile_y = slot->tile_y;
        if (slot->merge != 0u) {
            atlas_unlock(store);
            bool refused = false;
            /* encoded is free until the encode below: the file's bytes go there. */
            const bool have = atlas_file_read(store, level, tile_x, tile_y, encoded, file, &refused);
            atlas_lock(store);
            atlas_refused_note(store, refused);
            /* A dirty slot is never taken for another tile, so it is still this one. */
            if (have) {
                atlas_merge(slot->borrowed_pixels, file);
            }
            slot->merge = 0u;
        }
        memcpy(tile, slot->borrowed_pixels, UI_ATLAS_TILE_BYTES);
        slot->dirty = 0u;
        atlas_unlock(store);
        const uint64_t length = ui_atlas_tile_encode(tile, store->seed, level, tile_x, tile_y, encoded);
        char path[FILE_PATH_BYTES];
        atlas_path(store, level, tile_x, tile_y, path);
        const file_status_t status = file_write_atomic(path, encoded, length);
        if (status != FILE_OK) {
            atlas_lock(store);
            if (slot->state == UI_ATLAS_SLOT_LIVE && slot->level == level && slot->tile_x == tile_x &&
                slot->tile_y == tile_y) {
                slot->dirty = 1u; /* tried again at the next save */
            }
            const bool say = store->save_failed_said == 0u;
            store->save_failed_said = 1u;
            atlas_unlock(store);
            if (say) {
                LOG_ERROR("atlas: %s could not be written: %s; it is tried again at the next save", path,
                          file_status_name(status));
            }
        }
    }
}

void ui_atlas_save_now(ui_atlas_t *atlas, arena_t *scratch) {
    if (atlas->open == 0u) {
        return;
    }
    uint32_t *const tile = arena_alloc(scratch, UI_ATLAS_TILE_BYTES, 64u);
    uint32_t *const file = arena_alloc(scratch, UI_ATLAS_TILE_BYTES, 64u);
    uint8_t *const encoded = arena_alloc(scratch, UI_ATLAS_FILE_BYTES_MAX, 64u);
    atlas_write_dirty(atlas, tile, file, encoded);
    ui_atlas_mips_make(atlas, UI_ATLAS_LEVELS - 1u, scratch);
    atlas_write_dirty(atlas, tile, file, encoded);
}

static void atlas_save_job(job_worker_t *worker, void *borrowed_argument) {
    ui_atlas_t *const atlas = borrowed_argument;
    (void)worker;
    arena_reset(&atlas->arena_save);
    ui_atlas_save_now(atlas, &atlas->arena_save);
    atomic_store_explicit(&atlas->save_done, 1u, memory_order_release);
}

void ui_atlas_save_step(ui_atlas_t *atlas, int64_t world_ns) {
    if (atlas == nullptr || atlas->open == 0u || atlas->store.borrowed_directory == nullptr ||
        atlas->borrowed_job == nullptr) {
        return;
    }
    if (atlas->saving_busy != 0u && atomic_load_explicit(&atlas->save_done, memory_order_acquire) != 0u) {
        atlas->saving_busy = 0u;
    }
    if (atlas->save_due_ns == 0) {
        atlas->save_due_ns = world_ns + (int64_t)UI_ATLAS_SAVE_INTERVAL_MS * (int64_t)ATLAS_MS;
        return;
    }
    if (world_ns < atlas->save_due_ns || atlas->saving_busy != 0u) {
        return;
    }
    atomic_store_explicit(&atlas->save_done, 0u, memory_order_relaxed);
    if (job_try_submit(atlas->borrowed_job, &atlas->save_group, atlas_save_job, atlas)) {
        atlas->saving_busy = 1u;
        atlas->save_due_ns = world_ns + (int64_t)UI_ATLAS_SAVE_INTERVAL_MS * (int64_t)ATLAS_MS;
    }
}

void ui_atlas_store_read(void *borrowed_context, uint32_t level, int64_t px, int64_t py, uint32_t width,
                         uint32_t height, uint32_t *out, arena_t *scratch) {
    ui_atlas_t *const atlas = borrowed_context;
    ui_atlas_store_t *const store = &atlas->store;
    uint32_t *const file = arena_alloc(scratch, UI_ATLAS_TILE_BYTES, 64u);
    uint8_t *const bytes = arena_alloc(scratch, UI_ATLAS_FILE_BYTES_MAX, 64u);
    const int32_t tx0 = ui_atlas_tile_x_of_column(px), tx1 = ui_atlas_tile_x_of_column(px + (int64_t)width - 1);
    /* Rows grow south, tiles' y north: the first row is in the northmost tile. */
    const int32_t ty_north = ui_atlas_tile_y_of_row(py), ty_south = ui_atlas_tile_y_of_row(py + (int64_t)height - 1);
    for (int32_t ty = ty_north; ty >= ty_south; ty--) {
        for (int32_t tx = tx0; tx <= tx1; tx++) {
            /* This tile's part of the rectangle, in the rectangle's own pixels. */
            const int64_t tile_column0 = (int64_t)tx * UI_ATLAS_TILE_SIDE;
            const int64_t tile_row0 = -((int64_t)ty + 1) * UI_ATLAS_TILE_SIDE;
            const int64_t c0 = px > tile_column0 ? px : tile_column0;
            const int64_t c1 = px + (int64_t)width < tile_column0 + UI_ATLAS_TILE_SIDE
                                   ? px + (int64_t)width
                                   : tile_column0 + UI_ATLAS_TILE_SIDE;
            const int64_t r0 = py > tile_row0 ? py : tile_row0;
            const int64_t r1 = py + (int64_t)height < tile_row0 + UI_ATLAS_TILE_SIDE ? py + (int64_t)height
                                                                                     : tile_row0 + UI_ATLAS_TILE_SIDE;
            int32_t index = -1;
            const atlas_have_t have = atlas_ensure(store, level, tx, ty, bytes, file, &index);
            const uint32_t *const from = have == ATLAS_HAVE_SLOT   ? store->borrowed_slots[index].borrowed_pixels
                                         : have == ATLAS_HAVE_FILE ? file
                                                                   : nullptr;
            for (int64_t r = r0; r < r1; r++) {
                uint32_t *const to = out + (uint64_t)(r - py) * width + (uint64_t)(c0 - px);
                if (from == nullptr) {
                    for (int64_t c = c0; c < c1; c++) {
                        to[c - c0] = UI_ATLAS_PIXEL_NONE;
                    }
                    continue;
                }
                memcpy(to, from + (uint64_t)(r - tile_row0) * UI_ATLAS_TILE_SIDE + (uint64_t)(c0 - tile_column0),
                       (uint64_t)(c1 - c0) * sizeof(uint32_t));
            }
            atlas_unlock(store);
        }
    }
}

/* ---- The drawing job -------------------------------------------------------------------------------------------- */

typedef struct atlas_band_t {
    ui_atlas_t *atlas; /* 0 */
    uint32_t band;     /* 8 */
    uint8_t _pad0[4];  /* 12 */
} atlas_band_t;
static_assert(sizeof(atlas_band_t) == 16);

/* What a band borrows, from an arena. */
static ui_atlas_scratch_t atlas_band_scratch(arena_t *arena, const ui_atlas_view_t *view, uint32_t bands) {
    const uint32_t rows = (view->height + bands - 1u) / bands;
    const uint64_t bytes = ui_atlas_band_scratch_bytes(view->width, rows);
    return (ui_atlas_scratch_t){arena_alloc(arena, bytes, 64u), bytes};
}

static void atlas_band_job(job_worker_t *worker, void *borrowed_argument) {
    const atlas_band_t *const band = borrowed_argument;
    ui_atlas_t *const atlas = band->atlas;
    ui_atlas_draw_band(&atlas->input, &atlas->target, band->band, UI_ATLAS_BANDS,
                       atlas_band_scratch(&worker->arena_job, &atlas->input.view, UI_ATLAS_BANDS));
    if (atomic_fetch_sub_explicit(&atlas->bands_left, 1u, memory_order_acq_rel) == 1u) {
        ui_atlas_draw_finish(&atlas->input, &atlas->target);
        /* Release gives main every pixel; after it this job touches nothing of the atlas. */
        atomic_store_explicit(&atlas->draw_done, 1u, memory_order_release);
    }
}

/* What every picture needs first: the weather systems for a now layer, and the mips the zoom reads made fresh. */
static void atlas_draw_prepare(ui_atlas_t *atlas, arena_t *scratch) {
    const ui_atlas_layer_t *const row = &ui_atlas_layers[atlas->input.view.layer];
    atlas->input.borrowed_systems = nullptr;
    const climate_t *const climate = atlas->borrowed_climate;
    if (row->time == UI_ATLAS_TIME_NOW && climate != nullptr && climate_is_open(climate)) {
        climate_systems_day_t *const systems =
            arena_alloc(&atlas->arena_draw, sizeof(climate_systems_day_t), alignof(climate_systems_day_t));
        if (ui_atlas_systems_build(climate, &atlas->arena_draw, &atlas->input.view, atlas->input.t, systems)) {
            atlas->input.borrowed_systems = systems;
        }
    }
    ui_atlas_mips_make(atlas, ui_atlas_level_of(atlas->input.view.zoom), scratch);
}

static void atlas_draw_job(job_worker_t *worker, void *borrowed_argument) {
    ui_atlas_t *const atlas = borrowed_argument;
    atlas_draw_prepare(atlas, &worker->arena_job);
    atlas_band_t *const bands = arena_alloc_array(&atlas->arena_draw, UI_ATLAS_BANDS, sizeof(atlas_band_t), 8u);
    atomic_store_explicit(&atlas->bands_left, UI_ATLAS_BANDS, memory_order_relaxed);
    for (uint32_t b = 0u; b < UI_ATLAS_BANDS; b++) {
        bands[b] = (atlas_band_t){atlas, b, {0}};
        job_spawn(worker, &atlas->draw_group, atlas_band_job, &bands[b]);
    }
}

typedef struct atlas_picture_band_t {
    const ui_atlas_draw_input_t *input; /* 0 */
    const ui_atlas_target_t *target;    /* 8 */
    uint32_t band;                      /* 16 */
    uint32_t bands;                     /* 20 */
} atlas_picture_band_t;
static_assert(sizeof(atlas_picture_band_t) == 24);

static void atlas_picture_band_job(job_worker_t *worker, void *borrowed_argument) {
    const atlas_picture_band_t *const job = borrowed_argument;
    ui_atlas_draw_band(job->input, job->target, job->band, job->bands,
                       atlas_band_scratch(&worker->arena_job, &job->input->view, job->bands));
}

void ui_atlas_draw_picture(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target, job_system_t *system,
                           job_group_t *group, uint32_t bands, arena_t *scratch) {
    bands = bands == 0u ? 1u : (bands > UI_ATLAS_BANDS ? UI_ATLAS_BANDS : bands);
    if (system != nullptr) {
        atlas_picture_band_t jobs[UI_ATLAS_BANDS];
        for (uint32_t b = 0u; b < bands; b++) {
            jobs[b] = (atlas_picture_band_t){input, target, b, bands};
            job_submit(system, group, atlas_picture_band_job, &jobs[b]);
        }
        job_group_wait(system, group);
    } else {
        for (uint32_t b = 0u; b < bands; b++) {
            arena_reset(scratch);
            ui_atlas_draw_band(input, target, b, bands, atlas_band_scratch(scratch, &input->view, bands));
        }
        arena_reset(scratch);
    }
    ui_atlas_draw_finish(input, target);
}

void ui_atlas_draw_wait(ui_atlas_t *atlas) {
    if (atlas->drawing_busy != 0u && atlas->borrowed_job != nullptr) {
        job_group_wait(atlas->borrowed_job, &atlas->draw_group);
    }
}

/* Starts a picture of the view into the back picture: on the workers, or here with no job system. */
static void atlas_draw_start(ui_atlas_t *atlas, const ui_atlas_view_t *view, const ui_atlas_world_t *world) {
    const uint32_t back = atlas->front ^ 1u;
    const climate_t *const climate = atlas->borrowed_climate;
    const bool climate_open = climate != nullptr && climate_is_open(climate);
    atlas->input = (ui_atlas_draw_input_t){
        .view = *view,
        .ground = {.borrowed_context = atlas, .read = ui_atlas_store_read},
        .borrowed_climate = climate_open ? climate : nullptr, /* a world with no climate draws the ground only */
        .borrowed_week = climate_open ? climate_week_store(climate) : nullptr,
        .borrowed_font = atlas->borrowed_font,
        .borrowed_systems = nullptr,
        .t = world->t,
        .census_week = world->census_week,
        .sample = UI_ATLAS_SAMPLE_PIXELS,
        .flat = 0u,
        ._pad0 = {0},
    };
    atlas->target = (ui_atlas_target_t){
        .borrowed_pixels = atlas->borrowed_pictures[back],
        .borrowed_grey = atlas->borrowed_greys[back],
        .borrowed_field = atlas->borrowed_field,
        .borrowed_scratch = atlas->borrowed_overlay_scratch,
    };
    arena_reset(&atlas->arena_draw);
    atomic_store_explicit(&atlas->draw_done, 0u, memory_order_relaxed);
    atlas->drawing_busy = 1u;
    if (atlas->borrowed_job == nullptr) {
        arena_reset(&atlas->arena_band);
        atlas_draw_prepare(atlas, &atlas->arena_band);
        for (uint32_t b = 0u; b < UI_ATLAS_BANDS; b++) {
            arena_reset(&atlas->arena_band);
            ui_atlas_draw_band(&atlas->input, &atlas->target, b, UI_ATLAS_BANDS,
                               atlas_band_scratch(&atlas->arena_band, &atlas->input.view, UI_ATLAS_BANDS));
        }
        ui_atlas_draw_finish(&atlas->input, &atlas->target);
        atomic_store_explicit(&atlas->draw_done, 1u, memory_order_release);
        return;
    }
    job_submit(atlas->borrowed_job, &atlas->draw_group, atlas_draw_job, atlas);
}

/* ---- On screen ---------------------------------------------------------------------------------------------------- */

bool ui_atlas_up(const ui_atlas_t *atlas) {
    return atlas != nullptr && atlas->up != 0u;
}

void ui_atlas_hide(ui_atlas_t *atlas) {
    if (atlas == nullptr) {
        return;
    }
    atlas->up = 0u;
    atlas->dragging = 0u;
    ui_atlas_draw_wait(atlas);
}

/* The view's blocks a pixel, and the block under a canvas pixel by the plan's formula. */
static void atlas_block_at(const ui_atlas_view_t *view, int32_t px, int32_t py, float64_t out[2]) {
    const float64_t s = ui_atlas_blocks_per_pixel(view->zoom);
    out[0] = view->center[0] + ((float64_t)px + 0.5f64 - (float64_t)view->width / 2.0f64) * s;
    out[1] = view->center[1] - ((float64_t)py + 0.5f64 - (float64_t)view->height / 2.0f64) * s;
}

static ui_rect_t atlas_slider_rect(uint32_t width, uint32_t height) {
    const int32_t scale = (int32_t)ui_widget_scale_for(width, height);
    const int32_t w = 160 * scale, h = 12 * scale;
    return (ui_rect_t){((int32_t)width - w) / 2, (int32_t)height - h - 4 * scale, w, h};
}

bool ui_atlas_input(ui_atlas_t *atlas, ui_widget_t *widget, const input_frame_t *frame, uint32_t width, uint32_t height,
                    const ui_atlas_world_t *world, bool screen_up) {
    if (atlas == nullptr || atlas->open == 0u) {
        return false;
    }
    ui_atlas_view_t *const view = &atlas->wanted;
    if (atlas->up == 0u) {
        if (!input_keys_has(&frame->pressed, KEY_M) || screen_up) {
            return false;
        }
        if (atlas->opened_once == 0u) {
            view->center[0] = world->player[0];
            view->center[1] = world->player[1];
            view->zoom = UI_ATLAS_ZOOM_DEFAULT;
            view->layer = 0u;
            view->day = world->day_of_year;
            atlas->opened_once = 1u;
        }
        atlas->too_big = width > UI_ATLAS_PICTURE_MAX_WIDTH || height > UI_ATLAS_PICTURE_MAX_HEIGHT ? 1u : 0u;
        atlas->up = 1u;
        atlas->dragging = 0u;
        atlas->readout_valid = 0u;
        return true;
    }
    ui_widget_input_feed(widget, frame);
    if (input_keys_has(&frame->pressed, KEY_M) || input_keys_has(&frame->pressed, KEY_ESC)) {
        atlas->up = 0u;
        atlas->dragging = 0u;
        return false;
    }
    view->width = width;
    view->height = height;
    const ui_atlas_layer_t *const row = &ui_atlas_layers[view->layer];
    for (uint32_t key = 0u; key < UI_ATLAS_GROUPS; key++) {
        if (input_keys_has(&frame->pressed, KEY_1 + key)) {
            view->layer = (uint8_t)ui_atlas_layer_next_in_group(view->layer, key + 1u);
        }
    }
    const bool shift = input_keys_has(&frame->down, KEY_LEFTSHIFT) || input_keys_has(&frame->down, KEY_RIGHTSHIFT);
    const int32_t step = shift ? 30 : 1;
    if (row->time == UI_ATLAS_TIME_NORMALS) {
        int32_t day = view->day;
        day += input_keys_has(&frame->pressed, KEY_RIGHTBRACE) ? step : 0;
        day -= input_keys_has(&frame->pressed, KEY_LEFTBRACE) ? step : 0;
        day %= (int32_t)UI_ATLAS_DAYS;
        view->day = (uint16_t)(day < 0 ? day + (int32_t)UI_ATLAS_DAYS : day);
    }
    if (input_keys_has(&frame->pressed, KEY_P)) {
        view->center[0] = world->player[0];
        view->center[1] = world->player[1];
    }
    const int32_t px = widget->input.pointer_x, py = widget->input.pointer_y;
    if (frame->wheel != 0) {
        /* About the pointer: the block under it stays under it. */
        float64_t before[2];
        atlas_block_at(view, px, py, before);
        int64_t wide = (int64_t)view->zoom - (int64_t)frame->wheel;
        wide = wide < UI_ATLAS_ZOOM_MIN ? UI_ATLAS_ZOOM_MIN : (wide > UI_ATLAS_ZOOM_MAX ? UI_ATLAS_ZOOM_MAX : wide);
        const int32_t zoom = (int32_t)wide;
        view->zoom = zoom;
        const float64_t s = ui_atlas_blocks_per_pixel(zoom);
        view->center[0] = before[0] - ((float64_t)px + 0.5f64 - (float64_t)width / 2.0f64) * s;
        view->center[1] = before[1] + ((float64_t)py + 0.5f64 - (float64_t)height / 2.0f64) * s;
    }
    const bool on_slider =
        row->time == UI_ATLAS_TIME_NORMALS && ui_rect_contains(atlas_slider_rect(width, height), px, py);
    if (widget->input.left_pressed != 0u && !on_slider) {
        atlas->dragging = 1u;
        atlas->drag_from[0] = px;
        atlas->drag_from[1] = py;
        atlas->drag_center[0] = view->center[0];
        atlas->drag_center[1] = view->center[1];
    }
    if (atlas->dragging != 0u) {
        const float64_t s = ui_atlas_blocks_per_pixel(view->zoom);
        view->center[0] = atlas->drag_center[0] - (float64_t)(px - atlas->drag_from[0]) * s;
        view->center[1] = atlas->drag_center[1] + (float64_t)(py - atlas->drag_from[1]) * s;
        atlas->dragging = widget->input.left_down != 0u ? 1u : 0u;
    }
    return true;
}

void ui_atlas_frame(ui_atlas_t *atlas, uint32_t width, uint32_t height, const ui_atlas_world_t *world) {
    if (atlas == nullptr || atlas->open == 0u) {
        return;
    }
    if (atlas->drawing_busy != 0u && atomic_load_explicit(&atlas->draw_done, memory_order_acquire) != 0u) {
        if (atlas->borrowed_job != nullptr) {
            job_group_wait(atlas->borrowed_job, &atlas->draw_group); /* the last band is past its last write */
        }
        atlas->front ^= 1u;
        atlas->shown = atlas->input.view;
        atlas->has_shown = 1u;
        atlas->drawing_busy = 0u;
        atlas->pictures_drawn++;
    }
    if (atlas->up == 0u || atlas->too_big != 0u) {
        return;
    }
    atlas->now_ns = world->world_ns;
    ui_atlas_view_t want = atlas->wanted;
    want.width = width;
    want.height = height;
    const ui_atlas_layer_t *const row = &ui_atlas_layers[want.layer];
    want.day = row->time == UI_ATLAS_TIME_NORMALS ? want.day : 0u;
    want.now_bucket = row->time == UI_ATLAS_TIME_NOW
                          ? (uint64_t)(world->world_ns / ((int64_t)UI_ATLAS_NOW_REDRAW_MS * (int64_t)ATLAS_MS))
                          : 0u;
    want.week = row->time == UI_ATLAS_TIME_WEEK ? world->census_week : 0u;
    want._pad0[0] = 0u;
    if (atlas->has_shown == 0u ||
        world->world_ns - atlas->ground_sampled_ns >= (int64_t)UI_ATLAS_GROUND_SAMPLE_MS * (int64_t)ATLAS_MS ||
        world->world_ns < atlas->ground_sampled_ns) {
        atlas_lock(&atlas->store);
        atlas->ground_revision = atlas->store.revision;
        atlas_unlock(&atlas->store);
        atlas->ground_sampled_ns = world->world_ns;
    }
    want.ground_revision = atlas->ground_revision;
    if (atlas->drawing_busy == 0u && (atlas->has_shown == 0u || memcmp(&want, &atlas->shown, sizeof(want)) != 0)) {
        atlas_draw_start(atlas, &want, world);
        if (atlas->borrowed_job == nullptr) {
            atlas->front ^= 1u;
            atlas->shown = atlas->input.view;
            atlas->has_shown = 1u;
            atlas->drawing_busy = 0u;
            atlas->pictures_drawn++;
        }
    }
}

static const char *const ATLAS_CLASS_NAMES[CLIMATE_CLASS_COUNT] = {
    "Sea",     "Ice",     "Tundra",        "Desert",    "Steppe",      "Rainforest",
    "Monsoon", "Savanna", "Mediterranean", "Temperate", "Continental", "Boreal"};

/* Where the wind comes from, as one of eight compass points: the way back along it. */
static const char *atlas_compass_from(float32_t u, float32_t v) {
    static const char *const POINTS[8] = {"W", "SW", "S", "SE", "E", "NE", "N", "NW"};
    /* The wind blows toward (u, v); it comes from (-u, -v). atan2 of the toward direction, 0 east. */
    const float32_t angle = atan2f(v, u);
    int32_t sector = (int32_t)lroundf(angle / (3.14159265f32 / 4.0f32));
    sector = ((sector % 8) + 8) % 8;
    return POINTS[sector];
}

void ui_atlas_readout(ui_atlas_t *atlas, int64_t x, int64_t y, uint32_t layer, uint16_t day,
                      const ui_atlas_world_t *world, char *out, uint64_t bytes) {
    const float64_t lat = climate_latitude_deg((float64_t)y + 0.5f64);
    char where[96];
    (void)snprintf(where, sizeof(where), "%.1f\xC2\xB0%s  x %" PRId64 " y %" PRId64, (libc_double_t)fabs(lat),
                   lat >= 0.0f64 ? "N" : "S", x, y);
    /* The height and whether it is explored: the level-0 pixel, under the lock, if its tile is in memory. */
    uint32_t pixel = UI_ATLAS_PIXEL_NONE;
    ui_atlas_store_t *const store = (ui_atlas_store_t *)&atlas->store;
    atlas_lock(store);
    const int32_t index = atlas_find(store, 0u, ui_atlas_tile_of(x, 0u), ui_atlas_tile_of(y, 0u));
    if (index >= 0) {
        const uint32_t i = ui_atlas_in_tile(ui_atlas_pixel_column_of(x, 0u));
        const uint32_t j = ui_atlas_in_tile(ui_atlas_pixel_row_of(y, 0u));
        pixel = store->borrowed_slots[index].borrowed_pixels[(uint64_t)j * UI_ATLAS_TILE_SIDE + i];
    }
    atlas_unlock(store);
    if (!ui_atlas_pixel_generated(pixel)) {
        (void)snprintf(out, bytes, "%s  not explored", where);
        return;
    }
    const climate_t *const climate = atlas->borrowed_climate;
    climate_normals_at_t normals;
    climate_air_t air;
    climate_cell_t cell;
    const float64_t bx = (float64_t)x + 0.5f64, by = (float64_t)y + 0.5f64;
    if (climate == nullptr || !climate_is_open(climate) ||
        !climate_normals_at(climate, bx, by, nanf(""), (float32_t)day, &normals) ||
        !climate_air_at(climate, bx, by, (float32_t)day, &air) ||
        !climate_cell_get(climate, (int32_t)x, (int32_t)y, &cell)) {
        (void)snprintf(out, bytes, "%s  z %" PRId32 "\nthe climate here is not worked out yet", where,
                       (int32_t)ui_atlas_pixel_z(pixel));
        return;
    }
    const char *const current = cell.current == CLIMATE_CURRENT_WARM   ? "warm"
                                : cell.current == CLIMATE_CURRENT_COLD ? "cold"
                                                                       : "neutral";
    const uint32_t climate_class = normals.climate_class < CLIMATE_CLASS_COUNT ? normals.climate_class : 0u;
    if (ui_atlas_layers[layer].time == UI_ATLAS_TIME_NOW) {
        /* Now values: the weather at the explored ground (sea level over the sea) at the world's moment, with the last
         * census's week. arena_band is main's, and free outside an inline draw. */
        const int64_t cell_x = (int64_t)floor((float64_t)x / (float64_t)CLIMATE_CELL_BLOCKS);
        const int64_t cell_y = (int64_t)floor((float64_t)y / (float64_t)CLIMATE_CELL_BLOCKS);
        const climate_week_store_t *const week = climate_week_store(climate);
        const climate_week_t *const last =
            week != nullptr ? climate_week_store_cell(week, (int32_t)cell_x, (int32_t)cell_y) : nullptr;
        const int16_t z = ui_atlas_pixel_z(pixel);
        climate_now_t now;
        arena_reset(&atlas->arena_band);
        if (!climate_weather_at(climate, bx, by, z > 0 ? (float64_t)z : 0.0f64, world->t, last, &atlas->arena_band,
                                &now)) {
            (void)snprintf(out, bytes, "%s  z %" PRId32 "\nthe weather here is not worked out yet", where, (int32_t)z);
            return;
        }
        (void)snprintf(out, bytes,
                       "%s  z %" PRId32 "\n%.1f \xC2\xB0"
                       "C  %.1f mm an hour  wind %.1f from %s  %.0f hPa  current %s  %s",
                       where, (int32_t)z, (libc_double_t)now.temperature_c, (libc_double_t)now.rain_mm_h,
                       (libc_double_t)sqrtf(now.wind[0] * now.wind[0] + now.wind[1] * now.wind[1]),
                       atlas_compass_from(now.wind[0], now.wind[1]), (libc_double_t)now.pressure_hpa, current,
                       ATLAS_CLASS_NAMES[climate_class]);
        return;
    }
    const float32_t speed = sqrtf(air.wind[0] * air.wind[0] + air.wind[1] * air.wind[1]);
    (void)snprintf(out, bytes,
                   "%s  z %" PRId32 "\n%.1f \xC2\xB0"
                   "C  %.0f mm a year  wind %.1f from %s  %.0f hPa  current %s  %s",
                   where, (int32_t)ui_atlas_pixel_z(pixel), (libc_double_t)normals.t_day_c,
                   (libc_double_t)normals.rain_mm_year, (libc_double_t)speed,
                   atlas_compass_from(air.wind[0], air.wind[1]), (libc_double_t)air.pressure_hpa, current,
                   ATLAS_CLASS_NAMES[climate_class]);
}

bool ui_atlas_layer_scale(uint32_t layer, ui_atlas_scale_t *out) {
    const ui_atlas_layer_t *const row = &ui_atlas_layers[layer];
    if (row->scale != nullptr) {
        *out = *row->scale;
        return true;
    }
    climate_layer_t source = (climate_layer_t)row->climate_layer;
    if (row->value == UI_ATLAS_VALUE_RAIN_RADAR) {
        source = CLIMATE_LAYER_RAIN_RADAR;
    } else if (row->value == UI_ATLAS_VALUE_WIND_NOW) {
        source = CLIMATE_LAYER_WIND_NOW;
    } else if (row->value == UI_ATLAS_VALUE_PRESSURE_NOW) {
        source = CLIMATE_LAYER_PRESSURE_NOW;
    }
    const float32_t *values = nullptr;
    const uint32_t *colours = nullptr;
    uint32_t count = 0u;
    if (row->climate_layer == UI_ATLAS_NO_CLIMATE_LAYER && row->value == UI_ATLAS_VALUE_NONE) {
        return false;
    }
    if (!climate_layer_scale(source, &values, &colours, &count)) {
        return false;
    }
    *out = (ui_atlas_scale_t){values, colours, count, {0}};
    return true;
}

static void atlas_panel_text(ui_canvas_t *canvas, const ui_font_t *font, int32_t x, int32_t y, const char *text,
                             uint32_t scale, bool bottom) {
    const ui_text_size_t size = ui_font_measure(font, text, scale);
    const int32_t pad = 3 * (int32_t)scale;
    const int32_t top = bottom ? y - (int32_t)size.height - 2 * pad : y;
    ui_compose_fill(canvas, (ui_rect_t){x, top, (int32_t)size.width + 2 * pad, (int32_t)size.height + 2 * pad},
                    0xC0101010u);
    ui_font_draw(font, canvas, x + pad, top + pad, text,
                 (ui_text_style_t){.color = 0xFFFFFFFFu, .shadow = 0u, .scale = scale});
}

void ui_atlas_frame_draw(ui_atlas_t *atlas, ui_widget_t *widget, ui_canvas_t *canvas, const ui_font_t *font,
                         const ui_atlas_world_t *world) {
    const uint32_t width = canvas->width, height = canvas->height;
    const uint32_t scale = ui_widget_scale_for(width, height);
    const ui_rect_t whole = {0, 0, (int32_t)width, (int32_t)height};
    if (atlas->too_big != 0u) {
        ui_compose_fill(canvas, whole, UI_ATLAS_COLOUR_DARK);
        atlas_panel_text(canvas, font, 8, 8, "The Atlas needs a screen of 3840 by 2160 or less. M or Esc to close.",
                         scale, false);
        return;
    }
    ui_atlas_view_t *const view = &atlas->wanted;
    const ui_atlas_layer_t *const row = &ui_atlas_layers[view->layer];
    /* The last picture, moved by the pan since it was drawn, the uncovered edge dark until the new one lands. */
    ui_atlas_geometry_t shown_geometry = {0};
    int32_t shift[2] = {0, 0};
    const bool same_size = atlas->has_shown != 0u && atlas->shown.width == width && atlas->shown.height == height;
    if (same_size) {
        ui_atlas_view_t now = *view;
        now.width = width;
        now.height = height;
        shown_geometry = ui_atlas_geometry(&atlas->shown);
        if (now.zoom == atlas->shown.zoom) {
            const ui_atlas_geometry_t now_geometry = ui_atlas_geometry(&now);
            shift[0] = (int32_t)(shown_geometry.column0 - now_geometry.column0);
            shift[1] = (int32_t)(shown_geometry.row0 - now_geometry.row0);
        }
        const ui_image_t image = {atlas->borrowed_pictures[atlas->front], width, height};
        ui_compose_image_whole(canvas, &image, (ui_rect_t){shift[0], shift[1], (int32_t)width, (int32_t)height});
        /* Only the strips a pan uncovered: a full fill as well would cost a second pass over the screen every frame. */
        const int32_t w = (int32_t)width, h = (int32_t)height;
        if (shift[0] > 0) {
            ui_compose_fill(canvas, (ui_rect_t){0, 0, shift[0], h}, UI_ATLAS_COLOUR_DARK);
        } else if (shift[0] < 0) {
            ui_compose_fill(canvas, (ui_rect_t){w + shift[0], 0, -shift[0], h}, UI_ATLAS_COLOUR_DARK);
        }
        if (shift[1] > 0) {
            ui_compose_fill(canvas, (ui_rect_t){0, 0, w, shift[1]}, UI_ATLAS_COLOUR_DARK);
        } else if (shift[1] < 0) {
            ui_compose_fill(canvas, (ui_rect_t){0, h + shift[1], w, -shift[1]}, UI_ATLAS_COLOUR_DARK);
        }
    } else {
        ui_compose_fill(canvas, whole, UI_ATLAS_COLOUR_DARK);
    }
    /* Title. */
    char title[160];
    const char *const when = row->time == UI_ATLAS_TIME_NOW    ? ", now"
                             : row->time == UI_ATLAS_TIME_WEEK ? ", last week"
                                                               : "";
    if (row->time == UI_ATLAS_TIME_NORMALS) {
        (void)snprintf(title, sizeof(title), "Atlas: %s, day %u\n1-9 layers, [ ] day, P you, M close", row->label,
                       (uint32_t)view->day + 1u);
    } else {
        (void)snprintf(title, sizeof(title), "Atlas: %s%s\n1-9 layers, [ ] day, P you, M close", row->label, when);
    }
    atlas_panel_text(canvas, font, 4 * (int32_t)scale, 4 * (int32_t)scale, title, scale, false);
    /* Legend. */
    ui_atlas_scale_t legend_scale;
    const bool has_scale = ui_atlas_layer_scale(view->layer, &legend_scale);
    const float32_t arrow_speed = row->arrows != 0u ? 10.0f32 : 0.0f32;
    const int32_t legend_width =
        ui_atlas_legend_width(font, scale, row->label, row->unit, row->entries, row->entry_count, arrow_speed);
    ui_rect_t legend = {(int32_t)width - legend_width - 4 * (int32_t)scale, 0, legend_width,
                        (int32_t)height - 4 * (int32_t)scale};
    if (row->time == UI_ATLAS_TIME_NORMALS) {
        /* Above the day slider where the two would meet, so the slider never covers the legend's last line. */
        const ui_rect_t slider = atlas_slider_rect(width, height);
        if (slider.x + slider.width > legend.x) {
            legend.height = slider.y - 2 * (int32_t)scale;
        }
    }
    const int32_t legend_height =
        ui_atlas_draw_legend(canvas, font, legend, scale, row->label, row->unit, has_scale ? &legend_scale : nullptr,
                             row->entries, row->entry_count, arrow_speed);
    /* Day slider. */
    if (row->time == UI_ATLAS_TIME_NORMALS) {
        char label[32];
        (void)snprintf(label, sizeof(label), "Day %u of 365", (uint32_t)view->day + 1u);
        int32_t day = view->day;
        if (ui_widget_slider(widget, UI_ATLAS_SLIDER_ID, atlas_slider_rect(width, height), label, &day, 0,
                             (int32_t)UI_ATLAS_DAYS - 1)) {
            view->day = (uint16_t)day;
        }
    }
    /* Readout, worked out again only when the pointer's block, the layer or the day changes, and on a now layer as often
     * as its picture. */
    view->width = width;
    view->height = height;
    float64_t block[2];
    atlas_block_at(view, widget->input.pointer_x, widget->input.pointer_y, block);
    const int32_t bx = (int32_t)floor(block[0]), by = (int32_t)floor(block[1]);
    const uint16_t day = row->time == UI_ATLAS_TIME_NORMALS ? view->day : world->day_of_year;
    /* On a now layer the key is the half second, as the picture's; a layer has only one of the two. */
    const uint16_t key = row->time == UI_ATLAS_TIME_NOW
                             ? (uint16_t)(world->world_ns / ((int64_t)UI_ATLAS_NOW_REDRAW_MS * (int64_t)ATLAS_MS))
                             : day;
    if (atlas->readout_valid == 0u || bx != atlas->readout_block[0] || by != atlas->readout_block[1] ||
        atlas->readout_view_layer != view->layer || atlas->readout_view_day != key) {
        ui_atlas_readout(atlas, bx, by, view->layer, day, world, atlas->readout, sizeof(atlas->readout));
        atlas->readout_block[0] = bx;
        atlas->readout_block[1] = by;
        atlas->readout_view_layer = view->layer;
        atlas->readout_view_day = key;
        atlas->readout_valid = 1u;
    }
    int32_t readout_bottom = (int32_t)height - (row->time == UI_ATLAS_TIME_NORMALS ? 20 : 4) * (int32_t)scale;
    /* On a narrow screen the readout reaching under the legend goes above it instead. */
    const int32_t readout_right =
        4 * (int32_t)scale + (int32_t)ui_font_measure(font, atlas->readout, scale).width + 6 * (int32_t)scale;
    const int32_t legend_top = legend.y + legend.height - legend_height;
    if (readout_right > legend.x && readout_bottom > legend_top - 2 * (int32_t)scale) {
        readout_bottom = legend_top - 2 * (int32_t)scale;
    }
    atlas_panel_text(canvas, font, 4 * (int32_t)scale, readout_bottom, atlas->readout, scale, true);
    /* The player: a white triangle with a dark edge, pointing where they look, where the shown picture has them. */
    if (same_size) {
        const float64_t s = shown_geometry.step;
        const float32_t px =
            (float32_t)(world->player[0] / s - (float64_t)shown_geometry.column0) + (float32_t)shift[0];
        const float32_t py = (float32_t)(-world->player[1] / s - (float64_t)shown_geometry.row0) + (float32_t)shift[1];
        const float32_t fx = cosf(world->yaw), fy = -sinf(world->yaw);
        const float32_t size = 7.0f32 * (float32_t)scale;
        const float32_t tip[2] = {px + fx * size, py + fy * size};
        const float32_t left[2] = {px - fx * size * 0.6f32 - fy * size * 0.6f32,
                                   py - fy * size * 0.6f32 + fx * size * 0.6f32};
        const float32_t right[2] = {px - fx * size * 0.6f32 + fy * size * 0.6f32,
                                    py - fy * size * 0.6f32 - fx * size * 0.6f32};
        /* Filled by rows: a pixel is in when it is on the inner side of all three edges. */
        const int32_t x0 = (int32_t)floorf(fminf(tip[0], fminf(left[0], right[0]))) - 1;
        const int32_t x1 = (int32_t)ceilf(fmaxf(tip[0], fmaxf(left[0], right[0]))) + 1;
        const int32_t y0 = (int32_t)floorf(fminf(tip[1], fminf(left[1], right[1]))) - 1;
        const int32_t y1 = (int32_t)ceilf(fmaxf(tip[1], fmaxf(left[1], right[1]))) + 1;
        const float32_t *const corners[3] = {tip, left, right};
        for (int32_t yy = y0; yy <= y1; yy++) {
            for (int32_t xx = x0; xx <= x1; xx++) {
                const float32_t cx = (float32_t)xx + 0.5f32, cy = (float32_t)yy + 0.5f32;
                float32_t least = 1.0e9f32;
                bool inside = true;
                for (uint32_t e = 0u; e < 3u; e++) {
                    const float32_t *const a = corners[e];
                    const float32_t *const b = corners[(e + 1u) % 3u];
                    const float32_t ex = b[0] - a[0], ey = b[1] - a[1];
                    const float32_t length = sqrtf(ex * ex + ey * ey);
                    /* The corners go round one way; the cross product's sign says which side. */
                    const float32_t d = (ex * (cy - a[1]) - ey * (cx - a[0])) / (length > 0.0f32 ? length : 1.0f32);
                    const float32_t side =
                        (right[0] - tip[0]) * (left[1] - tip[1]) - (right[1] - tip[1]) * (left[0] - tip[0]);
                    const float32_t inner = side > 0.0f32 ? -d : d;
                    inside = inside && inner >= 0.0f32;
                    least = fminf(least, inner);
                }
                if (inside) {
                    ui_compose_fill(canvas, (ui_rect_t){xx, yy, 1, 1},
                                    least < (float32_t)scale ? 0xFF202020u : 0xFFFFFFFFu);
                }
            }
        }
    }
}
