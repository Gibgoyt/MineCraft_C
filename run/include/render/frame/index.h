/* See run/src/render/frame/README.md. */
#ifndef MC_RUN_RENDER_FRAME_INDEX_H
#define MC_RUN_RENDER_FRAME_INDEX_H

#include "arena/index.h"
#include "display/flip/index.h"
#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/frame/index.h"
#include "gpu_bridge/world_buffers/index.h"
#include "machine/index.h"
#include "texture/index.h"
#include "types/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* One being read, one newest and waiting, one being written, so neither side ever waits for the other. */
    RENDER_EXCHANGE_SLOTS = 3u,
    /* Frames whose time is kept for the percentiles. Past this the oldest go. */
    RENDER_FRAME_SAMPLES = 1u << 16,
};

/* A triple buffer's numbers, from one writing thread to one reading thread, with no locks and no syscalls: the same idea as
 * world_edit_snapshot_t. Whoever uses it holds the three slots. Each side has its own cache line. */
typedef struct render_exchange_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) uint32_t back; /* 0: the writer's */
    uint8_t _pad0[MC_CPU_CACHE_LINE_BYTES - 4u];
    _Atomic uint32_t exchange; /* 64: a slot's number, and RENDER_EXCHANGE_FRESH if it is waiting unread */
    uint8_t _pad1[MC_CPU_CACHE_LINE_BYTES - 4u];
    uint32_t front; /* 128: the reader's */
    uint8_t _pad2[MC_CPU_CACHE_LINE_BYTES - 4u];
} render_exchange_t;
static_assert(sizeof(render_exchange_t) == 3u * MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(render_exchange_t, exchange) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(render_exchange_t, front) == 2u * MC_CPU_CACHE_LINE_BYTES);

/* Call before either thread starts. */
void render_exchange_start(render_exchange_t *exchange);

/* The writer: the slot just filled becomes the newest, and this is the next to fill. A slot the reader never saw is reused. */
uint32_t render_exchange_publish(render_exchange_t *exchange);

/* The reader: the newest slot not yet read, or false if there is none. Its slot stays whole until this next returns true. */
bool render_exchange_take(render_exchange_t *exchange, uint32_t *out_slot);

/* Whether the slot last published has not been taken yet. Either thread. */
bool render_exchange_waiting(const render_exchange_t *exchange);

/* Where the eye is and how it looks. x is east, y north and z up, a yaw of 0 looks east and a positive one turns towards north,
 * and a positive pitch looks up. In float64_t, so that it stays exact as far out as the world goes. */
typedef struct render_camera_t {
    float64_t position[3];      /* 0 */
    float32_t yaw;              /* 24: radians */
    float32_t pitch;            /* 28: radians */
    float32_t half_fov_tangent; /* 32: of half the vertical field of view */
    float32_t speed;            /* 36: blocks per second */
} render_camera_t;
static_assert(sizeof(render_camera_t) == 40);

/* What moves a free-fly camera for one step, wherever it came from: a script, or real keys and a real mouse. */
typedef struct render_fly_input_t {
    int64_t mouse_dx; /* 0: counts, right is positive */
    int64_t mouse_dy; /* 8: counts, towards the player is positive */
    int64_t wheel;    /* 16: steps, away from the player is positive: each makes the camera faster */
    int8_t forward;   /* 24: 1 forward, -1 back */
    int8_t right;     /* 25 */
    int8_t up;        /* 26 */
    uint8_t fast;     /* 27: go at RENDER_CAMERA_FAST_FACTOR times the speed */
    uint8_t _pad0[4]; /* 28 */
} render_fly_input_t;
static_assert(sizeof(render_fly_input_t) == 32);

#define RENDER_CAMERA_FAST_FACTOR 5.0f32
#define RENDER_CAMERA_SPEED_DEFAULT 20.0f32
#define RENDER_CAMERA_SPEED_STEP 1.25f32
#define RENDER_CAMERA_SPEED_MAX 2000.0f32
#define RENDER_CAMERA_SPEED_MIN 0.5f32
#define RENDER_CAMERA_RADIANS_PER_COUNT 0.0025f32
/* Looking straight up or down leaves no sideways. */
#define RENDER_CAMERA_PITCH_LIMIT 1.553f32
/* The tangent of half of a vertical field of view of 70 degrees. */
#define RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT 0.7002f32

