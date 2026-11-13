/* ctest render_feature_lod: golden pictures, the cost on real terrain and the built program. Here and not in index.c to keep that file
 * under the 1500 lines where plans/CODING_STANDARD.md section 5 asks for a split. Included once, by index.c, after everything these
 * name. */
#ifndef MC_TESTS_CUDA_LOD_FIXTURES_REST_H
#define MC_TESTS_CUDA_LOD_FIXTURES_REST_H

/* ---- Golden pictures ------------------------------------------------------------------------------------------------------------- */

static void ld_golden_compare(test_context_t *t, const char *path, const uint8_t *picture) {
    const uint64_t capacity = display_png_bytes_needed(LD_WIDTH, LD_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    if (getenv("MC_GOLDEN_WRITE") != nullptr) {
        uint64_t written = 0u;
        REQUIRE(display_png_encode(picture, LD_WIDTH * 4u, LD_WIDTH, LD_HEIGHT, file, capacity, &written));
        REQUIRE(file_write_atomic(path, file, written) == FILE_OK);
        printf("  wrote %s\n", path);
        return;
    }
    uint64_t length = 0u;
    if (!CHECK_EQ_U64(file_read_whole(path, file, capacity, &length), FILE_OK)) {
        printf("  cannot read %s: run with MC_GOLDEN_WRITE=1, then look at what it wrote\n", path);
        return;
    }
    png_info_t info;
    png_error_t error;
    REQUIRE(png_probe(file, length, &info, &error) == PNG_OK);
    REQUIRE(info.width == LD_WIDTH && info.height == LD_HEIGHT);
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    REQUIRE(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK);
    uint64_t wrong = 0u, exact = 0u;
    for (uint32_t y = 0u; y < LD_HEIGHT; y++) {
        for (uint32_t x = 0u; x < LD_WIDTH; x++) {
            const uint64_t i = (uint64_t)y * LD_WIDTH + x;
            const uint32_t ours = sk_pixel(picture, LD_WIDTH, x, y);
            const uint32_t golden = post_xrgb(rgba[i * 4u], rgba[i * 4u + 1u], rgba[i * 4u + 2u]);
            wrong += sk_bytes_near(ours, golden, LD_GOLDEN_TOLERANCE) ? 0u : 1u;
            exact += ours == golden ? 1u : 0u;
        }
    }
    printf("  %s: %" PRIu64 " of %" PRIu64 " pixels exact, %" PRIu64 " past the tolerance\n", path, exact,
           (uint64_t)LD_PIXELS, wrong);
    CHECK_EQ_U64(wrong, 0u);
}

/* Ground a test works out, and real terrain, each with the far field and with what the setting leaves. */
TEST(golden_pictures_match) {
    sk_rig_t rig;
    const ld_source_t source = {LD_GROUND_HILLS, {0}};
    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 8.5f64, 170.5f64, 0.3f32, -0.12f32);
    REQUIRE(ld_begin(t, &rig, &source, LD_HAND_RADIUS, 64u, 950.0f32, SHADE_FEATURES_ALL, eye.position));
    render_frame_t *const frame = &rig.render->frame;
    render_frame_time_set(frame, 0.33f32);
    render_frame_features_set(frame, SHADE_FEATURES_PLAY);
    const uint8_t *picture = nullptr;
    for (uint32_t i = 0u; i < LD_GOLDEN_FRAMES; i++) {
        picture = sk_draw(t, &rig, &eye);
        REQUIRE(picture != nullptr);
    }
    ld_golden_compare(t, "tests/cuda/lod/fixtures/hills.png", picture);
    render_frame_features_set(frame, LD_PLAIN);
    picture = sk_draw(t, &rig, &eye);
    REQUIRE(picture != nullptr);
    ld_golden_compare(t, "tests/cuda/lod/fixtures/hills_plain.png", picture);
    render_frame_features_set(frame, LD_PLAIN & ~(uint32_t)SHADE_FEATURE_LOD);
    picture = sk_draw(t, &rig, &eye);
    REQUIRE(picture != nullptr);
    ld_golden_compare(t, "tests/cuda/lod/fixtures/hills_off.png", picture);
    ld_end(t, &rig);

    /* The world's own ground and its far field together: what a game opens, near the eye. */
    sk_rig_t real;
    REQUIRE(sk_rig_begin(t, &real));
    const game_config_t config = {.render_distance = 5u,
                                  .width = LD_WIDTH,
                                  .height = LD_HEIGHT,
                                  .features_off = 0u,
                                  .god_rays = 0u,
                                  .show_date = 0u,
                                  .block_radius_given = 2u};
    REQUIRE(game_open(real.game, &config));
    REQUIRE(game_settle(real.game));
    render_camera_t real_eye = real.game->camera;
    real_eye.position[2] += 20.0f64;
    /* The ground at the start is low now, and -0.2 was nearly all sky; on plan 007 phase 03's land the start is on a slope
     * falling away from the eye, and -0.45 was nearly all sky again, over a few crowns. */
    real_eye.pitch = -0.75f32;
    render_frame_time_set(&real.render->frame, 0.38f32);
    for (uint32_t i = 0u; i < LD_GOLDEN_FRAMES; i++) {
        picture = sk_draw(t, &real, &real_eye);
        REQUIRE(picture != nullptr);
    }
    ld_golden_compare(t, "tests/cuda/lod/fixtures/real_near.png", picture);
    game_close(real.game);
    sk_rig_end(t, &real);

    /* Real terrain out to where the far levels reach: what R = 512 looks like, in the levels that grow nothing, which a debug build can
     * make in this test's time. */
    sk_rig_t distant;
    REQUIRE(sk_rig_begin(t, &distant));
    REQUIRE(sk_hand_world_open(t, &distant, LD_HAND_RADIUS, LD_WIDTH, LD_HEIGHT, 8176.0f32));
    REQUIRE(sk_hand_world_sync(t, &distant));
    const render_lod_plan_t plan = {.shifts = {4u, 5u, 6u}, .radii = {16u, 16u, 11u}, .level_count = 3u};
    REQUIRE(render_open_lod(distant.render, &plan, 0u, render_lod_sample_terrain, distant.terrain) == GPU_STATUS_OK);
    render_frame_features_set(&distant.render->frame, SHADE_FEATURES_PLAY);
    render_frame_time_set(&distant.render->frame, 0.38f32);
    render_camera_t distant_eye;
    /* Over every peak: at the 420 this first had, the middle of the world is inside a range now, and the eye saw only rock. */
    render_camera_start(&distant_eye, 8.5f64, 8.5f64, 1240.5f64, 0.7f32, -0.12f32);
    REQUIRE(ld_settle(t, &distant, distant_eye.position));
    for (uint32_t i = 0u; i < LD_GOLDEN_FRAMES; i++) {
        picture = sk_draw(t, &distant, &distant_eye);
        REQUIRE(picture != nullptr);
    }
    ld_golden_compare(t, "tests/cuda/lod/fixtures/real_far.png", picture);
    render_close_lod(distant.render);
    ld_end(t, &distant);
}

