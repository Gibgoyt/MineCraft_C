/* ctest render_feature_sky: everything a real run has, on memory of its own, and a world with nothing in it but what a test puts
 * there. Here and not in index.c to keep that file under the 1500 lines where plans/CODING_STANDARD.md section 5 asks for a split.
 * Included once, by index.c, after everything these name. */
#ifndef MC_TESTS_CUDA_SKY_FIXTURES_RIG_H
#define MC_TESTS_CUDA_SKY_FIXTURES_RIG_H

/* ---- A rig: everything a real run has, on memory of its own ----------------------------------------------------------- */

typedef struct sk_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
} sk_rig_t;
static_assert(sizeof(sk_rig_t) == 64);

static void *sk_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool sk_rig_begin(test_context_t *t, sk_rig_t *rig) {
    rig->bridge = sk_aligned(t, sizeof(gpu_bridge_t));
    rig->job = sk_aligned(t, sizeof(job_system_t));
    rig->world = sk_aligned(t, sizeof(world_t));
    rig->terrain = sk_aligned(t, sizeof(terrain_t));
    rig->render = sk_aligned(t, sizeof(render_t));
    rig->texture = sk_aligned(t, sizeof(texture_t));
    rig->game = sk_aligned(t, sizeof(game_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    return true;
}

static void sk_rig_end(test_context_t *t, sk_rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

/* Every column of a table empty but there, and nothing else: so that all a picture holds is what a test put in it. */
static bool sk_hand_world_open(test_context_t *t, sk_rig_t *rig, uint32_t radius, uint32_t width, uint32_t height,
                               float32_t fog_end) {
    world_t *const world = rig->world;
    render_t *const render = rig->render;
    if (!CHECK(world_open(world, radius, 0, 0))) {
        return false;
    }
    const render_upload_budget_t budget = {.bytes = RENDER_UPLOAD_BUDGET_BYTES_DEFAULT,
                                           .sections = RENDER_UPLOAD_BUDGET_SECTIONS_DEFAULT};
    if (!CHECK_EQ_U64(render_open(render, &world->snapshot, RENDER_POOL_BYTES_MIN, budget), GPU_STATUS_OK) ||
        !CHECK_EQ_U64(render_open_frame(render, rig->texture, width, height, fog_end), GPU_STATUS_OK)) {
        return false;
    }
    world_map_request_t request;
    while (world_map_stream_take(&world->map, &request, 1u) == 1u) {
        if (!CHECK(world_map_stream_commit(&world->map, &request) != nullptr)) {
            return false;
        }
        world_edit_column_filled(&world->edit, request.slot);
    }
    return true;
}

static void sk_hand_world_close(sk_rig_t *rig) {
    render_close(rig->render);
    world_close(rig->world);
}

/* Publishes, and pumps until the card has it all. */
static bool sk_hand_world_sync(test_context_t *t, sk_rig_t *rig) {
    world_t *const world = rig->world;
    const world_edit_snapshot_t *const snapshot = &world->snapshot;
    for (uint32_t pass = 0u; pass < 1000000u; pass++) {
        (void)render_upload_resend_drain(&rig->render->upload, &world->edit);
        (void)world_edit_snapshot_publish(&world->snapshot);
        if (!CHECK_EQ_U64(render_upload_frame(&rig->render->upload), GPU_STATUS_OK)) {
            return false;
        }
        if (snapshot->published_count == snapshot->acquired_count + snapshot->overwritten_count &&
            world->edit.queue_count[WORLD_EDIT_CONSUMER_RENDER] == 0u && render_upload_idle(&rig->render->upload)) {
            return true;
        }
    }
    return CHECK(false);
}

static const uint8_t *sk_draw(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera) {
    render_t *const render = rig->render;
    const gpu_status_t status = render_frame_draw(&render->frame, &render->buffers, camera);
    if (!CHECK_EQ_U64(status, GPU_STATUS_OK)) {
        gpu_error_log(&render->frame.gpu.last_error);
        return nullptr;
    }
    const uint8_t *const picture = render_frame_take(&render->frame);
    CHECK(picture != nullptr);
    return picture;
}

static uint32_t sk_pixel(const uint8_t *picture, uint32_t width, uint32_t x, uint32_t y) {
    uint32_t value = 0u;
    memcpy(&value, picture + ((uint64_t)y * width + x) * 4u, 4u);
    return value & 0xFFFFFFu;
}

static bool sk_bytes_near(uint32_t a, uint32_t b, uint32_t tolerance) {
    for (uint32_t shift = 0u; shift < 24u; shift += 8u) {
        const int32_t gap = (int32_t)((a >> shift) & 255u) - (int32_t)((b >> shift) & 255u);
        if ((uint32_t)abs(gap) > tolerance) {
            return false;
        }
    }
    return true;
}

#endif