#define RENDER_DAY_NOON 0.5f32

void render_camera_start(render_camera_t *camera, float64_t x, float64_t y, float64_t z, float32_t yaw,
                         float32_t pitch);

/* One step of flying: looks first, then moves along where it now looks, level, and up and down along z. */
void render_camera_fly(render_camera_t *camera, const render_fly_input_t *input, float32_t seconds);

/* What the kernels take, for a picture of this size. */
void render_camera_to_traverse(const render_camera_t *camera, uint32_t width, uint32_t height, traverse_camera_t *out);

/* What the frames took, on the card's clock, since render_frame_init. */
typedef struct render_frame_stats_t {
    uint64_t frames;                    /* 0 */
    uint64_t readback_bytes;            /* 8 */
    float64_t sum_ms[FRAME_PASS_COUNT]; /* 16 */
    float64_t sum_total_ms;             /* 56 */
    float32_t max_ms[FRAME_PASS_COUNT]; /* 64 */
    float32_t max_total_ms;             /* 84 */
} render_frame_stats_t;
static_assert(sizeof(render_frame_stats_t) == 88);

/* The device blocks a frame needs, which the Owner holds. */
typedef struct render_frame_blocks_t {
    const device_block_t *borrowed_textures;    /* 0: render_frame_texture_table_bytes */
    const device_block_t *borrowed_faces;       /* 8: render_frame_face_table_bytes */
    const device_block_t *borrowed_pixels;      /* 16: all of texture_pixels_get */
    const device_block_t *borrowed_coarse;      /* 24 */
    const device_block_t *borrowed_hits;        /* 32 */
    const device_block_t *borrowed_shaded;      /* 40 */
    const device_block_t *borrowed_xrgb;        /* 48 */
    const device_block_t *borrowed_shapes;      /* 56: render_frame_shape_table_bytes */
    const device_block_t *borrowed_ao[2];       /* 64: shade_ao_bytes each: one is the last frame's, one the next's */
    const device_block_t *borrowed_history[2];  /* 80: post_history_bytes each, the same way */
    const device_block_t *borrowed_shafts;      /* 96: post_shafts_bytes */
    const device_block_t *borrowed_xrgb_second; /* 104: the same size as borrowed_xrgb: frames write the two in turn */
} render_frame_blocks_t;
static_assert(sizeof(render_frame_blocks_t) == 112);

typedef struct render_entities_t render_entities_t;
typedef struct render_lod_t render_lod_t;

