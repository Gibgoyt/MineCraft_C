/* See run/src/world/edit/README.md. */
#ifndef MC_RUN_WORLD_EDIT_INDEX_H
#define MC_RUN_WORLD_EDIT_INDEX_H

#include "arena/index.h"
#include "machine/index.h"
#include "world/column/index.h"
#include "world/map/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Who wants to know that a section changed. Each has its own flags, so none of them clears another's. */
typedef enum world_edit_consumer_t : uint8_t {
    WORLD_EDIT_CONSUMER_RENDER = 0,
    WORLD_EDIT_CONSUMER_SAVE = 1,
    WORLD_EDIT_CONSUMER_LIGHT = 2,
    WORLD_EDIT_CONSUMER_COUNT = 3,
} world_edit_consumer_t;

enum : uint32_t {
    WORLD_EDIT_FOR_RENDER = 1u << WORLD_EDIT_CONSUMER_RENDER,
    WORLD_EDIT_FOR_SAVE = 1u << WORLD_EDIT_CONSUMER_SAVE,
    WORLD_EDIT_FOR_LIGHT = 1u << WORLD_EDIT_CONSUMER_LIGHT,
    WORLD_EDIT_FOR_ALL = WORLD_EDIT_FOR_RENDER | WORLD_EDIT_FOR_SAVE | WORLD_EDIT_FOR_LIGHT,
};
enum : uint64_t {
    /* One bit per section of a column. */
    WORLD_EDIT_SECTIONS_ALL = (1ull << COLUMN_SECTIONS) - 1ull,
    /* What world_open gives the log: at 16 bytes each, 1 MiB. Far more than a tick's worth of a large explosion. */
    WORLD_EDIT_LOG_CAPACITY_DEFAULT = 1ull << 16,
};
static_assert(COLUMN_SECTIONS < 64u); /* the mask above */

/* The highest z with a block in a column with none. */
#define WORLD_EDIT_TOP_Z_NONE ((int16_t)(COLUMN_Z_STORAGE_MIN - 1))

/* One block that changed. The light does not go in the log: a pass writes far too much of it to be worth reading back. */
typedef struct world_edit_record_t {
    int32_t x;       /* 0 */
    int32_t y;       /* 4 */
    int32_t z;       /* 8 */
    uint16_t before; /* 12 */
    uint16_t after;  /* 14 */
} world_edit_record_t;
static_assert(sizeof(world_edit_record_t) == 16);

/* What is known about the column in one slot of the map. Zero bytes mean nothing is known. */
typedef struct world_edit_column_t {
    uint64_t dirty[WORLD_EDIT_CONSUMER_COUNT]; /* 0: one bit per section, one mask per consumer */
    uint64_t revision;                         /* 24: of the last change here; see world_edit_t.revision */
    uint32_t generation;                       /* 32: the map's, for the load all this describes */
    int16_t top_z;                             /* 36: no block is above the top of this section */
    uint8_t queued;                            /* 38: WORLD_EDIT_FOR_*: this slot is waiting in that queue */
    uint8_t _pad0[1];                          /* 39 */
} world_edit_column_t;
static_assert(sizeof(world_edit_column_t) == 40);

/* Dirty sections of one column, for one consumer. */
typedef struct world_edit_dirty_t {
    uint64_t section_mask; /* 0 */
    int32_t chunk_x;       /* 8 */
    int32_t chunk_y;       /* 12 */
    uint32_t slot;         /* 16 */
    uint32_t generation;   /* 20 */
} world_edit_dirty_t;
static_assert(sizeof(world_edit_dirty_t) == 24);

typedef enum world_edit_status_t : uint8_t {
    WORLD_EDIT_STATUS_CHANGED = 0,
    WORLD_EDIT_STATUS_UNCHANGED = 1,      /* it already was that, so nothing is logged or flagged */
    WORLD_EDIT_STATUS_Z_OUT_OF_RANGE = 2, /* nothing was changed */
    WORLD_EDIT_STATUS_NOT_LOADED = 3,     /* nothing was changed */
    WORLD_EDIT_STATUS_NO_MEMORY = 4,      /* the pool has no room: nothing was changed */
} world_edit_status_t;

/* How a world changes once a column is in it: a log of blocks, and dirty flags per section and per consumer. One thread at
 * a time, the one holding the map. */
