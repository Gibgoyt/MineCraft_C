/* ctest run_render_distance_live. See README.md. */
#include "harness/index.h"

#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/fill/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "job/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum : uint32_t {
    DL_WIDTH = 640u,
    DL_HEIGHT = 360u,
    DL_MARKS = 40u,
    DL_CROP = 24u,         /* the square round the crosshair that the log fills */
    DL_TOLERANCE = 16u,    /* a channel this far off is a pixel changed: 6% */
    DL_LOG_REACH = 5u,     /* the eye stands this many blocks from the log's face */
    DL_SEARCH_CHUNKS = 3u, /* each way from the eye's chunk */
    DL_PATH_BYTES = 512u,
};
enum : uint64_t {
    /* Ahmed's seed, which spawned at his own point, -7.5, -7.5, 40, in a forest (plan 005 phase 14, "Measured live"). */
    DL_SEED = 11107447384583428913ull,
};
enum : int64_t {
    DL_FRAME_NS = 8000000,               /* main asks for a picture at 120 Hz, as a display would */
    DL_START_DEADLINE_NS = 1000000000,   /* Ahmed: a change shows "in less than 1 second" */
    DL_NEAR_DEADLINE_NS = 2000000000,    /* the targets: the ground out to 4 chunks, from opening */
    DL_SETTLE_DEADLINE_NS = 15000000000, /* the targets: everything out to render distance 32 */
    DL_WAIT_DEFAULT_NS = 20000000000,    /* what each step waits for its fill, past which it goes on */
    DL_AFTER_DONE_NS = 500000000,        /* pictures still taken once a fill is done, for the last */
    DL_LOG_REFERENCE_NS = 2000000000,    /* the log is looked at this long before anything happens */
    DL_BUSY_NS = 1000000000,             /* the log is mined this long into a change, while it is busiest */
};

/* Less of the picture than this changing is a change with nothing to see: 0.25%. */
#define DL_VISIBLE_SHARE 0.0025f64

/* Seconds after a step's start at which the picture is kept, as plan 005 phase 14's measure.sh took them; then every 10 s. */
static const float64_t dl_mark_seconds[] = {0.0f64, 0.5f64,  1.0f64,  2.0f64,  3.0f64, 5.0f64,
                                            8.0f64, 12.0f64, 16.0f64, 20.0f64, 30.0f64};

/* ---- A rig: everything a real run has, on memory of its own ----------------------------------------------------------- */

typedef struct dl_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
} dl_rig_t;
static_assert(sizeof(dl_rig_t) == 64);