/* ---- The cost ----------------------------------------------------------------------------------------------------------------- */

typedef struct ld_cost_t {
    float64_t primary_ms; /* 0 */
    float64_t shade_ms;   /* 8 */
    float64_t post_ms;    /* 16 */
    float64_t total_ms;   /* 24 */
} ld_cost_t;
static_assert(sizeof(ld_cost_t) == 32);

static void ld_cost_row(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera, const char *name,
                        uint32_t features) {
    render_frame_t *const frame = &rig->render->frame;
    render_frame_features_set(frame, features);
    for (uint32_t i = 0u; i < 4u; i++) {
        REQUIRE(sk_draw(t, rig, camera) != nullptr);
    }
    const render_frame_stats_t before = frame->stats;
    for (uint32_t i = 0u; i < LD_COST_FRAMES; i++) {
        REQUIRE(sk_draw(t, rig, camera) != nullptr);
    }
    const render_frame_stats_t *const after = &frame->stats;
    const float64_t frames = (float64_t)LD_COST_FRAMES;
    ld_cost_t cost = {.primary_ms = (after->sum_ms[FRAME_PASS_PRIMARY] - before.sum_ms[FRAME_PASS_PRIMARY]) / frames,
                      .shade_ms = (after->sum_ms[FRAME_PASS_SHADE] - before.sum_ms[FRAME_PASS_SHADE]) / frames,
                      .post_ms = (after->sum_ms[FRAME_PASS_POST] - before.sum_ms[FRAME_PASS_POST]) / frames,
                      .total_ms = (after->sum_total_ms - before.sum_total_ms) / frames};
    printf("  cost, %-34s: primary %7.3f ms, shade %7.3f ms, post %7.3f ms, frame %7.3f ms\n", name,
           (libc_double_t)cost.primary_ms, (libc_double_t)cost.shade_ms, (libc_double_t)cost.post_ms,
           (libc_double_t)cost.total_ms);
    CHECK(cost.total_ms > 0.0f64);
}

