/* See run/src/ui/atlas/README.md. */
#ifndef MC_RUN_UI_ATLAS_INDEX_H
#define MC_RUN_UI_ATLAS_INDEX_H

#include "arena/index.h"
#include "climate/index.h"
#include "input/index.h"
#include "job/index.h"
#include "types/index.h"
#include "ui/atlas/draw/index.h"
#include "ui/atlas/layer/index.h"
#include "ui/atlas/tile/index.h"
#include "ui/compose/index.h"
#include "ui/font/index.h"
#include "ui/widget/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Plan 004 phase 09: the Atlas on M. What it knows of the ground (the store), the view, the keys, the drawing and saving
 * jobs, and each frame's fixed parts. */

enum : uint32_t {
    UI_ATLAS_SLOTS = 160u,              /* tiles kept in memory: 160 MiB */
    UI_ATLAS_STALE_MAX = 256u,          /* mips waiting to be made again; past this every cached mip is */
    UI_ATLAS_PICTURE_MAX_WIDTH = 3840u, /* a larger canvas does not open the Atlas, and says why */
    UI_ATLAS_PICTURE_MAX_HEIGHT = 2160u,
    UI_ATLAS_NOW_REDRAW_MS = 500u,      /* ms of world time between pictures of a now layer: twice a second, 9.3 */
    UI_ATLAS_GROUND_SAMPLE_MS = 500u,   /* ms between looks at the store's revision while streaming */
    UI_ATLAS_SAVE_INTERVAL_MS = 30000u, /* ms of world time between saves of dirty tiles, as save/autosave */
    UI_ATLAS_DAYS = 365u,
    UI_ATLAS_SLIDER_ID = 0xA7A5u,
    UI_ATLAS_READOUT_BYTES = 192u,
};

/* Where the region ui_atlas_world_open reserves goes; only pages touched cost memory. */
enum : uint64_t {
    UI_ATLAS_PICTURE_BYTES = (uint64_t)UI_ATLAS_PICTURE_MAX_WIDTH * UI_ATLAS_PICTURE_MAX_HEIGHT * 4u,
    UI_ATLAS_DRAW_ARENA_BYTES = 64ull << 20, /* the drawing job's: the systems' day list, and bands run inline */
    UI_ATLAS_SAVE_ARENA_BYTES = 8ull << 20,  /* the saving job's: a file read, an encode, a scratch tile */
    UI_ATLAS_BAND_ARENA_BYTES = 48ull << 20, /* bands drawn on main, with no job system: reset for each */
    UI_ATLAS_REGION_BYTES = (uint64_t)UI_ATLAS_SLOTS * UI_ATLAS_TILE_BYTES + 2u * UI_ATLAS_PICTURE_BYTES +
                            UI_ATLAS_PICTURE_BYTES /* the field */ + UI_ATLAS_PICTURE_BYTES / 4u * 2u /* two greys */ +
                            UI_ATLAS_OVERLAY_SCRATCH_BYTES + UI_ATLAS_DRAW_ARENA_BYTES + UI_ATLAS_SAVE_ARENA_BYTES +
                            UI_ATLAS_BAND_ARENA_BYTES + (4ull << 20),
};

typedef enum ui_atlas_slot_state_t : uint8_t {
    UI_ATLAS_SLOT_EMPTY = 0,
    UI_ATLAS_SLOT_LIVE = 1,
} ui_atlas_slot_state_t;

/* One tile in memory. Every field and every pixel only under the store's lock. */
typedef struct ui_atlas_slot_t {
    uint32_t *borrowed_pixels; /* 0: UI_ATLAS_TILE_PIXELS, from the atlas region */
    int32_t tile_x;            /* 8 */
    int32_t tile_y;            /* 12 */
    uint64_t last_used;        /* 16: the store's use_clock when last read or written */
    uint64_t revision;         /* 24: the store's revision at its last write */
    uint8_t level;             /* 32: 0 to UI_ATLAS_LEVELS - 1 */
    uint8_t state;             /* 33: ui_atlas_slot_state_t */
    uint8_t dirty;             /* 34: changed since its file was written */
    uint8_t merge; /* 35: made before its file was read: read it, keep what is generated here, the rest from it */
    uint8_t parents_marked; /* 36: its parents are in the stale list since its last write's save */
    uint8_t _pad0[3];       /* 37 */
} ui_atlas_slot_t;
static_assert(sizeof(ui_atlas_slot_t) == 40);

/* A mip that must be made again from its children. */
typedef struct ui_atlas_tile_key_t {
    int32_t tile_x;   /* 0 */
    int32_t tile_y;   /* 4 */
    uint8_t level;    /* 8 */
    uint8_t _pad0[3]; /* 9 */
} ui_atlas_tile_key_t;
static_assert(sizeof(ui_atlas_tile_key_t) == 12);

