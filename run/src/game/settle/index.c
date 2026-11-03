/* See README.md. */
#include "game/index.h"

#include "log/index.h"
#include "time/index.h"

/* A run with no threads waits this long for a worker. */
enum : int64_t { GAME_SETTLE_IDLE_NS = 50000 };

/* Whether every section the eye's place wants is on the card, or as much of it as the pool will take. */
static bool game_is_settled(const game_t *game) {
    const world_t *const world = game->borrowed_world;
    const render_t *const render = game->borrowed_render;
    const world_edit_snapshot_t *const snapshot = &world->snapshot;
    const bool unread = snapshot->published_count != snapshot->acquired_count + snapshot->overwritten_count;
    const bool sections_waiting =
        world->edit.queue_count[WORLD_EDIT_CONSUMER_RENDER] != 0u && render->buffers.pool_full_count == 0u;
    return game->stream_quiet != 0u && world->map.pending_count == 0u && !unread && !sections_waiting &&
           render_upload_idle(&render->upload) &&
           (render->borrowed_lod == nullptr || render_lod_idle(render->borrowed_lod));
}

bool game_settle(game_t *game) {
    render_t *const render = game->borrowed_render;
    for (;;) {
        const uint32_t waiting_before = game->jobs_in_use;
        game_sim_step(game);
        const gpu_status_t status = render_upload_frame(&render->upload);
        if (status != GPU_STATUS_OK) {
            gpu_error_log(&render->buffers.last_error);
            return false;
        }
        const uint64_t far_built = render->borrowed_lod != nullptr ? render->borrowed_lod->stats.columns_built : 0u;
        if (render->borrowed_lod != nullptr &&
            render_lod_frame(render->borrowed_lod, game->camera.position) != GPU_STATUS_OK) {
            return false; /* it has said why */
        }
        if (render->buffers.pool_full_count != 0u && game->pool_full_said == 0u) {
            LOG_WARN("game: the section pool is full, so part of the world will not be drawn");
            game->pool_full_said = 1u;
        }
        if (game_is_settled(game)) {
            return true;
        }
        const bool far_waiting = render->borrowed_lod != nullptr && render->borrowed_lod->jobs_in_use != 0u &&
                                 far_built == render->borrowed_lod->stats.columns_built;
        if (far_waiting || (waiting_before != 0u && waiting_before == game->jobs_in_use)) {
            time_sleep_ns(GAME_SETTLE_IDLE_NS);
        }
    }
}
