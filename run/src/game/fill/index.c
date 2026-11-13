/* See README.md. How long the world takes to fill in around the eye. Allocates nothing. */
#include "game/fill/index.h"

#include "log/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

/* Ring r holds the columns with (r - 1)^2 < dx^2 + dy^2 <= r^2. */
static uint32_t fill_ring_of(int64_t dx, int64_t dy) {
    const int64_t d2 = dx * dx + dy * dy;
    int64_t ring = (int64_t)ceil(sqrt((float64_t)d2));
    /* sqrt of a whole square can land a hair either side. */
    while (ring > 0 && (ring - 1) * (ring - 1) >= d2) {
        ring--;
    }
    while (ring * ring < d2) {
        ring++;
    }
    return ring >= (int64_t)GAME_FILL_RINGS ? GAME_FILL_RINGS - 1u : (uint32_t)ring;
}

static uint32_t fill_rings(const game_fill_t *fill) {
    return fill->block_radius + 1u < GAME_FILL_RINGS ? fill->block_radius + 1u : GAME_FILL_RINGS;
}

/* -1 for never, so that a log never says -0.00. */
static float64_t fill_seconds(int64_t ns) {
    return ns < 0 ? -1.0f64 : (float64_t)ns / (float64_t)TIME_NS_PER_SECOND;
}

enum : int64_t {
    FILL_SCAN_GAP_NS = 10000000, /* the table is looked at no more often than this */
};

void game_fill_reset(game_fill_t *fill) {
    memset(fill, 0, sizeof(*fill));
    atomic_store_explicit(&fill->first_ns, -1, memory_order_relaxed);
    atomic_store_explicit(&fill->ring_near_ns, -1, memory_order_relaxed);
    atomic_store_explicit(&fill->near_ns, -1, memory_order_relaxed);
    atomic_store_explicit(&fill->far_ns, -1, memory_order_relaxed);
    atomic_store_explicit(&fill->done_ns, -1, memory_order_relaxed);
    fill->logged = 1u; /* nothing going on to say */
}

/* Columns on the card now, counted per ring into ready, and in all. */
static uint32_t fill_count(const game_fill_t *fill, const render_t *render, uint16_t ready[GAME_FILL_RINGS]) {
    memset(ready, 0, GAME_FILL_RINGS * sizeof(uint16_t));
    const world_buffers_t *const buffers = &render->buffers;
    if (buffers->borrowed_shadow == nullptr) {
        return 0u;
    }
    /* A jump's rings are round where it went: until the window has moved there, what it holds round its old middle is not the fill's. */
    const int64_t center_x =
        fill->has_centre != 0u ? (int64_t)fill->centre_x : (int64_t)buffers->window.min_x + buffers->window.side / 2u;
    const int64_t center_y =
        fill->has_centre != 0u ? (int64_t)fill->centre_y : (int64_t)buffers->window.min_y + buffers->window.side / 2u;
    const int64_t limit = (int64_t)fill->block_radius * fill->block_radius;
    uint32_t total = 0u;
    for (uint32_t slot = 0u; slot < buffers->window.slot_count; slot++) {
        const world_buffers_column_t *const column = &buffers->borrowed_shadow[slot];
        if (column->ready == 0u) {
            continue;
        }
        total++;
        const int64_t dx = (int64_t)column->chunk_x - center_x;
        const int64_t dy = (int64_t)column->chunk_y - center_y;
        if (dx * dx + dy * dy <= limit) {
            const uint32_t ring = fill_ring_of(dx, dy);
            ready[ring] = (uint16_t)(ready[ring] + 1u);
        }
    }
    return total;
}

static uint64_t fill_far_built(const render_t *render) {
    return render->borrowed_lod != nullptr ? render->borrowed_lod->stats.columns_built : 0u;
}

static void fill_keep(game_fill_t *fill, const game_fill_summary_t *summary) {
    fill->history[fill->history_count % GAME_FILL_HISTORY] = *summary;
    fill->history_count++;
}

