/* See run/src/save/world/README.md. */
#ifndef MC_RUN_SAVE_WORLD_INDEX_H
#define MC_RUN_SAVE_WORLD_INDEX_H

#include "arena/index.h"
#include "file/index.h"
#include "world/column/index.h"
#include "world/edit/index.h"
#include "world/section/index.h"
#include "world/tick/index.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Raised by any change an older reader could not read. A file of another version is refused and left alone. Version 2: a record may end in levels.
     * Version 3: a column has 96 sections, up to z = 1250, so its mask in a delta file takes two words.
     * Version 4: a manifest says which generator made the land under it. What came before could not say, and is taken as an older generator's. */
    SAVE_WORLD_VERSION = 4u,
    SAVE_WORLD_VERSION_FIRST_WITH_GENERATOR = 4u,
    /* A delta file holds a square of columns this many on a side: half a kilometre of blocks. */
    SAVE_WORLD_REGION_SHIFT = 5u,
    SAVE_WORLD_REGION_SIDE = 1u << SAVE_WORLD_REGION_SHIFT,
    SAVE_WORLD_REGION_COLUMNS = SAVE_WORLD_REGION_SIDE * SAVE_WORLD_REGION_SIDE,
    /* The most delta files one world may have: past this a save fails and says so, and nothing already saved is touched. */
    SAVE_WORLD_REGIONS_MAX = 1u << 16,
    /* The largest section's bytes on disk: its 4 byte head and 4096 states. */
    SAVE_WORLD_RECORD_BYTES_MAX = 4u + 2u * SECTION_VOXELS,
    /* A section with flowing fluid in it adds its levels after that: half a byte per voxel. */
    SAVE_WORLD_RECORD_LEVELS_BYTES = SECTION_VOXELS / 2u,
    SAVE_WORLD_ENTRY_BYTES_MAX = SAVE_WORLD_RECORD_BYTES_MAX + SAVE_WORLD_RECORD_LEVELS_BYTES,
    /* What one set of captured sections may hold before it must be written. */
    SAVE_WORLD_PENDING_SECTIONS = 1u << 15,
    SAVE_WORLD_PENDING_HASH_SLOTS = SAVE_WORLD_PENDING_SECTIONS * 2u,
    /* Columns captured that the far field has not heard of yet. Past this it is told that it has lost count, and looks again at everything. */
    SAVE_WORLD_FAR_RING = 1u << 12,
};
enum : uint32_t {
    SAVE_WORLD_RECORD_HEAD_BYTES = 4u,
    /* In a record's first number, which otherwise says how many bits an index takes. */
    SAVE_WORLD_RECORD_HAS_LEVELS = 0x8000u,
    SAVE_WORLD_PALETTE_MAX = 256u,
};
static_assert(SAVE_WORLD_RECORD_BYTES_MAX == 8196u);

enum : uint64_t {
    SAVE_WORLD_PENDING_BYTES = 64ull << 20,
};

/* ---- The files: all little-endian, as written by the only machine this builds for ---------------------------------- */

/* At the start of <world>/blocks/manifest. After it, region_count save_world_region_t, sorted by region. */
typedef struct save_world_manifest_header_t {
    uint8_t magic[8];          /* 0: "MCWORLDM" */
    uint64_t seed;             /* 8: all a column needs when it has no delta */
    uint64_t generation;       /* 16: counts saves; 0 is the world as it was made */
    uint64_t table_hash;       /* 24: of the block table the states in the deltas mean */
    uint64_t regions_checksum; /* 32: of what follows this header */
    uint32_t version;          /* 40: SAVE_WORLD_VERSION */
    uint32_t header_bytes;     /* 44: sizeof this */
    uint32_t region_count;     /* 48 */
    uint32_t generator;        /* 52: TERRAIN_GENERATOR_VERSION of the land the deltas were cut from */
    uint64_t checksum;         /* 56: of the 56 bytes before this */
} save_world_manifest_header_t;
static_assert(sizeof(save_world_manifest_header_t) == 64);
static_assert(alignof(save_world_manifest_header_t) == 8);
static_assert(offsetof(save_world_manifest_header_t, magic) == 0);
static_assert(offsetof(save_world_manifest_header_t, seed) == 8);
static_assert(offsetof(save_world_manifest_header_t, generation) == 16);
static_assert(offsetof(save_world_manifest_header_t, table_hash) == 24);
static_assert(offsetof(save_world_manifest_header_t, regions_checksum) == 32);
static_assert(offsetof(save_world_manifest_header_t, version) == 40);
static_assert(offsetof(save_world_manifest_header_t, header_bytes) == 44);
static_assert(offsetof(save_world_manifest_header_t, region_count) == 48);
static_assert(offsetof(save_world_manifest_header_t, generator) == 52);
static_assert(offsetof(save_world_manifest_header_t, checksum) == 56);