/* All that a far field costs at R=512 on real terrain, at 1080p: nothing here is a limit, and the numbers go in the stage file. Only a release
 * build measures anything worth keeping, and a debug build's terrain would take all of this test's time. */
TEST(the_cost_at_a_render_distance_of_512_is_measured_on_real_terrain) {
#ifndef NDEBUG
    printf("  cost: not on a build with no optimisation: see tests/cuda/lod/README.md\n");
    return;
#endif
    const uint32_t render_distance = MC_GAME_RENDER_DISTANCE_MAX;
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = {.render_distance = render_distance,
                                  .width = LD_COST_WIDTH,
                                  .height = LD_COST_HEIGHT,
                                  .features_off = 0u,
                                  .god_rays = 0u,
                                  .show_date = 0u};
    REQUIRE(game_open(game, &config));
    const int64_t start_ns = time_now_ns();
    REQUIRE(game_settle(game));
    const float64_t seconds = time_ns_to_seconds(time_now_ns() - start_ns);
    render_lod_t *const lod = rig.render->borrowed_lod;
    REQUIRE(lod != nullptr);
    printf(
        "  cost, R=%" PRIu32 ": %" PRIu32 " levels, %" PRIu64
        " columns, ready in %.1f s, %.1f s of workers' time, %.0f us per sample\n",
        render_distance, lod->level_count, lod->stats.columns_built, (libc_double_t)seconds,
        (libc_double_t)((float64_t)lod->stats.build_ns * 1e-9f64),
        (libc_double_t)((float64_t)lod->stats.build_ns * 1e-3f64 / ((float64_t)lod->stats.columns_built * 256.0f64)));
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        const render_lod_level_t *const level = &lod->levels[i];
        printf("  cost, level of %3" PRIu32 " blocks, %4" PRIu32 " columns, %5" PRIu32
               " sections, %7.1f MiB of slabs, of a pool of %.0f MiB: %.1f KiB a column\n",
               (uint32_t)(1u << level->shift), level->column_count, level->buffers.section_count,
               (libc_double_t)((float64_t)level->buffers.slab_bytes / 1048576.0f64),
               (libc_double_t)((float64_t)level->buffers.pool_bytes / 1048576.0f64),
               (libc_double_t)((float64_t)level->buffers.slab_bytes / 1024.0f64 / (float64_t)level->column_count));
    }
    CHECK_EQ_U64(lod->stats.columns_no_room, 0u);
    const device_memory_pool_t *const pool = &rig.bridge->device_memory.pools[DEVICE_POOL_VOXELS];
    printf("  cost, voxels pool: %.0f MiB taken of a budget of %.0f MiB\n",
           (libc_double_t)((float64_t)pool->live_bytes / 1048576.0f64),
           (libc_double_t)((float64_t)pool->limit_bytes / 1048576.0f64));
    /* The whole card's share must stay under what a test may hold. */
    CHECK(pool->live_bytes < 3ull << 30);

    render_camera_t level_eye = game->camera;
    level_eye.position[2] += 40.0f64;
    level_eye.pitch = -0.03f32;
    render_camera_t down_eye = game->camera;
    down_eye.pitch = -0.35f32;
    render_frame_time_set(&rig.render->frame, (float32_t)GAME_DAY_START);
    const uint32_t on = SHADE_FEATURES_PLAY, off = on & ~(uint32_t)SHADE_FEATURE_LOD;
    ld_cost_row(t, &rig, &level_eye, "at the horizon, no far field", off);
    ld_cost_row(t, &rig, &level_eye, "at the horizon, far field", on);
    ld_cost_row(t, &rig, &level_eye, "at the horizon, no sun rays", on & ~(uint32_t)SHADE_FEATURE_SHADOW);
    ld_cost_row(t, &rig, &down_eye, "looking down, no far field", off);
    ld_cost_row(t, &rig, &down_eye, "looking down, far field", on);
    game_close(game);
    sk_rig_end(t, &rig);
}