/* What the Atlas knows of the ground: written by the sim, read by the drawing job, saved by the saving job. */
typedef struct ui_atlas_store_t {
    /* 0: a ticket lock over everything below and every slot's pixels: first come, first served, so 16 band jobs reading
     * cannot starve the sim's column writes, as a pthread mutex let them (Issues found in the phase 09 file). */
    _Atomic uint32_t ticket_next;
    _Atomic uint32_t ticket_serving;     /* 4 */
    uint8_t _pad1[32];                   /* 8 */
    ui_atlas_slot_t *borrowed_slots;     /* 40: UI_ATLAS_SLOTS */
    ui_atlas_tile_key_t *borrowed_stale; /* 48: UI_ATLAS_STALE_MAX */
    const uint16_t *borrowed_colours;    /* 56: RGB565 by block id, from ui_t's tables */
    char *borrowed_directory;            /* 64: <world>/atlas, or nullptr: nothing is kept on disk */
    uint64_t seed;                       /* 72: the world's; a file of another seed is not read */
    uint64_t revision;                   /* 80: every column written */
    uint64_t use_clock;                  /* 88 */
    uint64_t columns_lost;               /* 96: writes dropped with every slot dirty; logged once */
    uint64_t wait_ns_most;               /* 104: the longest ui_atlas_column_write waited for the lock */
    uint64_t files_refused;              /* 112: files ignored as damaged or foreign */
    uint32_t stale_count;                /* 120 */
    uint8_t stale_overflow;              /* 124: the list filled: every cached mip is stale */
    uint8_t save_failed_said;            /* 125 */
    uint8_t lost_said;                   /* 126 */
    uint8_t refused_said;                /* 127 */
} ui_atlas_store_t;
static_assert(sizeof(ui_atlas_store_t) == 128);

/* What main tells the Atlas of the world each frame. */
typedef struct ui_atlas_world_t {
    float64_t player[2];  /* 0: block x and y */
    float64_t t;          /* 16: the weather systems' time, D + f */
    int64_t world_ns;     /* 24: world time: what now layers and the ground's sampling count */
    uint64_t census_week; /* 32: the last census's week */
    float32_t yaw;        /* 40: radians, as the camera's: 0 looks east, a positive one turns toward north */
    uint16_t day_of_year; /* 44: today's, 0 to 364 */
    uint8_t _pad0[2];     /* 46 */
} ui_atlas_world_t;
static_assert(sizeof(ui_atlas_world_t) == 48);

/* The main thread's, but for what each field says. */
typedef struct ui_atlas_t {
    job_group_t draw_group;               /* 0: the drawing job and its bands */
    job_group_t save_group;               /* 64: the saving job */
    ui_atlas_store_t store;               /* 128 */
    ui_atlas_draw_input_t input;          /* 256: what the job under way draws; the job's until draw_done */
    ui_atlas_target_t target;             /* 384: the back picture's, while drawing */
    ui_atlas_view_t shown;                /* 416: what the front picture was drawn from */
    ui_atlas_view_t wanted;               /* 472: what main wants now */
    uint32_t *borrowed_pictures[2];       /* 528: front (shown) and back (being drawn) */
    uint8_t *borrowed_greys[2];           /* 544: the same, their grey masks */
    float32_t *borrowed_field;            /* 560 */
    uint8_t *borrowed_overlay_scratch;    /* 568 */
    job_system_t *borrowed_job;           /* 576 */
    const climate_t *borrowed_climate;    /* 584 */
    const ui_font_t *borrowed_font;       /* 592 */
    uint8_t _pad1[40];                    /* 600 */
    arena_t arena_draw;                   /* 640: the drawing job's; reset at each picture */
    arena_t arena_save;                   /* 704: the saving job's; reset at each save */
    _Atomic uint32_t draw_done;           /* 768: worker to main, release; main reads with acquire */
    _Atomic uint32_t save_done;           /* 772: the same, for the saving job */
    _Atomic uint32_t bands_left;          /* 776: the drawing job's bands not yet done; the last one finishes */
    uint32_t front;                       /* 780: which picture is shown */
    int64_t ground_sampled_ns;            /* 784: main's: when ground_revision was last taken */
    int64_t save_due_ns;                  /* 792: the sim's: world time of the next save */
    int64_t now_ns;                       /* 800: main's: the world time of the last frame */
    uint64_t pictures_drawn;              /* 808: for tests: pictures finished */
    uint64_t ground_revision;             /* 816: main's sample of the store's revision */
    float64_t drag_center[2];             /* 824: the centre when the button went down */
    int32_t drag_from[2];                 /* 840: the pointer then */
    int32_t readout_block[2];             /* 848: the block the readout is of */
    uint8_t up;                           /* 856: on the screen */
    uint8_t drawing_busy;                 /* 857: a drawing job is out */
    uint8_t saving_busy;                  /* 858: a saving job is out */
    uint8_t dragging;                     /* 859 */
    uint8_t has_shown;                    /* 860: the front picture holds something */
    uint8_t opened_once;                  /* 861: this world has opened it, so the view is remembered */
    uint8_t open;                         /* 862: a world's store is open */
    uint8_t too_big;                      /* 863: the canvas is past UI_ATLAS_PICTURE_MAX_*: said instead */
    uint8_t readout_valid;                /* 864 */
    uint8_t readout_view_layer;           /* 865 */
    uint16_t readout_view_day;            /* 866 */
    char readout[UI_ATLAS_READOUT_BYTES]; /* 868 */
    uint8_t _pad0[28];                    /* 1060 */
    arena_t arena_band;                   /* 1088: bands drawn here with no job system; reset for each */
} ui_atlas_t;
static_assert(sizeof(ui_atlas_t) == 1152);
static_assert(alignof(ui_atlas_t) == 64);
static_assert(offsetof(ui_atlas_t, store) == 128);
static_assert(offsetof(ui_atlas_t, input) == 256);
static_assert(offsetof(ui_atlas_t, arena_draw) == 640);
static_assert(offsetof(ui_atlas_t, draw_done) == 768);