void game_fill_start(game_fill_t *fill, game_fill_kind_t kind, uint32_t render_distance, uint32_t block_radius,
                     const render_t *render) {
    if (fill->logged == 0u && atomic_load_explicit(&fill->started_ns, memory_order_relaxed) != 0) {
        /* Cut short by this one: kept as it got, never done. */
        game_fill_summary_t cut;
        game_fill_summary(fill, &cut);
        cut.far_columns = fill->far_built_last - fill->far_built_start; /* the far field may be a new one now */
        fill_keep(fill, &cut);
    }
    fill->kind = kind;
    fill->has_centre = 0u; /* a jump sets its own after this */
    fill->render_distance = render_distance;
    fill->block_radius = block_radius;
    fill->has_far = render->borrowed_lod != nullptr && render->borrowed_lod->level_count != 0u ? 1u : 0u;
    fill->far_built_start = fill_far_built(render);
    fill->far_built_last = fill->far_built_start;
    fill->rings_done = 0u;
    fill->logged = 0u;
    for (uint32_t ring = 0u; ring < GAME_FILL_RINGS; ring++) {
        fill->ring_ns[ring] = -1;
        fill->ring_columns[ring] = 0u;
    }
    const int32_t reach = (int32_t)block_radius;
    const int64_t limit = (int64_t)reach * reach;
    for (int32_t dy = -reach; dy <= reach; dy++) {
        for (int32_t dx = -reach; dx <= reach; dx++) {
            if ((int64_t)dx * dx + (int64_t)dy * dy <= limit) {
                const uint32_t ring = fill_ring_of(dx, dy);
                fill->ring_columns[ring] = (uint16_t)(fill->ring_columns[ring] + 1u);
            }
        }
    }
    uint16_t ready[GAME_FILL_RINGS];
    fill->ready_start = fill_count(fill, render, ready);
    atomic_store_explicit(&fill->first_ns, -1, memory_order_relaxed);
    atomic_store_explicit(&fill->ring_near_ns, -1, memory_order_relaxed);
    atomic_store_explicit(&fill->near_ns, -1, memory_order_relaxed);
    atomic_store_explicit(&fill->far_ns, fill->has_far != 0u ? -1 : 0, memory_order_relaxed);
    atomic_store_explicit(&fill->done_ns, -1, memory_order_relaxed);
    atomic_fetch_add_explicit(&fill->sequence, 1u, memory_order_release);
    atomic_store_explicit(&fill->started_ns, time_now_ns(), memory_order_relaxed);
    fill->scanned_ns = 0;
}

void game_fill_jump_ask(game_fill_t *fill, int32_t chunk_x, int32_t chunk_y) {
    atomic_store_explicit(&fill->jump_chunk_x, chunk_x, memory_order_relaxed);
    atomic_store_explicit(&fill->jump_chunk_y, chunk_y, memory_order_relaxed);
    atomic_store_explicit(&fill->jump_asked, 1u, memory_order_release);
}

/* A jump asked for since the last step: a fill of its own, round where it went, from now. */
static void fill_jump_take(game_fill_t *fill, const render_t *render) {
    if (atomic_load_explicit(&fill->jump_asked, memory_order_relaxed) == 0u ||
        atomic_exchange_explicit(&fill->jump_asked, 0u, memory_order_acquire) == 0u) {
        return;
    }
    const int32_t chunk_x = atomic_load_explicit(&fill->jump_chunk_x, memory_order_relaxed);
    const int32_t chunk_y = atomic_load_explicit(&fill->jump_chunk_y, memory_order_relaxed);
    /* What the window held at the start is never round where it went, so the count at the start is taken round there too. */
    fill->has_centre = 1u;
    fill->centre_x = chunk_x;
    fill->centre_y = chunk_y;
    game_fill_start(fill, GAME_FILL_JUMP, fill->render_distance, fill->block_radius, render);
    fill->has_centre = 1u;
    fill->centre_x = chunk_x;
    fill->centre_y = chunk_y;
    uint16_t ready[GAME_FILL_RINGS];
    fill->ready_start = fill_count(fill, render, ready);
    LOG_INFO("fill: a jump to chunk %" PRId32 ", %" PRId32 ", timed from now", chunk_x, chunk_y);
}