typedef struct world_edit_t {
    world_map_t *borrowed_map;                            /* 0 */
    world_edit_record_t *borrowed_log;                    /* 8: a ring of log_capacity */
    world_edit_column_t *borrowed_columns;                /* 16: the map's slot_count */
    uint32_t *borrowed_queues[WORLD_EDIT_CONSUMER_COUNT]; /* 24: rings of slot numbers, in the order they got dirty */
    uint64_t log_capacity;                                /* 48: a power of two */
    uint64_t log_next;                                    /* 56: the sequence number the next record gets */
    uint64_t revision;                                    /* 64: counts every change of anything here */
    uint32_t queue_head[WORLD_EDIT_CONSUMER_COUNT];       /* 72 */
    uint32_t queue_count[WORLD_EDIT_CONSUMER_COUNT];      /* 84 */
} world_edit_t;
static_assert(sizeof(world_edit_t) == 96);

/* What world_edit_init takes from its arena, with a slot_count from the map. */
enum : uint64_t {
    WORLD_EDIT_BYTES_PER_SLOT = sizeof(world_edit_column_t) + WORLD_EDIT_CONSUMER_COUNT * sizeof(uint32_t),
    WORLD_EDIT_BYTES_FIXED = 8ull * 64ull, /* room for alignment */
};

/* log_capacity must be a power of two. Takes from the arena, which must outlive this, and aborts as for running out if it
 * is too small. The map must stay initialized for as long as this is. */
void world_edit_init(world_edit_t *edit, arena_t *arena, world_map_t *map, uint64_t log_capacity);
void world_edit_deinit(world_edit_t *edit);

/* One voxel, at world coordinates. A block goes in the log and flags its section for every consumer. Light flags it for
 * RENDER only, since the light pass is who writes it and a save rebuilds it. */
world_edit_status_t world_edit_block_set(world_edit_t *edit, int32_t x, int32_t y, int32_t z, uint16_t state);
world_edit_status_t world_edit_light_set(world_edit_t *edit, int32_t x, int32_t y, int32_t z, uint8_t packed);

/* For whoever just wrote a live column's sections directly: flags those sections, for those consumers. */
void world_edit_sections_mark(world_edit_t *edit, uint32_t slot, uint64_t section_mask, uint32_t consumers);

/* For a column just committed and filled with what generates there: it is not an edit and so is not logged and not for
 * SAVE. A section still all air under open sky is what a renderer starts with, so only the others are flagged. */
void world_edit_column_filled(world_edit_t *edit, uint32_t slot);

/* The sequence number the next record will get. A reader starting now sets its cursor to this. */
static inline uint64_t world_edit_log_position(const world_edit_t *edit) {
    return edit->log_next;
}

/* Copies out up to max records from *cursor on, oldest first, moves *cursor past them, and returns how many. If the ring
 * has gone past the cursor, *out_lost says how many records that reader will never see, and it must look at the world
 * itself. Each reader keeps its own cursor. */
uint64_t world_edit_log_read(const world_edit_t *edit, uint64_t *cursor, world_edit_record_t *out, uint64_t max,
                             uint64_t *out_lost);

/* The longest-waiting column dirty for this consumer, with all its flags, which it now holds: they are cleared here. False
 * if there is none. Flags of a column that has been unloaded are gone with it. */
bool world_edit_dirty_next(world_edit_t *edit, world_edit_consumer_t consumer, world_edit_dirty_t *out);

/* Gives back what world_edit_dirty_next handed over and the consumer could not finish, or any part of its mask. It changes
 * nothing in the world, so the revision stays. Out of date if that column has gone, and then it does nothing. */
void world_edit_dirty_restore(world_edit_t *edit, const world_edit_dirty_t *dirty, world_edit_consumer_t consumer);

/* Without taking them: for a column that world_map_stream_leaving offers, whether it has anything unsaved. 0 for no column.
 */
uint64_t world_edit_dirty_peek(world_edit_t *edit, uint32_t slot, world_edit_consumer_t consumer);

/* Clears flags without handing anything over. */
void world_edit_dirty_clear(world_edit_t *edit, uint32_t slot, world_edit_consumer_t consumer, uint64_t section_mask);

/* The last change to a live column, and the top of its highest section with a block. A section whose array has gone back
 * to all air, unnoticed until section_compact, still counts: so this is never below the true top, and may be above it. */
uint64_t world_edit_column_revision(world_edit_t *edit, uint32_t slot);
int16_t world_edit_column_top_z(world_edit_t *edit, uint32_t slot);

/* ---- Snapshots: what the sim thread publishes and the gpu thread reads --------------------------------------------- */