static void *dl_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool dl_rig_begin(test_context_t *t, dl_rig_t *rig) {
    rig->bridge = dl_aligned(t, sizeof(gpu_bridge_t));
    rig->job = dl_aligned(t, sizeof(job_system_t));
    rig->world = dl_aligned(t, sizeof(world_t));
    rig->terrain = dl_aligned(t, sizeof(terrain_t));
    rig->render = dl_aligned(t, sizeof(render_t));
    rig->texture = dl_aligned(t, sizeof(texture_t));
    rig->game = dl_aligned(t, sizeof(game_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, DL_SEED)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    return true;
}

static void dl_rig_end(test_context_t *t, dl_rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

/* ---- Watching the picture --------------------------------------------------------------------------------------------- */

typedef struct dl_watch_t {
    uint8_t *screen;          /* 0: what main shows, at DL_WIDTH by DL_HEIGHT, 4 bytes a pixel */
    uint8_t *marks[DL_MARKS]; /* 8: the picture at each mark */
    uint8_t *crop_reference;  /* 328: the crosshair's square before the log was mined, or nullptr */
    char *shots;              /* 336: a directory for PNGs, or nullptr */
    display_buffer_t target;  /* 344 */
    uint64_t bytes;           /* 400: of a picture */
    int64_t wait_ns;          /* 408: how long a step waits for its fill */
    uint8_t *before;          /* 416: what was on the screen as the step began: before the change */
    uint8_t
        deadlines; /* 424: the deadlines fail the test: in an optimized build, or as MC_DISTANCE_LIVE_DEADLINES says */
    uint8_t _pad0[7]; /* 425 */
} dl_watch_t;
static_assert(sizeof(dl_watch_t) == 432);

/* What one step came to. Times from its start, -1 for never. */
typedef struct dl_step_t {
    game_fill_summary_t fill;       /* 0 */
    float64_t mark_share[DL_MARKS]; /* 64: of the picture, still differing from the last; -1 for a mark not kept */
    float64_t mark_at[DL_MARKS];    /* 384: when it was kept, in seconds */
    int64_t crop_changed_ns;        /* 704: the crosshair's square no longer the log's */
    int64_t ran_ns;                 /* 712 */
    float64_t before_share;         /* 720: of the picture, differing between before the step and its last */
    float64_t progress_1s; /* 728: how much of that had come by the 1 s mark, 0 to 1; -1 if that mark was not kept */
    uint32_t mark_count;   /* 736 */
    uint8_t _pad0[4];      /* 740 */
} dl_step_t;
static_assert(sizeof(dl_step_t) == 744);

/* Of all pixels, the share with any channel more than DL_TOLERANCE off. */
static float64_t dl_share(const uint8_t *a, const uint8_t *b, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h) {
    uint64_t changed = 0u;
    for (uint32_t y = y0; y < y0 + h; y++) {
        for (uint32_t x = x0; x < x0 + w; x++) {
            const uint64_t at = ((uint64_t)y * DL_WIDTH + x) * 4u;
            bool differs = false;
            for (uint32_t c = 0u; c < 3u; c++) {
                const int32_t d = (int32_t)a[at + c] - (int32_t)b[at + c];
                differs = differs || d > (int32_t)DL_TOLERANCE || d < -(int32_t)DL_TOLERANCE;
            }
            changed += differs ? 1u : 0u;
        }
    }
    return (float64_t)changed / (float64_t)((uint64_t)w * h);
}

static float64_t dl_crop_share(const uint8_t *a, const uint8_t *b) {
    return dl_share(a, b, (DL_WIDTH - DL_CROP) / 2u, (DL_HEIGHT - DL_CROP) / 2u, DL_CROP, DL_CROP);
}

static void dl_png(test_context_t *t, const dl_watch_t *watch, const char *step, const char *name,
                   const uint8_t *picture) {
    if (watch->shots == nullptr) {
        return;
    }
    const uint64_t capacity = display_png_bytes_needed(DL_WIDTH, DL_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    uint64_t written = 0u;
    char path[DL_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/%s_%s.png", watch->shots, step, name);
    if (display_png_encode(picture, DL_WIDTH * 4u, DL_WIDTH, DL_HEIGHT, file, capacity, &written)) {
        (void)file_write_atomic(path, file, written);
    }
}

static float64_t dl_mark_second(uint32_t i) {
    const uint32_t listed = (uint32_t)(sizeof(dl_mark_seconds) / sizeof(dl_mark_seconds[0]));
    return i < listed ? dl_mark_seconds[i] : dl_mark_seconds[listed - 1u] + 10.0f64 * (float64_t)(i + 1u - listed);
}

static float64_t dl_seconds(int64_t ns) {
    return ns < 0 ? -1.0f64 : (float64_t)ns / (float64_t)TIME_NS_PER_SECOND;
}

/* Frames by the clock from now until the fill that sequence names is done and a little after, or wait_ns. The pictures at the marks
 * are kept, and with a crop reference the crosshair's square is compared every frame. until_ns, if not 0, ends it sooner. */
static bool dl_watch(test_context_t *t, dl_rig_t *rig, dl_watch_t *watch, const char *name, uint32_t sequence,
                     int64_t until_ns, dl_step_t *out) {
    game_t *const game = rig->game;
    const game_fill_t *const fill = game_fill_of(game);
    const render_fly_input_t still = {0};
    memset(out, 0, sizeof(*out));
    out->crop_changed_ns = -1;
    memcpy(watch->before, watch->screen, watch->bytes);
    const int64_t started_ns = time_now_ns();
    const int64_t limit_ns = until_ns != 0 ? until_ns : watch->wait_ns;
    uint32_t marks = 0u;
    int64_t done_seen_ns = -1;
    for (;;) {
        if (!game_loop_frame(game, &still, (float32_t)DL_FRAME_NS / (float32_t)TIME_NS_PER_SECOND, &watch->target)) {
            return false;
        }
        const int64_t now_ns = time_now_ns() - started_ns;
        while (marks < DL_MARKS && (float64_t)now_ns >= dl_mark_second(marks) * (float64_t)TIME_NS_PER_SECOND &&
               (float64_t)now_ns < (dl_mark_second(marks) + 0.25f64) * (float64_t)TIME_NS_PER_SECOND) {
            memcpy(watch->marks[marks], watch->screen, watch->bytes);
            out->mark_at[marks] = dl_seconds(now_ns);
            marks++;
        }
        while (marks < DL_MARKS &&
               (float64_t)now_ns >= (dl_mark_second(marks) + 0.25f64) * (float64_t)TIME_NS_PER_SECOND) {
            marks++; /* a frame came too late for it: none kept */
        }
        if (watch->crop_reference != nullptr && out->crop_changed_ns < 0 &&
            dl_crop_share(watch->screen, watch->crop_reference) > 0.3f64) {
            out->crop_changed_ns = now_ns;
        }
        const bool ours = atomic_load_explicit(&fill->sequence, memory_order_acquire) == sequence;
        if (until_ns == 0 && ours && done_seen_ns < 0 &&
            atomic_load_explicit(&fill->done_ns, memory_order_acquire) >= 0) {
            done_seen_ns = now_ns;
        }
        const bool crop_wanted = watch->crop_reference != nullptr && out->crop_changed_ns < 0;
        if (done_seen_ns >= 0 && now_ns - done_seen_ns >= DL_AFTER_DONE_NS && now_ns >= 2 * DL_START_DEADLINE_NS &&
            !crop_wanted) {
            break;
        }
        if (now_ns >= limit_ns) {
            break;
        }
        time_sleep_ns(DL_FRAME_NS);
    }
    out->ran_ns = time_now_ns() - started_ns;
    game_fill_summary(fill, &out->fill);
    out->mark_count = marks;
    for (uint32_t i = 0u; i < marks; i++) {
        out->mark_share[i] = out->mark_at[i] > 0.0f64 || i == 0u
                                 ? dl_share(watch->marks[i], watch->screen, 0u, 0u, DL_WIDTH, DL_HEIGHT)
                                 : -1.0f64;
        char label[32];
        snprintf(label, sizeof(label), "%06.1f", (libc_double_t)dl_mark_second(i));
        if (out->mark_at[i] > 0.0f64 || i == 0u) {
            dl_png(t, watch, name, label, watch->marks[i]);
        }
    }
    dl_png(t, watch, name, "last", watch->screen);
    out->before_share = dl_share(watch->before, watch->screen, 0u, 0u, DL_WIDTH, DL_HEIGHT);
    out->progress_1s = marks > 2u && out->mark_share[2] >= 0.0f64 && out->before_share > 0.0f64
                           ? 1.0f64 - out->mark_share[2] / out->before_share
                           : -1.0f64;
    printf("  %s: ran %.1f s; first change %.2f s, rings to 4 %.2f s, all rings %.2f s, far field %.2f s, all %.2f s\n",
           name, (libc_double_t)dl_seconds(out->ran_ns), (libc_double_t)dl_seconds(out->fill.first_ns),
           (libc_double_t)dl_seconds(out->fill.ring_near_ns), (libc_double_t)dl_seconds(out->fill.near_ns),
           (libc_double_t)dl_seconds(out->fill.far_ns), (libc_double_t)dl_seconds(out->fill.done_ns));
    printf("    picture: %.2f%% differs between before and the last, %.0f%% of that came by 1 s; still differing from "
           "the last:",
           (libc_double_t)(out->before_share * 100.0f64), (libc_double_t)(out->progress_1s * 100.0f64));
    for (uint32_t i = 0u; i < marks; i++) {
        if (out->mark_share[i] >= 0.0f64) {
            printf(" %.1f s %.2f%%;", (libc_double_t)dl_mark_second(i), (libc_double_t)(out->mark_share[i] * 100.0f64));
        }
    }
    printf("\n");
    return true;
}

/* A change's deadlines: under a second to start showing, in the game's books and in the picture, and the targets' 15 s to settle. In the
 * picture, starting to show is half of what the change does to it there by 1 s, unless it does less than DL_VISIBLE_SHARE in all. */
static void dl_hold_change(test_context_t *t, const dl_watch_t *watch, const char *name, const dl_step_t *step) {
    const bool started = step->fill.first_ns >= 0 && step->fill.first_ns < DL_START_DEADLINE_NS;
    const bool seen = step->before_share < DL_VISIBLE_SHARE || step->progress_1s >= 0.5f64;
    if (!seen) {
        printf("  MISSED %s: by 1 s the picture had come %.0f%% of the way to its last, not half\n", name,
               (libc_double_t)(step->progress_1s * 100.0f64));
    }
    const bool settled = step->fill.done_ns >= 0 && step->fill.done_ns < DL_SETTLE_DEADLINE_NS;
    if (!started) {
        printf("  MISSED %s: the change started showing at %.2f s, not under %.2f s\n", name,
               (libc_double_t)dl_seconds(step->fill.first_ns), (libc_double_t)dl_seconds(DL_START_DEADLINE_NS));
    }
    if (!settled) {
        printf("  MISSED %s: all of it drawn at %.2f s (-1 is not by %.1f s), not under %.2f s\n", name,
               (libc_double_t)dl_seconds(step->fill.done_ns), (libc_double_t)dl_seconds(step->ran_ns),
               (libc_double_t)dl_seconds(DL_SETTLE_DEADLINE_NS));
    }
    if (watch->deadlines != 0u) {
        CHECK(started);
        CHECK(seen);
        CHECK(settled);
    }
}

/* ---- The lit log ------------------------------------------------------------------------------------------------------ */

static uint16_t dl_block(const world_t *world, int32_t x, int32_t y, int32_t z) {
    const column_t *const column = world_map_column_at(&world->map, column_chunk_of(x), column_chunk_of(y));
    uint16_t block = BLOCK_ID_AIR;
    if (column == nullptr || !column_block_get(column, (uint32_t)(x - column_chunk_of(x) * (int32_t)COLUMN_SIZE),
                                               (uint32_t)(y - column_chunk_of(y) * (int32_t)COLUMN_SIZE), z, &block)) {
        return UINT16_MAX; /* not loaded: not to be trusted */
    }
    return block;
}

static bool dl_is_log(uint16_t block) {
    if (block >= BLOCK_COUNT) {
        return false;
    }
    const char *const name = block_defs[block].name;
    const uint64_t length = strlen(name);
    return length > 4u && strcmp(name + length - 4u, "_log") == 0;
}

/* A log near the eye with open sky in front of one face: DL_LOG_REACH + 1 blocks of air at its height and the one above, and nothing
 * over the place the eye will stand. So it is lit by the sky, and nothing is between it and the eye. And no log right behind it, so that
 * mining it changes what the crosshair's square shows. */
static bool dl_find_log(const world_t *world, const float64_t eye[3], int32_t out_cell[3], int32_t out_step[2]) {
    static const int32_t steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    const int32_t eye_x = (int32_t)floor(eye[0]), eye_y = (int32_t)floor(eye[1]), eye_z = (int32_t)floor(eye[2]);
    const int32_t reach = (int32_t)(DL_SEARCH_CHUNKS * COLUMN_SIZE);
    int64_t best = INT64_MAX;
    for (int32_t y = eye_y - reach; y <= eye_y + reach; y++) {
        for (int32_t x = eye_x - reach; x <= eye_x + reach; x++) {
            for (int32_t z = eye_z - 48; z <= eye_z + 16; z++) {
                if (!dl_is_log(dl_block(world, x, y, z))) {
                    continue;
                }
                for (uint32_t s = 0u; s < 4u; s++) {
                    /* Not a log with another log behind it, as in a jungle tree's 2 x 2 trunk (plan 006 phase 04): mined, it shows the same bark. */
                    bool open = !dl_is_log(dl_block(world, x - steps[s][0], y - steps[s][1], z));
                    for (int32_t k = 1; open && k <= (int32_t)DL_LOG_REACH + 1; k++) {
                        open = dl_block(world, x + steps[s][0] * k, y + steps[s][1] * k, z) == BLOCK_ID_AIR &&
                               dl_block(world, x + steps[s][0] * k, y + steps[s][1] * k, z + 1) == BLOCK_ID_AIR;
                    }
                    const int32_t stand_x = x + steps[s][0] * (int32_t)DL_LOG_REACH;
                    const int32_t stand_y = y + steps[s][1] * (int32_t)DL_LOG_REACH;
                    for (int32_t above = z + 2; open && above < z + 96; above++) {
                        open = dl_block(world, stand_x, stand_y, above) == BLOCK_ID_AIR;
                    }
                    const int64_t d = (int64_t)(x - eye_x) * (x - eye_x) + (int64_t)(y - eye_y) * (y - eye_y);
                    if (open && d < best) {
                        best = d;
                        out_cell[0] = x;
                        out_cell[1] = y;
                        out_cell[2] = z;
                        out_step[0] = steps[s][0];
                        out_step[1] = steps[s][1];
                    }
                }
            }
        }
    }
    return best != INT64_MAX;
}

/* ---- The test --------------------------------------------------------------------------------------------------------- */

static int64_t dl_env_seconds(const char *name, int64_t fallback_ns) {
    const char *const text = getenv(name);
    if (text == nullptr || text[0] == '\0') {
        return fallback_ns;
    }
    return (int64_t)strtoll(text, nullptr, 10) * TIME_NS_PER_SECOND;
}

/* One change, as the Settings screen makes it. */
typedef struct dl_change_t {
    const char *name;         /* 0 */
    uint32_t render_distance; /* 8 */
    uint8_t _pad0[4];         /* 12 */
} dl_change_t;
static_assert(sizeof(dl_change_t) == 16);

static bool dl_change(test_context_t *t, dl_rig_t *rig, game_config_t *config, uint32_t render_distance,
                      uint32_t *sequence) {
    config->render_distance = render_distance;
    if (!CHECK(game_reconfigure(rig->game, config))) {
        return false;
    }
    *sequence = atomic_load_explicit(&game_fill_of(rig->game)->sequence, memory_order_acquire);
    return true;
}

/* A lit log near the eye, looked at square on for 2 s, then mined as the hands would: through world/edit, with the threads stopped for the
 * moment it takes. With busy, render distance goes from 64 to 48 first, and the log is
 * mined a second into that change, while workers are busiest. The eye goes back to where it was after. */
static bool dl_mine(test_context_t *t, dl_rig_t *rig, dl_watch_t *watch, game_config_t *config, const char *name,
                    bool busy, uint32_t *sequence) {
    game_t *const game = rig->game;
    const render_camera_t kept = game->camera;
    if (!CHECK(game_loop_stop(game))) {
        return false;
    }
    int32_t cell[3] = {0, 0, 0};
    int32_t step_xy[2] = {0, 0};
    if (!CHECK(dl_find_log(rig->world, game->camera.position, cell, step_xy))) {
        printf("  %s: no lit log within %" PRIu32 " chunks\n", name, (uint32_t)DL_SEARCH_CHUNKS);
        return false;
    }
    const float32_t yaw = step_xy[0] == 1    ? 3.14159265f32
                          : step_xy[0] == -1 ? 0.0f32
                          : step_xy[1] == 1  ? -1.5707963f32
                                             : 1.5707963f32;
    render_camera_start(&game->camera, (float64_t)(cell[0] + step_xy[0] * (int32_t)DL_LOG_REACH) + 0.5f64,
                        (float64_t)(cell[1] + step_xy[1] * (int32_t)DL_LOG_REACH) + 0.5f64, (float64_t)cell[2] + 0.5f64,
                        yaw, 0.0f32);
    game->camera.half_fov_tangent = kept.half_fov_tangent;
    printf("  %s: %s at %" PRId32 ", %" PRId32 ", %" PRId32 ", seen from %" PRId32 " blocks %s\n", name,
           block_defs[dl_block(rig->world, cell[0], cell[1], cell[2])].name, cell[0], cell[1], cell[2],
           (int32_t)DL_LOG_REACH,
           step_xy[0] == 1    ? "east"
           : step_xy[0] == -1 ? "west"
           : step_xy[1] == 1  ? "north"
                              : "south");
    if (!CHECK(game_loop_start(game, nullptr))) {
        return false;
    }
    char label[64];
    snprintf(label, sizeof(label), "%s_before", name);
    dl_step_t step;
    if (!dl_watch(t, rig, watch, label, *sequence, DL_LOG_REFERENCE_NS, &step)) {
        return false;
    }
    uint8_t *const reference = dl_aligned(t, watch->bytes);
    memcpy(reference, watch->screen, watch->bytes);
    dl_step_t change = {0};
    if (busy) {
        if (!dl_change(t, rig, config, 48u, sequence)) {
            return false;
        }
        snprintf(label, sizeof(label), "%s_change", name);
        if (!dl_watch(t, rig, watch, label, *sequence, DL_BUSY_NS, &change)) {
            return false;
        }
    }
    if (!CHECK(game_loop_stop(game))) {
        return false;
    }
    CHECK_EQ_U64(world_edit_block_set(&rig->world->edit, cell[0], cell[1], cell[2], BLOCK_ID_AIR),
                 WORLD_EDIT_STATUS_CHANGED);
    if (!CHECK(game_loop_start(game, nullptr))) {
        return false;
    }
    watch->crop_reference = reference;
    snprintf(label, sizeof(label), "%s_mined", name);
    const bool watched = dl_watch(t, rig, watch, label, *sequence, 0, &step);
    watch->crop_reference = nullptr;
    if (!watched) {
        return false;
    }
    printf("  %s: the mined log went from the picture at %.2f s", name,
           (libc_double_t)dl_seconds(step.crop_changed_ns));
    if (busy) {
        printf(", %.2f s into the change",
               (libc_double_t)dl_seconds(step.crop_changed_ns < 0 ? -1 : step.crop_changed_ns + change.ran_ns));
    }
    printf("\n");
    const bool gone = step.crop_changed_ns >= 0 && step.crop_changed_ns < DL_START_DEADLINE_NS;
    if (!gone) {
        printf("  MISSED %s: the log went at %.2f s (-1 is not by %.1f s), not under %.2f s\n", name,
               (libc_double_t)dl_seconds(step.crop_changed_ns), (libc_double_t)dl_seconds(step.ran_ns),
               (libc_double_t)dl_seconds(DL_START_DEADLINE_NS));
    }
    if (watch->deadlines != 0u) {
        CHECK(gone);
    }
    if (busy) {
        /* The change the log was mined in, from its start: its fill went on through the mining. */
        step.fill.first_ns = change.fill.first_ns >= 0 ? change.fill.first_ns : step.fill.first_ns;
        dl_hold_change(t, watch, "rd_48_busy", &step);
    }
    if (!CHECK(game_loop_stop(game))) {
        return false;
    }
    game->camera = kept;
    if (!CHECK(game_loop_start(game, nullptr))) {
        return false;
    }
    /* Pictures from where the eye went back to, so the next step's before is of that. */
    snprintf(label, sizeof(label), "%s_back", name);
    return dl_watch(t, rig, watch, label, *sequence, DL_LOG_REFERENCE_NS, &step);
}

/* Ahmed's words: "agents need to, during a live game, please change render distance and see a very very quick update in less than 1
 * second please!!", and "i mine a tree and after it has all settled 5 minutes later then the tree shows as disappeared". */
TEST(render_distance_changes_show_within_a_second_and_a_mined_log_goes_at_once) {
    dl_rig_t rig;
    REQUIRE(dl_rig_begin(t, &rig));
    game_t *const game = rig.game;
    dl_watch_t watch = {0};
    watch.bytes = (uint64_t)DL_WIDTH * DL_HEIGHT * 4u;
    watch.screen = dl_aligned(t, watch.bytes);
    watch.before = dl_aligned(t, watch.bytes);
    for (uint32_t i = 0u; i < DL_MARKS; i++) {
        watch.marks[i] = dl_aligned(t, watch.bytes);
    }
    watch.target = (display_buffer_t){.owned_map = watch.screen,
                                      .map_bytes = watch.bytes,
                                      .pitch = DL_WIDTH * 4u,
                                      .width = DL_WIDTH,
                                      .height = DL_HEIGHT};
    watch.wait_ns = dl_env_seconds("MC_DISTANCE_LIVE_WAIT_S", DL_WAIT_DEFAULT_NS);
    /* The deadlines are for an optimized build: a debug build's terrain alone misses them, as its numbers mean nothing (bench/world's
     * README). It still runs every step and says what it missed. */
#if defined(NDEBUG)
    watch.deadlines = 1u;
#else
    watch.deadlines = 0u;
#endif
    const char *const deadlines = getenv("MC_DISTANCE_LIVE_DEADLINES");
    if (deadlines != nullptr && deadlines[0] != '\0') {
        watch.deadlines = strcmp(deadlines, "0") == 0 ? (uint8_t)0u : (uint8_t)1u;
    }
    watch.shots = getenv("MC_DISTANCE_LIVE_SHOTS");
    if (watch.shots != nullptr) {
        (void)mkdir(watch.shots, 0755);
    }

    /* Ahmed's settings.yaml: render distance 32, and level 0 as the game works it out (plan 009 phase 04). The day stands still and each
     * frame stands alone, so the picture changes only with the world. */
    /* MC_DISTANCE_LIVE_OPEN measures a new world at another render distance, as a raised default would open it (plan 005 phase 8). */
    const char *const open_text = getenv("MC_DISTANCE_LIVE_OPEN");
    const uint64_t open_asked = open_text != nullptr && open_text[0] != '\0' ? strtoull(open_text, nullptr, 10) : 32u;
    const uint32_t open_distance =
        open_asked >= 2u && open_asked <= (uint64_t)MC_GAME_RENDER_DISTANCE_MAX ? (uint32_t)open_asked : 32u;
    char open_name[16];
    (void)snprintf(open_name, sizeof(open_name), "open_%" PRIu32, open_distance);
    game_config_t config = {.render_distance = open_distance,
                            .width = DL_WIDTH,
                            .height = DL_HEIGHT,
                            .features_off = SHADE_FEATURE_DAY_CYCLE | SHADE_FEATURE_AO | SHADE_FEATURE_TAA,
                            .show_date = 0u};
    dl_step_t step;

    /* A world opening, from cold. */
    const int64_t open_started_ns = time_now_ns();
    REQUIRE(game_open(game, &config));
    const int64_t open_ns = time_now_ns() - open_started_ns;
    REQUIRE(game_loop_start(game, nullptr));
    uint32_t sequence = atomic_load_explicit(&game_fill_of(game)->sequence, memory_order_acquire);
    printf("  game_open took %.2f s, at %.1f, %.1f, %.1f\n", (libc_double_t)dl_seconds(open_ns),
           (libc_double_t)game->camera.position[0], (libc_double_t)game->camera.position[1],
           (libc_double_t)game->camera.position[2]);
    REQUIRE(dl_watch(t, &rig, &watch, open_name, sequence, 0, &step));
    {
        const bool near = step.fill.ring_near_ns >= 0 && step.fill.ring_near_ns < DL_NEAR_DEADLINE_NS;
        const bool all = step.fill.done_ns >= 0 && step.fill.done_ns < DL_SETTLE_DEADLINE_NS;
        if (!near) {
            printf("  MISSED %s: the ground out to 4 chunks at %.2f s, not under %.2f s\n", open_name,
                   (libc_double_t)dl_seconds(step.fill.ring_near_ns), (libc_double_t)dl_seconds(DL_NEAR_DEADLINE_NS));
        }
        if (!all) {
            printf("  MISSED %s: everything at %.2f s, not under %.2f s\n", open_name,
                   (libc_double_t)dl_seconds(step.fill.done_ns), (libc_double_t)dl_seconds(DL_SETTLE_DEADLINE_NS));
        }
        if (watch.deadlines != 0u) {
            CHECK(near);
            CHECK(all);
        }
    }

    /* A lit log mined at render distance 32 with nothing else going on. */
    REQUIRE(dl_mine(t, &rig, &watch, &config, "log_32", false, &sequence));

    /* Render distance 32 to 64 and back down to 16, and to 64 again, as the Settings screen does them. */
    static const dl_change_t changes[] = {{"rd_64", 64u, {0}}, {"rd_16", 16u, {0}}, {"rd_64_again", 64u, {0}}};
    for (uint32_t i = 0u; i < sizeof(changes) / sizeof(changes[0]); i++) {
        REQUIRE(dl_change(t, &rig, &config, changes[i].render_distance, &sequence));
        REQUIRE(dl_watch(t, &rig, &watch, changes[i].name, sequence, 0, &step));
        dl_hold_change(t, &watch, changes[i].name, &step);
    }

    /* Render distance down to 48 from 64, a far field planned again, and a second into it a lit log mined. */
    REQUIRE(dl_mine(t, &rig, &watch, &config, "log_64_busy", true, &sequence));

    REQUIRE(game_loop_stop(game));
    game_report(game);
    game_close(game);
    dl_rig_end(t, &rig);
}

/* ---- The far field follows an edit (plan 009 phases 03 and 04) ----------------------------------------------------------- */

enum : int32_t {
    /* Blocks east of the eye: well inside level 0's 27 chunks, so that the level 2 column over it, 4 chunks wide, stands wholly on loaded
     * blocks and is made from them as level 1's are. At 400 it reached chunk 27, past the loaded circle, and came from terrain. */
    DL_EDIT_DISTANCE = 320,
    DL_EDIT_CUBE = 4,  /* a level 2 voxel's side, so levels 1 and 2 both change */
    DL_EDIT_ABOVE = 8, /* blocks above the ground, in the open air */
};
enum : uint32_t {
    /* Ahmed's picture: the bands follow the rows, and at 1080 level 1 reaches 771 blocks; at 360 it would stop at 257. */
    DL_EDIT_WIDTH = 1920u,
    DL_EDIT_HEIGHT = 1080u,
};
enum : int64_t {
    DL_EDIT_DEADLINE_NS = 100000000, /* the phase file: an edit reaches levels 1 and 2 within 100 ms */
    DL_EDIT_WAIT_NS = 3000000000,
};

/* A 4 x 4 x 4 cube of stone put in the open air 320 blocks from the eye, through world/edit with the threads stopped, as the hands would. The
 * store's watch says when the level 1 and level 2 columns holding it were sent with it, and what their voxel there was. */
TEST(an_edit_reaches_level_1_and_level_2_within_100_ms) {
    dl_rig_t rig;
    REQUIRE(dl_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const uint64_t bytes = (uint64_t)DL_EDIT_WIDTH * DL_EDIT_HEIGHT * 4u;
    uint8_t *const screen = dl_aligned(t, bytes);
    display_buffer_t target = {.owned_map = screen,
                               .map_bytes = bytes,
                               .pitch = DL_EDIT_WIDTH * 4u,
                               .width = DL_EDIT_WIDTH,
                               .height = DL_EDIT_HEIGHT};
    game_config_t config = {.render_distance = 64u,
                            .width = DL_EDIT_WIDTH,
                            .height = DL_EDIT_HEIGHT,
                            .features_off = SHADE_FEATURE_DAY_CYCLE | SHADE_FEATURE_AO | SHADE_FEATURE_TAA,
                            .show_date = 0u};
    REQUIRE(game_open(game, &config));
    /* Everything the eye's place wants, the far field included, on the card before anything is changed. */
    const int64_t settle_started_ns = time_now_ns();
    REQUIRE(game_settle(game));
    render_t *const render = rig.render;
    const render_lod_t *const far = render->borrowed_lod;
    REQUIRE(far != nullptr);
    printf("  settled in %.2f s; the far field has %" PRIu32 " levels, %" PRIu64 " columns built, %" PRIu64
           " of level 1 and %" PRIu64 " of level 2 from octants\n",
           (libc_double_t)dl_seconds(time_now_ns() - settle_started_ns), far->level_count, far->stats.columns_built,
           render->borrowed_octants->stats.columns_assembled[0], render->borrowed_octants->stats.columns_assembled[1]);
    CHECK(render->borrowed_octants->stats.columns_assembled[0] > 0u);
    CHECK(render->borrowed_octants->stats.columns_assembled[1] > 0u);

    /* The cube's corner: on a level 2 voxel, in the open air over the ground there. */
    const int32_t x = ((int32_t)floor(game->camera.position[0]) + DL_EDIT_DISTANCE) & ~(DL_EDIT_CUBE - 1);
    const int32_t y = (int32_t)floor(game->camera.position[1]) & ~(DL_EDIT_CUBE - 1);
    /* dl_block says UINT16_MAX for open air that holds no array too, so the column itself is asked for. */
    REQUIRE(world_map_column_at(&rig.world->map, column_chunk_of(x), column_chunk_of(y)) != nullptr);
    /* The highest top under any of the cube's columns, a tree's leaves included: the land there is the generator's (plan 006
     * phase 03 put a jungle round the default seed's start), and one column's top says nothing of its neighbour's crown. */
    int32_t ground = COLUMN_Z_STORAGE_MIN;
    for (int32_t dy = 0; dy < DL_EDIT_CUBE; dy++) {
        for (int32_t dx = 0; dx < DL_EDIT_CUBE; dx++) {
            int32_t top = COLUMN_Z_STORAGE_END - 1;
            while (top > ground && (dl_block(rig.world, x + dx, y + dy, top) == BLOCK_ID_AIR ||
                                    dl_block(rig.world, x + dx, y + dy, top) == UINT16_MAX)) {
                top--;
            }
            ground = top > ground ? top : ground;
        }
    }
    const int32_t z = (ground + DL_EDIT_ABOVE + DL_EDIT_CUBE) & ~(DL_EDIT_CUBE - 1);
    for (int32_t dz = 0; dz < DL_EDIT_CUBE; dz++) {
        for (int32_t dy = 0; dy < DL_EDIT_CUBE; dy++) {
            for (int32_t dx = 0; dx < DL_EDIT_CUBE; dx++) {
                const uint16_t there = dl_block(rig.world, x + dx, y + dy, z + dz);
                REQUIRE(there == BLOCK_ID_AIR || there == UINT16_MAX);
            }
        }
    }
    printf("  a cube of stone at %" PRId32 ", %" PRId32 ", %" PRId32 ", %" PRId32
           " blocks from the eye, over ground at %" PRId32 "\n",
           x, y, z, x - (int32_t)floor(game->camera.position[0]), ground);
    render_octant_watch_t *const watch = &render->borrowed_octants->watch;
    render_octant_watch_arm(render->borrowed_octants, x, y, z);
    for (int32_t dz = 0; dz < DL_EDIT_CUBE; dz++) {
        for (int32_t dy = 0; dy < DL_EDIT_CUBE; dy++) {
            for (int32_t dx = 0; dx < DL_EDIT_CUBE; dx++) {
                CHECK_EQ_U64(world_edit_block_set(&rig.world->edit, x + dx, y + dy, z + dz, BLOCK_ID_STONE),
                             WORLD_EDIT_STATUS_CHANGED);
            }
        }
    }
    const int64_t edited_ns = time_now_ns();
    REQUIRE(game_loop_start(game, nullptr));
    const render_fly_input_t still = {0};
    int64_t seen_ns[3] = {-1, -1, -1};
    while (time_now_ns() - edited_ns < DL_EDIT_WAIT_NS && (seen_ns[1] < 0 || seen_ns[2] < 0)) {
        REQUIRE(game_loop_frame(game, &still, (float32_t)DL_FRAME_NS / (float32_t)TIME_NS_PER_SECOND, &target));
        for (uint32_t shift = 1u; shift <= 2u; shift++) {
            const int64_t at = atomic_load_explicit(&watch->seen_ns[shift], memory_order_acquire);
            if (seen_ns[shift] < 0 && at > edited_ns &&
                atomic_load_explicit(&watch->material[shift], memory_order_relaxed) == BLOCK_ID_STONE) {
                seen_ns[shift] = at - edited_ns;
            }
        }
        time_sleep_ns(DL_FRAME_NS / 8);
    }
    REQUIRE(game_loop_stop(game));
    printf("  level 1 had the stone %.1f ms after the edit, level 2 %.1f ms (-1 is not within %.0f s)\n",
           (libc_double_t)((float64_t)seen_ns[1] * 1.0e-6f64), (libc_double_t)((float64_t)seen_ns[2] * 1.0e-6f64),
           (libc_double_t)dl_seconds(DL_EDIT_WAIT_NS));
    CHECK(seen_ns[1] >= 0);
    CHECK(seen_ns[2] >= 0);
    /* As the file's other deadlines: held in an optimized build, or as MC_DISTANCE_LIVE_DEADLINES says; reported in any. */
#if defined(NDEBUG)
    bool deadlines = true;
#else
    bool deadlines = false;
#endif
    const char *const asked = getenv("MC_DISTANCE_LIVE_DEADLINES");
    if (asked != nullptr && asked[0] != '\0') {
        deadlines = strcmp(asked, "0") != 0;
    }
    for (uint32_t shift = 1u; shift <= 2u; shift++) {
        if (seen_ns[shift] > DL_EDIT_DEADLINE_NS) {
            printf("  MISSED level %" PRIu32 ": %.1f ms, not within %.0f ms\n", shift,
                   (libc_double_t)((float64_t)seen_ns[shift] * 1.0e-6f64),
                   (libc_double_t)((float64_t)DL_EDIT_DEADLINE_NS * 1.0e-6f64));
        }
        if (deadlines) {
            CHECK(seen_ns[shift] >= 0 && seen_ns[shift] <= DL_EDIT_DEADLINE_NS);
        }
    }
    game_report(game);
    game_close(game);
    dl_rig_end(t, &rig);
}

TEST_MAIN()