/* Which delta file a region's sections are in now: <world>/blocks/r.<region_x>.<region_y>.<generation>. */
typedef struct save_world_region_t {
    int32_t region_x;       /* 0: a column's chunk_x >> SAVE_WORLD_REGION_SHIFT */
    int32_t region_y;       /* 4 */
    uint64_t generation;    /* 8: of the save that wrote the file */
    uint64_t file_bytes;    /* 16 */
    uint32_t section_count; /* 24 */
    uint32_t damaged;       /* 28: 0 on disk; in memory, what a save could not read of the file this replaces */
} save_world_region_t;
static_assert(sizeof(save_world_region_t) == 32);
static_assert(alignof(save_world_region_t) == 8);
static_assert(offsetof(save_world_region_t, region_x) == 0);
static_assert(offsetof(save_world_region_t, region_y) == 4);
static_assert(offsetof(save_world_region_t, generation) == 8);
static_assert(offsetof(save_world_region_t, file_bytes) == 16);
static_assert(offsetof(save_world_region_t, section_count) == 24);
static_assert(offsetof(save_world_region_t, damaged) == 28);

/* At the start of a delta file. After it, SAVE_WORLD_REGION_COLUMNS save_world_column_entry_t, then for each column with anything its
 * sections' records, in order, and then its save_world_section_entry_t. */
typedef struct save_world_delta_header_t {
    uint8_t magic[8];       /* 0: "MCWDELTA" */
    uint64_t seed;          /* 8 */
    uint64_t generation;    /* 16 */
    uint64_t file_bytes;    /* 24 */
    uint32_t version;       /* 32 */
    uint32_t header_bytes;  /* 36 */
    int32_t region_x;       /* 40 */
    int32_t region_y;       /* 44 */
    uint32_t section_count; /* 48 */
    uint8_t _pad0[4];       /* 52 */
    uint64_t checksum;      /* 56: of the 56 bytes before this */
} save_world_delta_header_t;
static_assert(sizeof(save_world_delta_header_t) == 64);
static_assert(alignof(save_world_delta_header_t) == 8);
static_assert(offsetof(save_world_delta_header_t, magic) == 0);
static_assert(offsetof(save_world_delta_header_t, seed) == 8);
static_assert(offsetof(save_world_delta_header_t, generation) == 16);
static_assert(offsetof(save_world_delta_header_t, file_bytes) == 24);
static_assert(offsetof(save_world_delta_header_t, version) == 32);
static_assert(offsetof(save_world_delta_header_t, header_bytes) == 36);
static_assert(offsetof(save_world_delta_header_t, region_x) == 40);
static_assert(offsetof(save_world_delta_header_t, region_y) == 44);
static_assert(offsetof(save_world_delta_header_t, section_count) == 48);
static_assert(offsetof(save_world_delta_header_t, _pad0) == 52);
static_assert(offsetof(save_world_delta_header_t, checksum) == 56);