enum : uint32_t {
    /* One being read, one newest and waiting, one being written: so neither side ever waits for the other. */
    WORLD_EDIT_SNAPSHOT_VIEWS = 3u,
    /* What world_open gives it. A busy tick's worth: the rest stay dirty for the next. */
    WORLD_EDIT_SNAPSHOT_SECTIONS_DEFAULT = 1024u,
};

/* One slot of the table as it was when published. Zero bytes are no column there: a request on its way counts as none. */
typedef struct world_edit_snapshot_column_t {
    uint64_t revision;   /* 0 */
    int32_t chunk_x;     /* 8 */
    int32_t chunk_y;     /* 12 */
    uint32_t generation; /* 16: when this differs from what the reader held, its sections are all air under open sky,
                          * apart from any delivered since */
    int16_t top_z;       /* 20: see world_edit_column_top_z */
    uint8_t state;       /* 22: WORLD_MAP_STATE_EMPTY or WORLD_MAP_STATE_READY */
    uint8_t _pad0[1];    /* 23 */
} world_edit_snapshot_column_t;
static_assert(sizeof(world_edit_snapshot_column_t) == 24);

/* A section that was dirty for the renderer, as it was when published. */
typedef struct world_edit_snapshot_section_t {
    int32_t chunk_x;        /* 0 */
    int32_t chunk_y;        /* 4 */
    uint32_t slot;          /* 8 */
    uint32_t generation;    /* 12 */
    uint16_t block_uniform; /* 16: every voxel's, unless has_blocks */
    uint8_t section_index;  /* 18 */
    uint8_t light_uniform;  /* 19: every voxel's, unless has_light */
    uint8_t has_blocks;     /* 20: the array at this index of borrowed_voxels holds the blocks */
    uint8_t has_light;      /* 21: and its light */
    uint8_t _pad0[2];       /* 22 */
} world_edit_snapshot_section_t;
static_assert(sizeof(world_edit_snapshot_section_t) == 24);

/* In section_voxel_index order. Pages of a uniform section's entry are never touched. */
typedef struct world_edit_snapshot_voxels_t {
    uint16_t blocks[SECTION_VOXELS]; /* 0 */
    uint8_t light[SECTION_VOXELS];   /* 8192 */
} world_edit_snapshot_voxels_t;
static_assert(sizeof(world_edit_snapshot_voxels_t) == 12288);

/* The world at one moment. Holds no pointer into a section, so that the sim thread goes on writing while this is read. */
typedef struct world_edit_snapshot_view_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) uint64_t sequence;     /* 0: 1 for the first published; gaps are views skipped */
    uint64_t revision;                                      /* 8: world_edit_t.revision */
    const world_edit_snapshot_column_t *borrowed_columns;   /* 16: slot_count, indexed by slot */
    const world_edit_snapshot_section_t *borrowed_sections; /* 24: section_count */
    const world_edit_snapshot_voxels_t *borrowed_voxels;    /* 32: section_count */
    int32_t center_x;                                       /* 40 */
    int32_t center_y;                                       /* 44 */
    int32_t min_x;                                          /* 48 */
    int32_t min_y;                                          /* 52 */
    uint32_t min_slot_x;                                    /* 56 */
    uint32_t min_slot_y;                                    /* 60 */
    uint32_t side;                                          /* 64 */
    uint32_t slot_count;                                    /* 68 */
    uint32_t view_radius;                                   /* 72 */
    uint32_t ready_count;                                   /* 76 */
    uint32_t section_count;                                 /* 80 */
    uint8_t _pad0[44];                                      /* 84 */
} world_edit_snapshot_view_t;
static_assert(sizeof(world_edit_snapshot_view_t) == 2u * MC_CPU_CACHE_LINE_BYTES);
static_assert(alignof(world_edit_snapshot_view_t) == MC_CPU_CACHE_LINE_BYTES);

/* A triple buffer from one publishing thread to one reading thread. No locks, no syscalls. Each side has its own cache
 * line, and the third is the one word they swap through. */