void game_fill_summary(const game_fill_t *fill, game_fill_summary_t *out) {
    memset(out, 0, sizeof(*out));
    out->first_ns = atomic_load_explicit(&fill->first_ns, memory_order_acquire);
    out->ring_near_ns = atomic_load_explicit(&fill->ring_near_ns, memory_order_acquire);
    out->near_ns = atomic_load_explicit(&fill->near_ns, memory_order_acquire);
    out->far_ns = atomic_load_explicit(&fill->far_ns, memory_order_acquire);
    out->done_ns = atomic_load_explicit(&fill->done_ns, memory_order_acquire);
    out->render_distance = fill->render_distance;
    out->block_radius = fill->block_radius;
    out->kind = fill->kind;
}

static const char *fill_kind_text(game_fill_kind_t kind) {
    return kind == GAME_FILL_OPEN ? "opening at" : (kind == GAME_FILL_JUMP ? "a jump at" : "a change to");
}

static void fill_finish(game_fill_t *fill, const render_t *render, int64_t now_ns) {
    atomic_store_explicit(&fill->done_ns, now_ns, memory_order_release);
    game_fill_summary_t summary;
    game_fill_summary(fill, &summary);
    summary.far_columns = fill_far_built(render) - fill->far_built_start;
    fill_keep(fill, &summary);
    LOG_INFO("fill: after %s render distance %" PRIu32 ", %" PRIu32
             " block by block: the first change drawn at %.2f s, the %" PRIu32 " nearest rings at %.2f s, all %" PRIu32
             " at %.2f s, the far field (%" PRIu64 " columns) at %.2f s, all of it at %.2f s",
             fill_kind_text(fill->kind), fill->render_distance, fill->block_radius,
             (libc_double_t)fill_seconds(summary.first_ns),
             (fill->block_radius < GAME_FILL_NEAR_RING ? fill->block_radius : GAME_FILL_NEAR_RING) + 1u,
             (libc_double_t)fill_seconds(summary.ring_near_ns), fill_rings(fill),
             (libc_double_t)fill_seconds(summary.near_ns), summary.far_columns,
             (libc_double_t)fill_seconds(summary.far_ns), (libc_double_t)fill_seconds(now_ns));
    fill->logged = 1u;
}