/* ---- The real threads ------------------------------------------------------------------------------------------------------- */

/* What a real run does: the gpu thread builds it all with the sim going on beside it, and goes on doing so with the eye flying. */
TEST(the_real_threads_build_the_far_field_and_keep_up_with_an_eye_that_flies) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = {.render_distance = 5u,
                                  .width = LD_WIDTH,
                                  .height = LD_HEIGHT,
                                  .features_off = 0u,
                                  .god_rays = 0u,
                                  .show_date = 0u,
                                  .block_radius_given = 2u};
    REQUIRE(game_open(game, &config));
    const uint64_t bytes = post_xrgb_bytes(LD_WIDTH, LD_HEIGHT);
    uint8_t *const screen = sk_aligned(t, bytes);
    display_buffer_t target = {
        .owned_map = screen, .map_bytes = bytes, .pitch = LD_WIDTH * 4u, .width = LD_WIDTH, .height = LD_HEIGHT};
    REQUIRE(game_loop_start(game, nullptr));
    const render_lod_t *const lod = rig.render->borrowed_lod;
    const uint64_t wanted = lod->levels[0].column_count;
    const int64_t started_ns = time_now_ns();
    const render_fly_input_t still = {0};
    /* The gpu thread's number, read from outside it: this only decides when to stop looking. */
    while (lod->stats.columns_built < wanted) {
        REQUIRE(game_loop_frame(game, &still, 1.0f32 / 120.0f32, &target));
        REQUIRE(time_now_ns() - started_ns < 200ll * TIME_NS_PER_SECOND);
        time_sleep_ns(8333333);
    }
    const render_fly_input_t ahead = {.forward = 1, .fast = 1u};
    const int64_t flying_ns = time_now_ns();
    /* Past a second of flight, and until a new column has come of it, however long a slow build takes to make one. */
    while (time_now_ns() - flying_ns < TIME_NS_PER_SECOND || lod->stats.columns_built <= wanted) {
        REQUIRE(game_loop_frame(game, &ahead, 1.0f32 / 120.0f32, &target));
        REQUIRE(time_now_ns() - started_ns < 240ll * TIME_NS_PER_SECOND);
        time_sleep_ns(8333333);
    }
    REQUIRE(game_loop_stop(game));
    printf("  %" PRIu64 " columns built, %" PRIu64 " dropped, and %" PRIu64 " pictures shown\n",
           lod->stats.columns_built, lod->stats.columns_dropped, game->pictures_shown);
    CHECK(lod->stats.columns_built > wanted);
    CHECK_EQ_U64(lod->stats.columns_no_room, 0u);
    CHECK(game->pictures_shown > 0u);
    game_close(game);
    sk_rig_end(t, &rig);
}

/* ---- The built program ------------------------------------------------------------------------------------------------------ */

typedef struct ld_temp_dir_t {
    char path[64]; /* 0 */
} ld_temp_dir_t;
static_assert(sizeof(ld_temp_dir_t) == 64);

static bool ld_temp_dir_make(ld_temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_render_lod_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void ld_temp_dir_remove(const ld_temp_dir_t *dir) {
    DIR *const handle = opendir(dir->path);
    if (handle != nullptr) {
        char path[512];
        for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
            if (entry->d_name[0] != '.') {
                snprintf(path, sizeof(path), "%s/%s", dir->path, entry->d_name);
                unlink(path);
            }
        }
        closedir(handle);
    }
    rmdir(dir->path);
}