typedef struct world_edit_snapshot_t {
    world_edit_snapshot_view_t views[WORLD_EDIT_SNAPSHOT_VIEWS]; /* 0 */

    /* The publisher's. */
    world_edit_t *borrowed_edit; /* 384 */
    uint64_t last_revision;      /* 392 */
    uint64_t last_map_change;    /* 400 */
    uint64_t published_count;    /* 408 */
    uint64_t overwritten_count;  /* 416: views replaced before they were read; their sections went back to dirty */
    uint32_t section_capacity;   /* 424 */
    uint32_t back;               /* 428: the view being written */
    uint8_t _pad0[16];           /* 432 */

    /* Both sides'. A view's number, and a bit for whether it is waiting unread. */
    _Atomic uint32_t exchange; /* 448 */
    uint8_t _pad1[60];         /* 452 */

    /* The reader's. */
    uint64_t acquired_count; /* 512 */
    uint32_t front;          /* 520: the view being read */
    uint8_t _pad2[52];       /* 524 */
} world_edit_snapshot_t;
static_assert(sizeof(world_edit_snapshot_t) == 9u * MC_CPU_CACHE_LINE_BYTES);
static_assert(alignof(world_edit_snapshot_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(world_edit_snapshot_t, borrowed_edit) == 6u * MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(world_edit_snapshot_t, exchange) == 7u * MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(world_edit_snapshot_t, acquired_count) == 8u * MC_CPU_CACHE_LINE_BYTES);

/* What world_edit_snapshot_init takes from its arena: per slot of the map, per section of capacity, and room for alignment. */
enum : uint64_t {
    WORLD_EDIT_SNAPSHOT_BYTES_PER_SLOT = WORLD_EDIT_SNAPSHOT_VIEWS * sizeof(world_edit_snapshot_column_t),
    WORLD_EDIT_SNAPSHOT_BYTES_PER_SECTION =
        WORLD_EDIT_SNAPSHOT_VIEWS * (sizeof(world_edit_snapshot_section_t) + sizeof(world_edit_snapshot_voxels_t)),
    WORLD_EDIT_SNAPSHOT_BYTES_FIXED = 16ull * 64ull,
};

/* section_capacity is the most sections one publish can carry, at least 1. Takes from the arena, which must outlive this,
 * and aborts as for running out if it is too small. Costs no pages until used. Call before either thread starts. */
void world_edit_snapshot_init(world_edit_snapshot_t *snapshot, arena_t *arena, world_edit_t *edit,
                              uint32_t section_capacity);
void world_edit_snapshot_deinit(world_edit_snapshot_t *snapshot);

/* The thread holding the map only, between its writes. Makes the world as it now is the newest view, with the longest-
 * waiting render-dirty sections up to the capacity, whose flags this takes. Never waits for the reader. A view replaced
 * before it was read gives its sections' flags back, so that a section is never lost, only sent later and newer. False,
 * doing nothing, if nothing has changed since the last publish. */
bool world_edit_snapshot_publish(world_edit_snapshot_t *snapshot);

/* The reading thread only. The newest view not yet read, or nullptr if there is none. Never waits for the publisher. The
 * view returned stays whole and unchanging until this next returns anything other than nullptr. */
const world_edit_snapshot_view_t *world_edit_snapshot_acquire(world_edit_snapshot_t *snapshot);

/* A view's record for a chunk, or nullptr if it has no column there. Any thread holding the view. */
static inline const world_edit_snapshot_column_t *world_edit_view_column_at(const world_edit_snapshot_view_t *view,
                                                                            int32_t chunk_x, int32_t chunk_y) {
    uint32_t slot = 0;
    if (!world_map_slot_in_window(view->min_x, view->min_y, view->min_slot_x, view->min_slot_y, view->side, chunk_x,
                                  chunk_y, &slot)) {
        return nullptr;
    }
    const world_edit_snapshot_column_t *const column = &view->borrowed_columns[slot];
    if (column->state != WORLD_MAP_STATE_READY || column->chunk_x != chunk_x || column->chunk_y != chunk_y) {
        return nullptr;
    }
    return column;
}

/* One voxel of a section the view carries. index is section_voxel_index. */
static inline uint16_t world_edit_view_block(const world_edit_snapshot_view_t *view, uint32_t section, uint32_t index) {
    const world_edit_snapshot_section_t *const entry = &view->borrowed_sections[section];
    return entry->has_blocks != 0u ? view->borrowed_voxels[section].blocks[index] : entry->block_uniform;
}
static inline uint8_t world_edit_view_light(const world_edit_snapshot_view_t *view, uint32_t section, uint32_t index) {
    const world_edit_snapshot_section_t *const entry = &view->borrowed_sections[section];
    return entry->has_light != 0u ? view->borrowed_voxels[section].light[index] : entry->light_uniform;
}

#endif