/* ---- The store: any thread, under its lock ---- */

/* Starts the store over region (UI_ATLAS_REGION_BYTES, from the Owner) for a world of this seed. directory (<world>/atlas,
 * made if missing) may be nullptr: nothing is kept on disk. colours is RGB565 by block id, BLOCK_COUNT long. False, having
 * logged why, if the region is too small. */
[[nodiscard]] bool ui_atlas_store_start(ui_atlas_t *atlas, uint8_t *region, uint64_t bytes, const char *directory,
                                        uint64_t seed, const uint16_t *colours, job_system_t *system,
                                        const climate_t *climate, const ui_font_t *font);
/* Waits for both jobs, writes everything left on this thread, and forgets the world. Safe on a closed atlas. */
void ui_atlas_store_stop(ui_atlas_t *atlas);

/* The sim: a finished column's 256 pixels (row 0 north) into its level-0 tile. About 2 microseconds under the lock. A null
 * atlas, or one with no world, does nothing. */
void ui_atlas_column_write(ui_atlas_t *atlas, int32_t chunk_x, int32_t chunk_y, const uint32_t *pixels);

/* The game's ui_atlas_ground_t.read: a rectangle of a level's pixels, made fresh where stale, read from files where not in
 * memory. borrowed_context is the ui_atlas_t. scratch lends a file's bytes. */
void ui_atlas_store_read(void *borrowed_context, uint32_t level, int64_t px, int64_t py, uint32_t width,
                         uint32_t height, uint32_t *out, arena_t *scratch);

/* The sim, once a step: every 30 s of world time, if no saving job is out, submits one. */
void ui_atlas_save_step(ui_atlas_t *atlas, int64_t world_ns);
/* The saving job's work, on this thread: every dirty tile written, then every stale mip made and written. */
void ui_atlas_save_now(ui_atlas_t *atlas, arena_t *scratch);
/* Every stale mip up to level made again from its children, on this thread. */
void ui_atlas_mips_make(ui_atlas_t *atlas, uint32_t level, arena_t *scratch);

/* ---- On screen: main ---- */

/* One poll's input. True if the Atlas is up after it, which then has the input: the body stands. M opens it when nothing
 * else is up (screen_up says whether a game screen is). */
bool ui_atlas_input(ui_atlas_t *atlas, ui_widget_t *widget, const input_frame_t *frame, uint32_t width, uint32_t height,
                    const ui_atlas_world_t *world, bool screen_up);
/* Whether it is up. */
bool ui_atlas_up(const ui_atlas_t *atlas);
/* Takes it down, the view kept (the census screen does this). Waits for a drawing job. */
void ui_atlas_hide(ui_atlas_t *atlas);
/* Main, each frame it is up: a finished picture swapped in, the view worked out, a new picture asked for if it differs. */
void ui_atlas_frame(ui_atlas_t *atlas, uint32_t width, uint32_t height, const ui_atlas_world_t *world);
/* Main, between a ui_frame_begin and ui_frame_end: the picture, title, legend, day slider, readout and player mark. */
void ui_atlas_frame_draw(ui_atlas_t *atlas, ui_widget_t *widget, ui_canvas_t *canvas, const ui_font_t *font,
                         const ui_atlas_world_t *world);
/* Waits for a drawing job, for tests and before anything the job reads goes. */
void ui_atlas_draw_wait(ui_atlas_t *atlas);

/* The readout of the block at (x, y), as the frame shows it, for the layer's time mode (day for normals). Into out. */
void ui_atlas_readout(const ui_atlas_t *atlas, int64_t x, int64_t y, uint32_t layer, uint16_t day, char *out,
                      uint64_t bytes);

/* One picture: the bands, then the finish. With system, from a thread that is not a worker: the bands as jobs in group,
 * each borrowing from its worker's job arena, waited for, then the finish here. Without, the bands one after another on
 * this thread, each borrowing scratch afresh. The bytes are the same either way, and for any number of bands. */
void ui_atlas_draw_picture(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target, job_system_t *system,
                           job_group_t *group, uint32_t bands, arena_t *scratch);

/* For tests: the layer's legend scale, its own or its climate layer's. False for none. */
bool ui_atlas_layer_scale(uint32_t layer, ui_atlas_scale_t *out);

#endif