/* One column in a delta file, at local y * SAVE_WORLD_REGION_SIDE + local x. All zeros for a column with nothing. */
typedef struct save_world_column_entry_t {
    column_mask_t section_mask; /* 0: one bit per section saved; the entries are for these, lowest first */
    uint64_t entries_offset;    /* 16 */
    uint64_t checksum;          /* 24: of the two fields above, then of the entries */
} save_world_column_entry_t;
static_assert(sizeof(save_world_column_entry_t) == 32);
static_assert(alignof(save_world_column_entry_t) == 8);
static_assert(offsetof(save_world_column_entry_t, section_mask) == 0);
static_assert(offsetof(save_world_column_entry_t, entries_offset) == 16);
static_assert(offsetof(save_world_column_entry_t, checksum) == 24);

/* Where one section's record is. */
typedef struct save_world_section_entry_t {
    uint64_t offset;   /* 0 */
    uint32_t bytes;    /* 8 */
    uint32_t checksum; /* 12: of the record */
} save_world_section_entry_t;
static_assert(sizeof(save_world_section_entry_t) == 16);
static_assert(alignof(save_world_section_entry_t) == 8);
static_assert(offsetof(save_world_section_entry_t, offset) == 0);
static_assert(offsetof(save_world_section_entry_t, bytes) == 8);
static_assert(offsetof(save_world_section_entry_t, checksum) == 12);

/* ---- In memory ------------------------------------------------------------------------------------------------------ */

/* One captured section, whose record is at offset in its set's bytes. */
typedef struct save_world_pending_ref_t {
    int32_t chunk_x;  /* 0 */
    int32_t chunk_y;  /* 4 */
    uint32_t offset;  /* 8 */
    uint16_t bytes;   /* 12 */
    uint8_t section;  /* 14 */
    uint8_t _pad0[1]; /* 15 */
} save_world_pending_ref_t;
static_assert(sizeof(save_world_pending_ref_t) == 16);
static_assert(alignof(save_world_pending_ref_t) == 4);
static_assert(offsetof(save_world_pending_ref_t, chunk_x) == 0);
static_assert(offsetof(save_world_pending_ref_t, chunk_y) == 4);
static_assert(offsetof(save_world_pending_ref_t, offset) == 8);
static_assert(offsetof(save_world_pending_ref_t, bytes) == 12);
static_assert(offsetof(save_world_pending_ref_t, section) == 14);
static_assert(offsetof(save_world_pending_ref_t, _pad0) == 15);

/* Sections captured from the world and not yet in a file. */
typedef struct save_world_pending_t {
    uint8_t *borrowed_bytes;                 /* 0: SAVE_WORLD_PENDING_BYTES */
    save_world_pending_ref_t *borrowed_refs; /* 8: SAVE_WORLD_PENDING_SECTIONS */
    uint32_t *borrowed_hash;                 /* 16: SAVE_WORLD_PENDING_HASH_SLOTS: a ref's index + 1, or 0 */
    uint64_t used;                           /* 24: of the bytes */
    uint32_t count;                          /* 32: of the refs */
    uint8_t _pad0[4];                        /* 36 */
} save_world_pending_t;
static_assert(sizeof(save_world_pending_t) == 40);
static_assert(alignof(save_world_pending_t) == 8);
static_assert(offsetof(save_world_pending_t, borrowed_bytes) == 0);
static_assert(offsetof(save_world_pending_t, borrowed_refs) == 8);
static_assert(offsetof(save_world_pending_t, borrowed_hash) == 16);
static_assert(offsetof(save_world_pending_t, used) == 24);
static_assert(offsetof(save_world_pending_t, count) == 32);
static_assert(offsetof(save_world_pending_t, _pad0) == 36);

/* One pending section's place in the order a save writes them. */
typedef struct save_world_sort_key_t {
    int32_t region_x; /* 0 */
    int32_t region_y; /* 4 */
    uint32_t place;   /* 8: local column * 64 + section */
    uint32_t ref;     /* 12 */
} save_world_sort_key_t;
static_assert(sizeof(save_world_sort_key_t) == 16);
static_assert(alignof(save_world_sort_key_t) == 4);
static_assert(offsetof(save_world_sort_key_t, region_x) == 0);
static_assert(offsetof(save_world_sort_key_t, region_y) == 4);
static_assert(offsetof(save_world_sort_key_t, place) == 8);
static_assert(offsetof(save_world_sort_key_t, ref) == 12);