static const char ld_script[] = "3 screenshot far.png\n"
                                "4 end\n";

static void ld_program_run(test_context_t *t, const ld_temp_dir_t *dir, const char *script, const char *flag,
                           test_process_t *out) {
    char *argv[] = {
        t->borrowed_args[0], (char *)"--headless", (char *)"--test-world", (char *)"--no-taa", (char *)"--no-ao",
        (char *)"--render-distance",
        /* Level 0 is never held under 12 chunks (block_radius_min_chunks), so the far field starts past it. */
        (char *)"14", (char *)"--script", (char *)script, (char *)"--screenshot-dir", (char *)dir->path,
        (char *)"--width", (char *)"160", (char *)"--height", (char *)"90", (char *)flag, nullptr};
    test_process_run(t, argv, LD_PROGRAM_TIMEOUT_MS, out);
}

static uint8_t *ld_png_pixels(test_context_t *t, const ld_temp_dir_t *dir, const char *name) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir->path, name);
    const uint64_t capacity = 16ull << 20;
    uint8_t *const bytes = test_alloc(t, capacity);
    uint64_t length = 0u;
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, capacity, &length), FILE_OK)) {
        return nullptr;
    }
    png_info_t info;
    png_error_t error;
    if (!CHECK(png_probe(bytes, length, &info, &error) == PNG_OK) || !CHECK(info.width == 160u && info.height == 90u)) {
        return nullptr;
    }
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    if (!CHECK(png_decode(bytes, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK)) {
        return nullptr;
    }
    return rgba;
}

/* The program, by a script: the far field on real terrain, with its flags, and the same bytes each time. */
TEST(the_program_draws_the_far_field_and_its_flags_mean_what_they_say) {
    ld_temp_dir_t on, again, off;
    REQUIRE(ld_temp_dir_make(&on));
    REQUIRE(ld_temp_dir_make(&again));
    REQUIRE(ld_temp_dir_make(&off));
    char script[128];
    snprintf(script, sizeof(script), "%s/script.txt", on.path);
    REQUIRE(file_write_atomic(script, ld_script, sizeof(ld_script) - 1u) == FILE_OK);
    test_process_t run;
    /* A flag repeated changes nothing, which is how the runs with no other flag take the same arguments. */
    ld_program_run(t, &on, script, "--no-taa", &run);
    REQUIRE(run.started != 0u);
    REQUIRE(CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK));
    CHECK_CONTAINS(run.output, "far field");
    ld_program_run(t, &again, script, "--no-taa", &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    ld_program_run(t, &off, script, "--no-lod", &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    const uint8_t *const first = ld_png_pixels(t, &on, "far.png");
    const uint8_t *const second = ld_png_pixels(t, &again, "far.png");
    const uint8_t *const none = ld_png_pixels(t, &off, "far.png");
    REQUIRE(first != nullptr && second != nullptr && none != nullptr);
    CHECK_MEM_EQ(first, second, 160u * 90u * 4u);
    uint64_t differ = 0u;
    for (uint64_t i = 0u; i < 160ull * 90ull; i++) {
        differ += memcmp(first + i * 4u, none + i * 4u, 3u) != 0 ? 1u : 0u;
    }
    printf("  the far field changes %" PRIu64 " pixels of the program's picture\n", differ);
    CHECK(differ > 100u);
    ld_temp_dir_remove(&on);
    ld_temp_dir_remove(&again);
    ld_temp_dir_remove(&off);

    /* A flag plan 009 phase 04 took away is no flag at all, and the one left is nothing without a world. */
    char *refused[] = {t->borrowed_args[0], (char *)"--full-detail", (char *)"8", nullptr};
    test_process_run(t, refused, LD_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    char *off_alone[] = {t->borrowed_args[0], (char *)"--no-lod", nullptr};
    test_process_run(t, off_alone, LD_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
}

#endif