void game_fill_step(game_fill_t *fill, const render_t *render) {
    fill_jump_take(fill, render);
    const int64_t started_ns = atomic_load_explicit(&fill->started_ns, memory_order_relaxed);
    if (fill->logged != 0u || started_ns == 0) {
        return;
    }
    const int64_t now_ns = time_now_ns() - started_ns;
    const uint64_t far_built = fill_far_built(render);
    fill->far_built_last = far_built;
    if (atomic_load_explicit(&fill->first_ns, memory_order_relaxed) < 0 && far_built > fill->far_built_start) {
        atomic_store_explicit(&fill->first_ns, now_ns, memory_order_release);
    }
    if (fill->has_far != 0u && atomic_load_explicit(&fill->far_ns, memory_order_relaxed) < 0 &&
        render->borrowed_lod != nullptr && render_lod_idle(render->borrowed_lod)) {
        atomic_store_explicit(&fill->far_ns, now_ns, memory_order_release);
    }
    const uint32_t rings = fill_rings(fill);
    /* Only between views is every section of what the table holds on the card. Once the far field is done, every time: that may be the
     * last look, as a run with no threads stops when all is on the card. */
    const bool far_done = atomic_load_explicit(&fill->far_ns, memory_order_relaxed) >= 0;
    if (fill->rings_done < rings && render_upload_idle(&render->upload) &&
        (far_done || fill->scanned_ns == 0 || now_ns - fill->scanned_ns >= FILL_SCAN_GAP_NS)) {
        fill->scanned_ns = now_ns;
        uint16_t ready[GAME_FILL_RINGS];
        const uint32_t total = fill_count(fill, render, ready);
        if (atomic_load_explicit(&fill->first_ns, memory_order_relaxed) < 0 && total != fill->ready_start) {
            atomic_store_explicit(&fill->first_ns, now_ns, memory_order_release);
        }
        for (uint32_t ring = 0u; ring < rings; ring++) {
            if (fill->ring_ns[ring] < 0 && ready[ring] >= fill->ring_columns[ring]) {
                fill->ring_ns[ring] = now_ns;
                LOG_INFO("fill: ring %" PRIu32 " of %" PRIu32 ", %" PRIu32 " columns, drawn at %.2f s", ring,
                         rings - 1u, (uint32_t)fill->ring_columns[ring], (libc_double_t)fill_seconds(now_ns));
            }
        }
        while (fill->rings_done < rings && fill->ring_ns[fill->rings_done] >= 0) {
            fill->rings_done++;
        }
        const uint32_t near_rings =
            (fill->block_radius < GAME_FILL_NEAR_RING ? fill->block_radius : GAME_FILL_NEAR_RING) + 1u;
        if (atomic_load_explicit(&fill->ring_near_ns, memory_order_relaxed) < 0 && fill->rings_done >= near_rings) {
            atomic_store_explicit(&fill->ring_near_ns, now_ns, memory_order_release);
        }
        if (fill->rings_done == rings) {
            atomic_store_explicit(&fill->near_ns, now_ns, memory_order_release);
        }
    }
    if (fill->rings_done == rings && atomic_load_explicit(&fill->far_ns, memory_order_relaxed) >= 0) {
        if (atomic_load_explicit(&fill->first_ns, memory_order_relaxed) < 0) {
            atomic_store_explicit(&fill->first_ns, now_ns, memory_order_release); /* nothing to change: done at once */
        }
        fill_finish(fill, render, now_ns);
    }
}

static void fill_report_line(const char *state, const game_fill_summary_t *summary) {
    LOG_INFO("fill: %s %s render distance %" PRIu32 ", %" PRIu32
             " block by block: first change %.2f s, near rings %.2f s, all rings %.2f s, far field %.2f s (%" PRIu64
             " columns), all %.2f s",
             state, fill_kind_text(summary->kind), summary->render_distance, summary->block_radius,
             (libc_double_t)fill_seconds(summary->first_ns), (libc_double_t)fill_seconds(summary->ring_near_ns),
             (libc_double_t)fill_seconds(summary->near_ns), (libc_double_t)fill_seconds(summary->far_ns),
             summary->far_columns, (libc_double_t)fill_seconds(summary->done_ns));
}

void game_fill_report(const game_fill_t *fill) {
    const uint32_t kept = fill->history_count < GAME_FILL_HISTORY ? fill->history_count : GAME_FILL_HISTORY;
    LOG_INFO("fill: %" PRIu32 " fills finished, the last %" PRIu32 " follow; -1.00 s is never", fill->history_count,
             kept);
    for (uint32_t i = fill->history_count - kept; i < fill->history_count; i++) {
        fill_report_line("finished", &fill->history[i % GAME_FILL_HISTORY]);
    }
    if (fill->logged == 0u && atomic_load_explicit(&fill->started_ns, memory_order_relaxed) != 0) {
        game_fill_summary_t summary;
        game_fill_summary(fill, &summary);
        summary.far_columns = fill->far_built_last - fill->far_built_start;
        fill_report_line("unfinished", &summary);
    }
    LOG_INFO("fill: a column waited for its climate tiles in %" PRIu64 " sim steps",
             atomic_load_explicit(&fill->climate_waits, memory_order_relaxed));
}