typedef enum save_world_status_t : uint8_t {
    SAVE_WORLD_OK = 0,
    SAVE_WORLD_FULL = 1,          /* no room for what was captured: write what is held, and capture the rest after */
    SAVE_WORLD_ERROR_FILE = 2,    /* the disk said no: file_status has why. Nothing saved has been touched */
    SAVE_WORLD_ERROR_LOCKED = 3,  /* something else has this world open */
    SAVE_WORLD_ERROR_DAMAGED = 4, /* a file a world needs is wrong or missing. Nothing has been changed */
    SAVE_WORLD_ERROR_VERSION = 5, /* it is from another version of the format. Nothing has been changed */
    SAVE_WORLD_ERROR_SEED = 6,    /* the deltas were made on another world's terrain. Nothing has been changed */
    SAVE_WORLD_ERROR_TABLES = 7,  /* the deltas' states mean other blocks than this build's. Nothing has been changed */
    SAVE_WORLD_ERROR_TOO_MANY = 8, /* past SAVE_WORLD_REGIONS_MAX. Nothing saved has been touched */
    SAVE_WORLD_ERROR_PATH = 9,     /* the directory's name is too long */
    SAVE_WORLD_ERROR_GENERATOR =
        10, /* another generator made its land: its changes would be stitched to a different landscape. Nothing has been changed */
} save_world_status_t;

/* Where a save has got to, for a test to die at. In the order they happen. */
typedef enum save_world_step_t : uint8_t {
    SAVE_WORLD_STEP_DELTA_RECORDS = 0, /* a temporary delta file has its records, and no table or header */
    SAVE_WORLD_STEP_DELTA_SYNCED = 1,  /* it is whole and on the disk, under its temporary name */
    SAVE_WORLD_STEP_DELTA_RENAMED = 2, /* it has its own name, and nothing refers to it */
    SAVE_WORLD_STEP_DELTAS_DONE = 3,   /* every delta file is there, and the directory is on the disk */
    SAVE_WORLD_STEP_MANIFEST_DONE = 4, /* the new manifest has replaced the old: this is when the save has happened */
    SAVE_WORLD_STEP_REPLACED_GONE = 5, /* the files it replaced have been removed */
    SAVE_WORLD_STEP_COUNT = 6,
} save_world_step_t;

typedef void (*save_world_step_hook_t)(void *context, save_world_step_t step);

/* Which columns have been captured, from the sim to whoever calls render_lod_frame. In the arena and not in save_world_t, whose size is a part of main's frame. */
typedef struct save_world_far_t {
    _Atomic uint32_t head;                  /* 0: the sim's: release when a pair has been written */
    _Atomic uint32_t tail;                  /* 4: the far field's: release when a pair has been read */
    _Atomic uint32_t lost;                  /* 8: pairs that had no room */
    uint8_t _pad0[4];                       /* 12 */
    int32_t ring[2u * SAVE_WORLD_FAR_RING]; /* 16: chunk_x, chunk_y */
} save_world_far_t;
static_assert(sizeof(save_world_far_t) == 16u + 2u * SAVE_WORLD_FAR_RING * sizeof(int32_t));
static_assert(alignof(save_world_far_t) == 4);
static_assert(offsetof(save_world_far_t, head) == 0);
static_assert(offsetof(save_world_far_t, tail) == 4);
static_assert(offsetof(save_world_far_t, lost) == 8);
static_assert(offsetof(save_world_far_t, _pad0) == 12);
static_assert(offsetof(save_world_far_t, ring) == 16);