/* One picture's way from the card to the screen. The gpu thread draws, and the main thread takes and presents. */
typedef struct render_frame_t {
    render_exchange_t pictures;                        /* 0 */
    frame_gpu_t gpu;                                   /* 192 */
    frame_targets_t targets;                           /* 312 */
    shade_scene_t scene;                               /* 344 */
    post_settings_t post;                              /* 448 */
    render_frame_stats_t stats;                        /* 560 */
    device_memory_t *borrowed_memory;                  /* 648 */
    uint8_t *borrowed_pictures[RENDER_EXCHANGE_SLOTS]; /* 656: width * height * 4 each */
    float32_t *borrowed_samples;                       /* 680: RENDER_FRAME_SAMPLES of total_ms */
    const device_block_t *borrowed_xrgb_second; /* 688: plan 008 phase 03: the other device picture, written in turn */
    uint32_t width;                             /* 696 */
    uint32_t height;                            /* 700 */
    const device_block_t *borrowed_shapes;      /* 704: a traverse_shape_t per block */
    post_temporal_t temporal;                   /* 712: what scene.borrowed_temporal points to */
    float32_t fog_end;                          /* 848: in blocks, as it was asked for */
    float32_t day_fraction;                     /* 852: what scene.sun and post were last worked out for */
    uint32_t drawn_features;                    /* 856: what the last frame was drawn with */
    uint8_t pinned;                             /* 860: how many of the pictures are pinned */
    uint8_t eyes_under_water;                   /* 861: as last told, by render_frame_under_water_set */
    uint8_t on_way;                             /* 862: frames queued and not finished: 0, 1 or 2 */
    uint8_t oldest_slot;                        /* 863: the flight slot of the first of them, whose picture goes home */
    render_entities_t *borrowed_entities;       /* 864: what each frame draws of them, from the Owner, or nullptr */
    render_lod_t *borrowed_lod;   /* 872: what each frame draws past level 0, from the Owner, or nullptr */
    frame_timings_t last_timings; /* 880: what the last frame took, kernel by kernel, on the card's clock */
    float32_t sun_given[3];       /* 944: with sun_from_outside: toward the sun, unit, x east, y north */
    uint8_t sun_from_outside;     /* 956: 1 after render_frame_sun_set, 0 after render_frame_time_set */
    uint8_t _pad1[3];             /* 957 */
} render_frame_t;
static_assert(sizeof(render_frame_t) == 960);
static_assert(alignof(render_frame_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(render_frame_t, gpu) == 192);
static_assert(offsetof(render_frame_t, borrowed_memory) == 648);
static_assert(offsetof(render_frame_t, temporal) == 712);
static_assert(offsetof(render_frame_t, pinned) == 860);
static_assert(offsetof(render_frame_t, on_way) == 862);
static_assert(offsetof(render_frame_t, borrowed_entities) == 864);
static_assert(offsetof(render_frame_t, borrowed_lod) == 872);
static_assert(offsetof(render_frame_t, sun_given) == 944);
static_assert(offsetof(render_frame_t, sun_from_outside) == 956);

/* ---- Weather (plan 004 phase 08) -------------------------------------------------------------------------------------- */

/* How cloud, fog and rain change the sky, the light and how far is seen. Numbers from the phase 08 file's tables. */
#define RENDER_CLOUD_SKY_GREY 0.8f32       /* share of the way to grey at cloud 1 */
#define RENDER_CLOUD_DIRECT_CUT 0.75f32    /* of direct sunlight, at cloud 1 */
#define RENDER_CLOUD_AMBIENT_CUT 0.3f32    /* of ambient light */
#define RENDER_CLOUD_DISC_CUT 0.9f32       /* of the sun's and moon's discs; the glow goes by all of cloud */
#define RENDER_FOG_WEATHER_CUT 0.85f32     /* of visibility, at fog or dust 1 */
#define RENDER_FOG_RAIN_MM_H 10.0f32       /* mm an hour of rain that halves visibility */
#define RENDER_FOG_VISIBILITY_MIN 32.0f32  /* blocks: never thicker than this */
#define RENDER_FOG_WEATHER_START 0.3f32    /* fog starts at this share of visibility when weather shortens it */
#define RENDER_FLASH_DISTANCE 500.0f32     /* blocks away where a flash is half as bright */
#define RENDER_WEATHER_EASE_SECONDS 5.0f32 /* how fast density and cloud follow a change */
#define RENDER_WEATHER_BOLT_SECONDS 0.2f32 /* how long a bolt is seen: full, half, full */
#define RENDER_WEATHER_TIME_AHEAD 0.05f32  /* seconds a frame may be past its slot's tick, for the fall between ticks */
enum : uint64_t {
    RENDER_WEATHER_WRAP_TICKS = 72000u, /* an hour: the fall's time wraps, as a float keeps it smooth that long */
};

/* What the sim sends of the weather where the eye is, once a tick, for the thread that draws: a slot of a render_exchange_t. */
typedef struct render_weather_slot_t {
    uint64_t world_ticks;                       /* 0: the tick this was made at */
    int64_t tick_ns;                            /* 8: when, on the sim's clock; 0 with no threads */
    float32_t rain_mm_h;                        /* 16: rain and snow as water, at the eye */
    float32_t snow_share;                       /* 20: 0 to 1 */
    float32_t wind[2];                          /* 24: blocks a second, x east and y north */
    float32_t cloud;                            /* 32: 0 clear to 1 overcast */
    float32_t fog;                              /* 36: 0 to 1 */
    float32_t dust;                             /* 40: 0 to 1 */
    float32_t flash;                            /* 44: f at the strike's distance, 0 to 1, or 0 for none */
    float32_t bolt_age;                         /* 48: seconds since the strike at world_ticks, or -1 for none */
    float32_t bolt_distance;                    /* 52: blocks from the eye to the strike */
    float32_t bolt[POST_BOLT_SEGMENTS + 1u][3]; /* 56: joints from the cloud down, in blocks from bolt_origin */
    int32_t bolt_origin[3];                     /* 212: the strike, a whole block */
    int32_t map_origin[2];                      /* 224: the rain top map's lowest corner, whole blocks */
    uint32_t map_revision;                      /* 232: goes up whenever the map changes */
    uint8_t forced;                             /* 236: --weather: numbers given, so pictures follow them at once */
    uint8_t sent;                               /* 237: 0 for no weather at all: the picture is a clear day's */
    uint8_t _pad0[2];                           /* 238 */
    int16_t map[POST_WEATHER_MAP_SIDE * POST_WEATHER_MAP_SIDE]; /* 240: rain top, z from sea level; y * side + x */
} render_weather_slot_t;
static_assert(sizeof(render_weather_slot_t) == 8432);
static_assert(alignof(render_weather_slot_t) == 8);
static_assert(offsetof(render_weather_slot_t, rain_mm_h) == 16);
static_assert(offsetof(render_weather_slot_t, bolt) == 56);
static_assert(offsetof(render_weather_slot_t, bolt_origin) == 212);
static_assert(offsetof(render_weather_slot_t, map_origin) == 224);
static_assert(offsetof(render_weather_slot_t, map_revision) == 232);
static_assert(offsetof(render_weather_slot_t, map) == 240);

/* The weather as the thread that draws sees it: the slot's numbers, eased so that weather never pops, and what the rain top block on
 * the card holds. Zero bytes are no weather. One thread: the one that draws. */
typedef struct render_weather_t {
    int64_t tick_ns;                            /* 0: the last slot's */
    uint64_t world_ticks;                       /* 8: the last slot's */
    float32_t density;                          /* 16: eased share of cells with a drop */
    float32_t cloud;                            /* 20: eased */
    float32_t fog;                              /* 24: eased */
    float32_t dust;                             /* 28: eased */
    float32_t rain_mm_h;                        /* 32: eased, for visibility */
    float32_t snow_share;                       /* 36: as sent */
    float32_t wind[2];                          /* 40: as sent */
    float32_t flash;                            /* 48: as sent */
    float32_t bolt_age;                         /* 52: as sent, or -1 */
    float32_t bolt_distance;                    /* 56 */
    float32_t time_s;                           /* 60: the fall's clock for the next frame */
    float32_t bolt[POST_BOLT_SEGMENTS + 1u][3]; /* 64: from bolt_origin */
    int32_t bolt_origin[3];                     /* 220 */
    int32_t map_origin[2];                      /* 232 */
    uint32_t map_revision;                      /* 240: what the card's block holds, if map_sent */
    float32_t age_now;                          /* 244: bolt_age for the next frame */
    uint8_t active;                             /* 248: a slot with weather has come: weather is drawn */
    uint8_t map_sent;                           /* 249 */
    uint8_t forced;                             /* 250 */
    uint8_t _pad0[5];                           /* 251 */
} render_weather_t;
static_assert(sizeof(render_weather_t) == 256);
static_assert(alignof(render_weather_t) == 8);

/* Sizes, for the Owner to take blocks and memory. */
uint64_t render_frame_texture_table_bytes(void);
uint64_t render_frame_face_table_bytes(void);
uint64_t render_frame_shape_table_bytes(void);
uint64_t render_frame_arena_bytes(uint32_t width, uint32_t height, uint64_t staging_bytes);

/* Builds the tables from the block table and the texture blob and sends them and every texture's pixels to the card, takes the
 * pictures from the arena, and pins them. fog_end is in blocks. staging_bytes is each of the two areas the per-frame copies to the card go
 * through (frame.gpu.staging; 0 for copies that wait), from the arena too. All of it must outlive this. On failure logs why, leaves
 * nothing pinned, and returns what went wrong. */
gpu_status_t render_frame_init(render_frame_t *frame, arena_t *arena, device_memory_t *memory, const texture_t *texture,
                               const render_frame_blocks_t *blocks, uint32_t width, uint32_t height, float32_t fog_end,
                               uint64_t staging_bytes);

/* Unpins, and forgets the rest. Safe on zero bytes. No kernel may be running. */
void render_frame_deinit(render_frame_t *frame);

/* Which effects the next frames draw with: SHADE_FEATURE_*, SHADE_FEATURES_ALL from render_frame_init. Between frames: a draw reads
 * this on the gpu thread. An effect with a history starts it again when it is turned on. */
void render_frame_features_set(render_frame_t *frame, uint32_t features);

/* Plan 008 phase 04, Q2's choice B for comparison: a pixel newly seen sends one AO ray, not SHADE_AO_FRESH_RAYS (bench_world_shake
 * --fresh-ao-rays 1). Kept across render_frame_features_set. Between frames. */
void render_frame_ao_fresh_one_set(render_frame_t *frame, bool one);

/* How far off the fog is whole, in blocks, for a render distance that has changed. Between frames, as for render_frame_features_set. */
void render_frame_fog_set(render_frame_t *frame, float32_t fog_end);

/* The time of day: 0 is midnight, 0.25 sunrise in the east (+x), 0.5 noon and 0.75 sunset in the west, and anything else wraps.
 * Noon from render_frame_init. Between frames, on the thread that draws. With SHADE_FEATURE_DAY_CYCLE off this still moves the sun:
 * it is whoever calls this that keeps time, and the setting tells it not to. */
void render_frame_time_set(render_frame_t *frame, float32_t day_fraction);

/* The sun given from outside, in place of a time of day: a unit vector toward it, x east, y north, z up, as the game works it out
 * from the world clock and the eye's latitude (plan 004 phase 02). direction[2], the sine of its elevation, decides the colours of
 * day, twilight and night. Between frames, on the thread that draws. render_frame_time_set goes back to the fixed path. */
void render_frame_sun_set(render_frame_t *frame, const float32_t direction[3]);

/* Whether the eyes are in water, for the next frames. Between frames, on the thread that draws. Nothing shows of it with SHADE_FEATURE_UNDER_WATER off. */
void render_frame_under_water_set(render_frame_t *frame, bool under_water);

/* What the sky and the light are at a time of day: where the sun and moon are, and all their colours. Touches nothing else, so a
 * test can ask without a card. Leaves post's fog alone. */
void render_day_fill(float32_t day_fraction, post_settings_t *post, shade_sun_t *sun);

/* The same for a sun given as a direction, a unit vector toward it, and a height that decides the colours of day, twilight and
 * night: render_day_fill is its fixed path through this. Touches nothing else. */
void render_sky_fill(const float32_t sun_direction[3], float32_t height, post_settings_t *post, shade_sun_t *sun);

/* The gpu thread. Draws one whole frame of what buffers hold now, from this camera, and makes it the newest picture. Waits for
 * the card. Allocates nothing. Anything but GPU_STATUS_OK means nothing more should be asked of the card, and gpu.last_error
 * says why. */
gpu_status_t render_frame_draw(render_frame_t *frame, const world_buffers_t *buffers, const render_camera_t *camera);

/* render_frame_draw with the weather (nullptr or one not active for none): with SHADE_FEATURE_WEATHER on, the sky, the light and the
 * fog of this frame follow it, and rain, snow and the bolt are drawn, with map_block as the rain top. The frame's own sky and fog
 * settings are left as they were, so the next frame starts from the clear day again. */
gpu_status_t render_frame_draw_weather(render_frame_t *frame, const world_buffers_t *buffers,
                                       const render_camera_t *camera, const render_weather_t *weather,
                                       const device_block_t *map_block);

/* Plan 008 phase 03: render_frame_draw_weather in two halves, so the gpu thread can build the next frame while the card draws this one.
 * render_frame_queue queues a frame on the card and returns at once: what it reads of buffers, the camera and the weather is read now, and
 * the temporal state moves on now, as it is about the order of frames. render_frame_finish waits for the oldest frame queued, adds it to
 * the books and makes it the newest picture. At most two are queued at once: a third queue fails. A picture's copy home starts when its
 * slot of the exchange is free, which is at once for the first and at the last one's finish for the second, so the exchange keeps its three
 * slots. Anything but GPU_STATUS_OK means nothing more should be asked of the card, and gpu.last_error says why: a kernel that faulted is
 * reported by the finish of its frame or the next. */
gpu_status_t render_frame_queue(render_frame_t *frame, const world_buffers_t *buffers, const render_camera_t *camera,
                                const render_weather_t *weather, const device_block_t *map_block);
gpu_status_t render_frame_finish(render_frame_t *frame);
/* Whether render_frame_finish would hand the oldest frame over without waiting. False with nothing on its way. */
bool render_frame_oldest_done(render_frame_t *frame);
/* Whether the oldest frame's picture is whole on the card, its copy home all that is left of it. False with nothing on its way. */
bool render_frame_oldest_whole(render_frame_t *frame);

/* The thread that draws, before a frame: a new slot from the sim (nullptr if none came), eased toward over the ticks between, at once
 * for the first or a forced one; and the clock of the fall and the bolt for a frame at now_ns (0 for a run with no threads, which
 * draws each slot as it is). Sends the slot's rain top to map_block if it changed. Anything but GPU_STATUS_OK is the card failing. */
gpu_status_t render_weather_take(render_weather_t *weather, const render_weather_slot_t *slot, int64_t now_ns,
                                 device_memory_t *memory, const device_block_t *map_block);

/* render_weather_take with the map sent through staging, so it does not wait for the frame on the card (plan 008 phase 03). nullptr staging
 * is render_weather_take. */
gpu_status_t render_weather_take_staged(render_weather_t *weather, const render_weather_slot_t *slot, int64_t now_ns,
                                        device_memory_t *memory, device_staging_t *staging,
                                        const device_block_t *map_block);

/* How far is seen through weather, in blocks, against fog_end, the setting's distance: max(32, F (1 - 0.85 max(fog, dust)) /
 * (1 + rain / 10)). Pure. */
float32_t render_weather_visibility(float32_t fog_end, float32_t fog, float32_t dust, float32_t rain_mm_h);

/* What weather does to a frame's sky, light and fog, from the clear day's, and what the kernels get, for an eye in eye_cell:
 * cloud greys the sky and dims the sun, a flash lights both, rain, fog and dust bring the fog in and tint it. fog_on says whether
 * the frame has fog at all, and fog_end is its setting. With everything at 0 post and sun are left to the bit. Pure. */
void render_weather_apply(const render_weather_t *weather, const int32_t eye_cell[3], bool fog_on, float32_t fog_end,
                          post_settings_t *post, shade_sun_t *sun, post_weather_t *out);

/* The flicker of a bolt this old: full, half, full over RENDER_WEATHER_BOLT_SECONDS, and 0 before or after. Pure. */
float32_t render_weather_flicker(float32_t age);

/* The main thread. The newest picture not taken yet, or nullptr if there is none. It stays whole until this next returns one. */
const uint8_t *render_frame_take(render_frame_t *frame);

/* The main thread. Copies a picture to a buffer of the same size, which may be scanout memory, with a fence after it. */
void render_frame_present(const render_frame_t *frame, const uint8_t *picture, display_buffer_t *target);

/* The same for pixels of the target's own size, whatever size the frame draws at: a picture brought up to it from a smaller one. */
void render_frame_present_full(const uint8_t *picture, display_buffer_t *target);

/* What the frames took, as a log line for each pass, with percentiles. Reorders the samples, so for the end of a run. */
void render_frame_report(render_frame_t *frame);

/* The frame's picture reduced to what a person would call its time: the p50 and p99 of total_ms, for tests and logs. Reorders
 * the samples. */
void render_frame_percentiles(render_frame_t *frame, float32_t *out_p50, float32_t *out_p99);

#endif