/* One world's delta files, and what is on its way to them. See the README for who may call what, when. */
typedef struct save_world_t {
    pthread_mutex_t mutex;                         /* 0: over pending and which manifest is current */
    save_world_pending_t pending[2];               /* 40 */
    save_world_region_t *borrowed_manifest[2];     /* 120: SAVE_WORLD_REGIONS_MAX; one is current */
    save_world_region_t *borrowed_written;         /* 136: SAVE_WORLD_PENDING_SECTIONS: what a save has made */
    save_world_region_t *borrowed_replaced;        /* 144: and what that replaces */
    save_world_sort_key_t *borrowed_keys;          /* 152: SAVE_WORLD_PENDING_SECTIONS */
    save_world_column_entry_t *borrowed_table_old; /* 160: SAVE_WORLD_REGION_COLUMNS */
    save_world_column_entry_t *borrowed_table_new; /* 168 */
    uint8_t *borrowed_manifest_bytes;              /* 176: the manifest as a file */
    uint8_t *borrowed_record;                      /* 184: SAVE_WORLD_ENTRY_BYTES_MAX: an old record to a new file */
    uint16_t *
        borrowed_states; /* 192: SECTION_VOXELS: a section being captured, then 65536: a state's place in a palette being built, + 1 */
    save_world_far_t *borrowed_far;    /* 200 */
    char *borrowed_directory;          /* 208: <world>/blocks */
    save_world_step_hook_t step_hook;  /* 216: nullptr but in a test */
    void *borrowed_step_context;       /* 224 */
    const world_tick_t *borrowed_tick; /* 232: where a section's levels are, or nullptr to keep none */
    uint64_t seed;                     /* 240 */
    uint64_t generation;               /* 248: of the current manifest */
    uint64_t table_hash;               /* 256 */
    _Atomic uint64_t sections_loaded;  /* 256: put over generated columns, ever */
    _Atomic uint64_t sections_damaged; /* 264: on disk and past reading, each time they were wanted */
    uint64_t sections_written;         /* 272: by saves */
    uint64_t saves_done;               /* 280: saves that wrote anything */
    file_t lock;                       /* 288: held open while this has the world */
    uint32_t region_count;             /* 296: in the current manifest */
    uint8_t file_status;               /* 300: file_status_t: what the disk said, for SAVE_WORLD_ERROR_FILE */
    uint8_t current;                   /* 301: which manifest is current */
    uint8_t active;                    /* 310: which of pending is captured to: the other is being written */
    uint8_t open;                      /* 311 */
} save_world_t;
static_assert(sizeof(save_world_t) == 312);
static_assert(alignof(save_world_t) == 8);
static_assert(offsetof(save_world_t, mutex) == 0);
static_assert(offsetof(save_world_t, pending) == 40);
static_assert(offsetof(save_world_t, borrowed_manifest) == 120);
static_assert(offsetof(save_world_t, borrowed_written) == 136);
static_assert(offsetof(save_world_t, borrowed_replaced) == 144);
static_assert(offsetof(save_world_t, borrowed_keys) == 152);
static_assert(offsetof(save_world_t, borrowed_table_old) == 160);
static_assert(offsetof(save_world_t, borrowed_table_new) == 168);
static_assert(offsetof(save_world_t, borrowed_manifest_bytes) == 176);
static_assert(offsetof(save_world_t, borrowed_record) == 184);
static_assert(offsetof(save_world_t, borrowed_states) == 192);
static_assert(offsetof(save_world_t, borrowed_far) == 200);
static_assert(offsetof(save_world_t, borrowed_directory) == 208);
static_assert(offsetof(save_world_t, step_hook) == 216);
static_assert(offsetof(save_world_t, borrowed_step_context) == 224);
static_assert(offsetof(save_world_t, borrowed_tick) == 232);
static_assert(offsetof(save_world_t, seed) == 240);
static_assert(offsetof(save_world_t, generation) == 248);
static_assert(offsetof(save_world_t, table_hash) == 256);
static_assert(offsetof(save_world_t, sections_loaded) == 264);
static_assert(offsetof(save_world_t, sections_damaged) == 272);
static_assert(offsetof(save_world_t, sections_written) == 280);
static_assert(offsetof(save_world_t, saves_done) == 288);
static_assert(offsetof(save_world_t, lock) == 296);
static_assert(offsetof(save_world_t, region_count) == 304);
static_assert(offsetof(save_world_t, file_status) == 308);
static_assert(offsetof(save_world_t, current) == 309);
static_assert(offsetof(save_world_t, active) == 310);
static_assert(offsetof(save_world_t, open) == 311);

/* What save_world_open takes from its arena. */
enum : uint64_t {
    SAVE_WORLD_ARENA_BYTES =
        2u * (SAVE_WORLD_PENDING_BYTES + SAVE_WORLD_PENDING_SECTIONS * sizeof(save_world_pending_ref_t) +
              SAVE_WORLD_PENDING_HASH_SLOTS * sizeof(uint32_t)) +
        2u * SAVE_WORLD_REGIONS_MAX * sizeof(save_world_region_t) +
        2u * SAVE_WORLD_PENDING_SECTIONS * sizeof(save_world_region_t) +
        SAVE_WORLD_PENDING_SECTIONS * sizeof(save_world_sort_key_t) +
        2u * SAVE_WORLD_REGION_COLUMNS * sizeof(save_world_column_entry_t) + sizeof(save_world_manifest_header_t) +
        SAVE_WORLD_REGIONS_MAX * sizeof(save_world_region_t) + SAVE_WORLD_ENTRY_BYTES_MAX +
        SECTION_VOXELS * sizeof(uint16_t) + 65536u * sizeof(uint16_t) + FILE_PATH_BYTES + sizeof(save_world_far_t) +
        32u * 64u,
    /* What a caller of save_world_column_overlay lends it. */
    SAVE_WORLD_OVERLAY_SCRATCH_BYTES = SAVE_WORLD_ENTRY_BYTES_MAX,
    /* What save_world_column_overlay_levels writes levels into. */
    SAVE_WORLD_OVERLAY_LEVELS_BYTES = (uint64_t)COLUMN_SECTIONS * SAVE_WORLD_RECORD_LEVELS_BYTES,
    /* What save_world_far_line works in: a place for what it last read, a whole column's states, and a record. */
    SAVE_WORLD_FAR_SCRATCH_BYTES =
        64u + (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS * sizeof(uint16_t) + SAVE_WORLD_ENTRY_BYTES_MAX,
};

/* A status as text for a log line. */
const char *save_world_status_name(save_world_status_t status);

/* What states this build's numbers mean: a hash of the block table's names, in order. */
uint64_t save_world_table_hash(void);

/* One section's record: SECTION_VOXELS states in section_voxel_index order, to out, which has room for
 * SAVE_WORLD_RECORD_BYTES_MAX. Returns its bytes. The same states always give the same bytes. palette_slots is 65536 entries, all 0,
 * and it leaves them so. */
uint32_t save_world_record_encode(const uint16_t *states, uint16_t *palette_slots, uint8_t *out);

/* And back. False, with out untouched, if these bytes are not exactly a record an encoder could mean. Trusts nothing. */
bool save_world_record_decode(const uint8_t *record, uint64_t bytes, uint16_t *out);

/* Whether a record is one save_world_record_decode would take, with its bits, palette count and palette, of SAVE_WORLD_PALETTE_MAX, if so. Writes nothing else. */
bool save_world_record_valid(const uint8_t *record, uint64_t bytes, uint32_t *out_bits, uint32_t *out_palette_count,
                             uint16_t *out_palette);

/* A record that holds levels ends in them. For one save_world_record_decode took: the levels, which it has checked, or nullptr if it has none. */
const uint8_t *save_world_record_levels(const uint8_t *record, uint64_t bytes);

/* Starts on <world_directory>/blocks, made if it is not there, for a world of this seed: a new world gets a manifest with no regions.
 * Takes the world's lock, removes what an interrupted save left, and takes from the arena, which must outlive this. Anything but
 * SAVE_WORLD_OK has logged why, holds nothing, and has written nothing over what was there. */
[[nodiscard]] save_world_status_t save_world_open(save_world_t *save, arena_t *arena, const char *world_directory,
                                                  uint64_t seed);

/* Gives up the lock and forgets it all. What was captured and not written is gone: save_world_flush first. Safe on one not open. */
void save_world_close(save_world_t *save);

/* Where captures find a section's levels, from now until this closes. Without it they keep blocks alone. */
void save_world_levels_from(save_world_t *save, const world_tick_t *tick);

/* A test's way in. */
void save_world_step_hook_set(save_world_t *save, save_world_step_hook_t hook, void *context);

/* ---- From the thread holding the map: the sim, or anyone once it has stopped ---------------------------------------- */

/* Every section dirty for saving, copied out, its flags taken. SAVE_WORLD_FULL if not all fitted: those stay dirty. */
[[nodiscard]] save_world_status_t save_world_capture_dirty(save_world_t *save, world_edit_t *edit);

/* The same for one column in the map, before it is unloaded. Unload it only on SAVE_WORLD_OK. */
[[nodiscard]] save_world_status_t save_world_capture_column(save_world_t *save, world_edit_t *edit, uint32_t slot);

/* Turns what has been captured into what the next save_world_commit_write writes, and starts a new set to capture to. False if there is
 * nothing to write. One that failed is still there and is written again first. Not while a write is running. */
[[nodiscard]] bool save_world_commit_begin(save_world_t *save);

/* ---- From any one thread, between a begin and the next ---------------------------------------------------------------- */

/* One save: new delta files for every region it touches, and then, in one rename, a manifest that names them. Killed at any moment the world
 * is left as the last save or as this one, whole. A failure has logged why, changed nothing that was saved, and keeps what it was given. */
[[nodiscard]] save_world_status_t save_world_commit_write(save_world_t *save);

/* ---- All at once, by the thread holding the map, with no write running --------------------------------------------- */

/* Captures and writes until nothing is unsaved. */
[[nodiscard]] save_world_status_t save_world_flush(save_world_t *save, world_edit_t *edit);

/* ---- From any thread, any time it is open ---------------------------------------------------------------------------- */

/* Writes this column's saved sections over states, COLUMN_SECTIONS * SECTION_VOXELS of them as generated, whether they are in a file or
 * only on the way there. For a column not in the map: a live one's edits are not here. Returns a mask of the sections it wrote. Those
 * it could not read keep what was generated, and it logs them, and a later edit of one replaces it. scratch is
 * SAVE_WORLD_OVERLAY_SCRATCH_BYTES of the caller's. Calls no heap. Does nothing on one not open. */
column_mask_t save_world_column_overlay(save_world_t *save, int32_t chunk_x, int32_t chunk_y, uint16_t *states,
                                        uint8_t *scratch);

/* The same, and the levels of each section it wrote that has any, at levels + section * SAVE_WORLD_RECORD_LEVELS_BYTES, out of
 * SAVE_WORLD_OVERLAY_LEVELS_BYTES, with a mask of those in *out_levels_mask. A voxel of fluid with no level saved is a source, so a save read without them
 * floods. */
column_mask_t save_world_column_overlay_levels(save_world_t *save, int32_t chunk_x, int32_t chunk_y, uint16_t *states,
                                               uint8_t *levels, column_mask_t *out_levels_mask, uint8_t *scratch);

/* For the far field, on a worker: what is saved of one line of blocks from the bottom of the world to the top, written over out, at the storage height of each.
 * scratch is SAVE_WORLD_FAR_SCRATCH_BYTES, 64 byte aligned and the first 64 zero, kept from one call to the next, as a line usually has neighbours in its
 * column. Those read are not counted as sections_loaded, which says what a game has loaded. */
void save_world_far_line(void *borrowed_save, int32_t x, int32_t y, uint16_t *out, uint8_t *scratch);

/* The next column whose sections have been captured since the far field last asked, oldest first, as soon as a reader can get them. One thread asks, and it is
 * not the sim. False if there is none. */
bool save_world_far_next(save_world_t *save, int32_t *out_chunk_x, int32_t *out_chunk_y);

/* Whether any have been lost for want of room since this was last asked. If so, it is all of the far field that may be out of date. */
bool save_world_far_lost_take(save_world_t *save);

#endif
