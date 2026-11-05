/* ctest render_feature_entities: golden pictures, the cost on real terrain and the built program. Here and not in index.c to keep that
 * file under the 1500 lines where plans/CODING_STANDARD.md section 5 asks for a split. Included once, by index.c, after everything
 * these name. */
#ifndef MC_TESTS_CUDA_ENTITY_RAYS_FIXTURES_REST_H
#define MC_TESTS_CUDA_ENTITY_RAYS_FIXTURES_REST_H

/* ---- Golden pictures ------------------------------------------------------------------------------------------------------------- */

static void en_golden_compare(test_context_t *t, const char *path, const uint8_t *picture) {
    const uint64_t capacity = display_png_bytes_needed(EN_WIDTH, EN_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    if (getenv("MC_GOLDEN_WRITE") != nullptr) {
        uint64_t written = 0u;
        REQUIRE(display_png_encode(picture, EN_WIDTH * 4u, EN_WIDTH, EN_HEIGHT, file, capacity, &written));
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
    REQUIRE(info.width == EN_WIDTH && info.height == EN_HEIGHT);
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    REQUIRE(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK);
    uint64_t wrong = 0u, exact = 0u;
    for (uint32_t y = 0u; y < EN_HEIGHT; y++) {
        for (uint32_t x = 0u; x < EN_WIDTH; x++) {
            const uint64_t i = (uint64_t)y * EN_WIDTH + x;
            const uint32_t ours = sk_pixel(picture, EN_WIDTH, x, y);
            const uint32_t golden = post_xrgb(rgba[i * 4u], rgba[i * 4u + 1u], rgba[i * 4u + 2u]);
            wrong += sk_bytes_near(ours, golden, EN_GOLDEN_TOLERANCE) ? 0u : 1u;
            exact += ours == golden ? 1u : 0u;
        }
    }
    printf("  %s: %" PRIu64 " of %" PRIu64 " pixels exact, %" PRIu64 " past the tolerance\n", path, exact,
           (uint64_t)EN_PIXELS, wrong);
    CHECK_EQ_U64(wrong, 0u);
}

typedef struct en_golden_row_t {
    float64_t x;      /* 0 */
    float64_t y;      /* 8 */
    float64_t z;      /* 16 */
    float32_t half;   /* 24 */
    float32_t yaw;    /* 28 */
    uint16_t block;   /* 32 */
    uint8_t stored;   /* 34 */
    uint8_t _pad0[5]; /* 35 */
} en_golden_row_t;
static_assert(sizeof(en_golden_row_t) == 40);

typedef struct en_golden_t {
    const char *path;  /* 0 */
    uint32_t features; /* 8 */
    uint8_t _pad0[4];  /* 12 */
} en_golden_t;
static_assert(sizeof(en_golden_t) == 16);

TEST(golden_pictures_match) {
    sk_rig_t rig;
    REQUIRE(en_begin(t, &rig, SHADE_FEATURES_PLAY));
    /* A block to aim at and to mine, a low wall, and a pane. */
    REQUIRE(en_put(t, &rig, 12, 6, -1, 13, 7, 0, BLOCK_ID_DIRT));
    REQUIRE(en_put(t, &rig, 14, 2, -1, 15, 12, 1, BLOCK_ID_OAK_PLANKS));
    REQUIRE(en_put(t, &rig, 10, 8, -1, 11, 13, 3, BLOCK_ID_GLASS));
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_frame_t *const frame = &rig.render->frame;
    render_frame_time_set(frame, 0.31f32);
    render_entities_t *const entities = rig.render->borrowed_entities;

    uint32_t coal = 0u;
    for (uint32_t i = 0u; i < BLOCK_DROP_ITEM_COUNT; i++) {
        coal = strcmp(block_drop_items[i].name, "coal") == 0 ? i : coal;
    }
    render_entities_begin(entities);
    static const en_golden_row_t rows[] = {
        {11.5f64, 3.5f64, 0.3f64, 0.3f32, 0.0f32, BLOCK_ID_GRASS_BLOCK, 0xF0u, {0}},
        {12.5f64, 4.5f64, -0.75f64, 0.25f32, 0.6f32, BLOCK_ID_OAK_LOG, 0xF0u, {0}},
        {13.0f64, 8.0f64, -0.5f64, 0.5f32, 0.3f32, BLOCK_ID_STONE, 0xF0u, {0}},
        {12.2f64, 9.5f64, 0.2f64, 0.5f32, 1.1f32, BLOCK_ID_DIRT, 0x80u, {0}}, /* behind the pane */
        {16.0f64, 7.0f64, 1.5f64, 0.5f32, 0.9f32, BLOCK_ID_OAK_LEAVES, 0xF0u, {0}}};
    for (uint32_t i = 0u; i < sizeof(rows) / sizeof(rows[0]); i++) {
        render_entities_item_t row = en_item(rows[i].x, rows[i].y, rows[i].z, rows[i].half, rows[i].block, rows[i].yaw);
        row.stored = rows[i].stored;
        render_entities_add(entities, &row);
    }
    const float64_t coal_place[3] = {11.0f64, 6.5f64, -0.8f64};
    render_entities_add_item_drop(entities, (uint16_t)coal, coal_place, 0.4f32, 7u, 0xF0u);
    render_entities_target_t target = en_target_of(12, 6, -1);
    target.crack_cell[0] = 12;
    target.crack_cell[1] = 6;
    target.crack_cell[2] = -1;
    target.crack_frame = 7u;
    render_entities_target_set(entities, &target);
    render_entities_publish(entities);

    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 3.0f64, 2.5f64, 0.45f32, -0.42f32);
    const en_golden_t goldens[] = {
        {"tests/cuda/entity_rays/fixtures/play.png", SHADE_FEATURES_PLAY, {0}},
        {"tests/cuda/entity_rays/fixtures/plain.png", EN_PLAIN, {0}},
        {"tests/cuda/entity_rays/fixtures/no_boxes.png", SHADE_FEATURES_PLAY & ~(uint32_t)SHADE_FEATURE_ENTITIES, {0}},
        {"tests/cuda/entity_rays/fixtures/no_outline.png", SHADE_FEATURES_PLAY & ~(uint32_t)SHADE_FEATURE_OUTLINE, {0}},
    };
    for (uint32_t g = 0u; g < sizeof(goldens) / sizeof(goldens[0]); g++) {
        render_frame_features_set(frame, goldens[g].features);
        const uint8_t *picture = nullptr;
        for (uint32_t i = 0u; i < EN_GOLDEN_FRAMES; i++) {
            picture = sk_draw(t, &rig, &eye);
            REQUIRE(picture != nullptr);
        }
        en_golden_compare(t, goldens[g].path, picture);
    }
    en_end(t, &rig);
}

/* ---- The cost ----------------------------------------------------------------------------------------------------------------- */

typedef struct en_cost_t {
    float64_t primary_ms; /* 0 */
    float64_t shade_ms;   /* 8 */
    float64_t post_ms;    /* 16 */
    float64_t total_ms;   /* 24 */
    float64_t lists_ms;   /* 32: on the host's clock */
} en_cost_t;
static_assert(sizeof(en_cost_t) == 40);

static void en_cost_measure(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera, uint32_t features,
                            en_cost_t *out) {
    render_frame_t *const frame = &rig->render->frame;
    render_frame_features_set(frame, features);
    for (uint32_t i = 0u; i < 4u; i++) {
        REQUIRE(sk_draw(t, rig, camera) != nullptr);
    }
    const render_frame_stats_t before = frame->stats;
    for (uint32_t i = 0u; i < EN_COST_FRAMES; i++) {
        REQUIRE(sk_draw(t, rig, camera) != nullptr);
    }
    const render_frame_stats_t *const after = &frame->stats;
    const float64_t frames = (float64_t)EN_COST_FRAMES;
    out->primary_ms = (after->sum_ms[FRAME_PASS_PRIMARY] - before.sum_ms[FRAME_PASS_PRIMARY]) / frames;
    out->shade_ms = (after->sum_ms[FRAME_PASS_SHADE] - before.sum_ms[FRAME_PASS_SHADE]) / frames;
    out->post_ms = (after->sum_ms[FRAME_PASS_POST] - before.sum_ms[FRAME_PASS_POST]) / frames;
    out->total_ms = (after->sum_total_ms - before.sum_total_ms) / frames;
    traverse_camera_t view;
    render_camera_to_traverse(camera, frame->width, frame->height, &view);
    const int64_t start = time_now_ns();
    for (uint32_t i = 0u; i < EN_COST_FRAMES; i++) {
        entity_rays_scene_t scene;
        REQUIRE(render_entities_frame(rig->render->borrowed_entities, &view, features, frame->fog_end, &scene) ==
                GPU_STATUS_OK);
    }
    out->lists_ms = time_ns_to_seconds(time_now_ns() - start) * 1000.0f64 / frames;
}

static void en_cost_row(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera, const char *name,
                        uint32_t features) {
    en_cost_t cost = {0};
    en_cost_measure(t, rig, camera, features, &cost);
    printf(
        "  cost, %-34s: primary %7.3f ms, shade %7.3f ms, post %7.3f ms, frame %7.3f ms, lists on the host %6.3f ms\n",
        name, (libc_double_t)cost.primary_ms, (libc_double_t)cost.shade_ms, (libc_double_t)cost.post_ms,
        (libc_double_t)cost.total_ms, (libc_double_t)cost.lists_ms);
    CHECK(cost.total_ms > 0.0f64);
}

/* Boxes on real terrain, seen, near and far; each row beside one with the setting off. Nothing here is a limit: the numbers go in the
 * stage file. */
TEST(the_cost_is_measured_on_real_terrain) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = {.render_distance = EN_COST_RENDER_DISTANCE,
                                  .width = EN_COST_WIDTH,
                                  .height = EN_COST_HEIGHT,
                                  .features_off = 0u,
                                  .god_rays = 0u,
                                  .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    render_camera_t camera = game->camera;
    camera.pitch = -0.35f32;
    render_frame_time_set(&rig.render->frame, (float32_t)GAME_DAY_START);
    const uint32_t off = SHADE_FEATURES_PLAY & ~(uint32_t)(SHADE_FEATURE_ENTITIES | SHADE_FEATURE_OUTLINE);
    en_send(&rig, nullptr, 0u, nullptr);
    en_cost_row(t, &rig, &camera, "the settings off", off);
    en_cost_row(t, &rig, &camera, "on, and nothing to draw", SHADE_FEATURES_PLAY);
    const render_entities_target_t target = {
        .box_min = {camera.position[0] + 2.0f64, camera.position[1] + 1.5f64, camera.position[2] - 4.0f64},
        .box_max = {camera.position[0] + 3.0f64, camera.position[1] + 2.5f64, camera.position[2] - 3.0f64},
        .outline = 1u};
    en_send(&rig, nullptr, 0u, &target);
    en_cost_row(t, &rig, &camera, "an outline", SHADE_FEATURES_PLAY);
    en_rng_t rng;
    en_rng_seed(&rng, 2024u);
    const uint32_t counts[] = {16u, 256u, 4096u};
    const char *const names[] = {"16 items around the eye", "256 items around the eye",
                                 "4096 items in the view distance"};
    render_entities_item_t *const items = test_alloc(t, 4096u * sizeof(render_entities_item_t));
    for (uint32_t c = 0u; c < 3u; c++) {
        const float64_t reach = c == 2u ? 80.0f64 : 16.0f64;
        for (uint32_t i = 0u; i < counts[c]; i++) {
            items[i] = en_item(camera.position[0] + (en_rng_next(&rng) - 0.5f64) * reach * 2.0f64,
                               camera.position[1] + (en_rng_next(&rng) - 0.5f64) * reach * 2.0f64,
                               camera.position[2] + (en_rng_next(&rng) - 0.7f64) * 16.0f64, 0.125f32, BLOCK_ID_DIRT,
                               (float32_t)(en_rng_next(&rng) * 6.28f64));
        }
        en_send(&rig, items, counts[c], &target);
        en_cost_row(t, &rig, &camera, names[c], SHADE_FEATURES_PLAY);
    }
    /* A wall of them right in front: the worst a tile's list comes to. */
    for (uint32_t i = 0u; i < 256u; i++) {
        items[i] = en_item(camera.position[0] + 2.0f64 + cos((float64_t)camera.yaw) * 1.0f64,
                           camera.position[1] + sin((float64_t)camera.yaw) * 1.0f64,
                           camera.position[2] - 1.0f64 + (float64_t)i * 0.002f64, 0.5f32, BLOCK_ID_DIRT, 0.0f32);
    }
    en_send(&rig, items, 256u, &target);
    en_cost_row(t, &rig, &camera, "256 in one heap at arm's length", SHADE_FEATURES_PLAY);
    game_close(game);
    sk_rig_end(t, &rig);
}

/* ---- The built program ------------------------------------------------------------------------------------------------------ */

typedef struct en_temp_dir_t {
    char path[64]; /* 0 */
} en_temp_dir_t;
static_assert(sizeof(en_temp_dir_t) == 64);

static bool en_temp_dir_make(en_temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_render_ent_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void en_temp_dir_remove(const en_temp_dir_t *dir) {
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

/* On seed 11, whose start is on grass: looks straight down, digs the grass underfoot, and looks: a crack half way, and dirt lying in the hole after. */
static const char en_script[] = "1 move 0 500\n"
                                "5 screenshot before.png\n"
                                "6 button left down\n"
                                "16 screenshot mining.png\n"
                                "26 button left up\n"
                                "28 screenshot hole.png\n"
                                "29 end\n";

static void en_program_run(test_context_t *t, const en_temp_dir_t *dir, const char *script, const char *flag,
                           test_process_t *out) {
    char *argv[] = {t->borrowed_args[0],
                    (char *)"--headless",
                    (char *)"--test-world",
                    (char *)"--seed",
                    (char *)"11",
                    (char *)"--walk",
                    (char *)"--no-taa",
                    (char *)"--no-ao",
                    (char *)"--render-distance",
                    (char *)"3",
                    (char *)"--script",
                    (char *)script,
                    (char *)"--screenshot-dir",
                    (char *)dir->path,
                    (char *)"--width",
                    (char *)"160",
                    (char *)"--height",
                    (char *)"90",
                    (char *)flag,
                    nullptr};
    test_process_run(t, argv, EN_PROGRAM_TIMEOUT_MS, out);
}

static uint8_t *en_png_read(test_context_t *t, const en_temp_dir_t *dir, const char *name, uint64_t *out_length) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir->path, name);
    const uint64_t capacity = 16ull << 20;
    uint8_t *const bytes = test_alloc(t, capacity);
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, capacity, out_length), FILE_OK)) {
        *out_length = 0u;
    }
    return bytes;
}

static uint8_t *en_png_pixels(test_context_t *t, const en_temp_dir_t *dir, const char *name) {
    uint64_t length = 0u;
    const uint8_t *const file = en_png_read(t, dir, name, &length);
    png_info_t info;
    png_error_t error;
    if (!CHECK(png_probe(file, length, &info, &error) == PNG_OK) || !CHECK(info.width == 160u && info.height == 90u)) {
        return nullptr;
    }
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    if (!CHECK(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK)) {
        return nullptr;
    }
    return rgba;
}

static uint64_t en_png_differs(const uint8_t *a, const uint8_t *b) {
    uint64_t differs = 0u;
    for (uint64_t i = 0u; i < 160ull * 90ull; i++) {
        differs += memcmp(a + i * 4u, b + i * 4u, 3u) != 0 ? 1u : 0u;
    }
    return differs;
}

/* What a real run does with the sim's own items and aim, from a script: the settings there, off and on, and the same bytes each time. */
TEST(the_program_shows_the_dirt_it_dropped_the_crack_and_the_outline) {
    en_temp_dir_t on, again, no_boxes, no_mark;
    REQUIRE(en_temp_dir_make(&on));
    REQUIRE(en_temp_dir_make(&again));
    REQUIRE(en_temp_dir_make(&no_boxes));
    REQUIRE(en_temp_dir_make(&no_mark));
    char script[128];
    snprintf(script, sizeof(script), "%s/dig.script", on.path);
    REQUIRE(file_write_atomic(script, en_script, sizeof(en_script) - 1u) == FILE_OK);
    test_process_t run;
    en_program_run(t, &on, script, "--no-sky", &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, "1 items dropped, 0 picked up, 1 on the ground in 1 stacks");
    CHECK_CONTAINS(run.output, "0 bytes live");
    en_program_run(t, &again, script, "--no-sky", &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    const char *const shots[] = {"before.png", "mining.png", "hole.png"};
    for (uint32_t s = 0u; s < 3u; s++) {
        uint64_t a_length = 0u, b_length = 0u;
        const uint8_t *const a = en_png_read(t, &on, shots[s], &a_length);
        const uint8_t *const b = en_png_read(t, &again, shots[s], &b_length);
        CHECK_EQ_U64(a_length, b_length);
        CHECK_MEM_EQ(a, b, a_length);
    }
    char *const none_argv[] = {t->borrowed_args[0],
                               (char *)"--headless",
                               (char *)"--test-world",
                               (char *)"--seed",
                               (char *)"11",
                               (char *)"--walk",
                               (char *)"--no-taa",
                               (char *)"--no-ao",
                               (char *)"--no-sky",
                               (char *)"--no-entities",
                               (char *)"--render-distance",
                               (char *)"3",
                               (char *)"--script",
                               script,
                               (char *)"--screenshot-dir",
                               no_boxes.path,
                               (char *)"--width",
                               (char *)"160",
                               (char *)"--height",
                               (char *)"90",
                               nullptr};
    test_process_run(t, none_argv, EN_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    char *const line_argv[] = {t->borrowed_args[0],
                               (char *)"--headless",
                               (char *)"--test-world",
                               (char *)"--seed",
                               (char *)"11",
                               (char *)"--walk",
                               (char *)"--no-taa",
                               (char *)"--no-ao",
                               (char *)"--no-sky",
                               (char *)"--no-outline",
                               (char *)"--render-distance",
                               (char *)"3",
                               (char *)"--script",
                               script,
                               (char *)"--screenshot-dir",
                               no_mark.path,
                               (char *)"--width",
                               (char *)"160",
                               (char *)"--height",
                               (char *)"90",
                               nullptr};
    test_process_run(t, line_argv, EN_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    const uint8_t *const before = en_png_pixels(t, &on, "before.png");
    const uint8_t *const mining = en_png_pixels(t, &on, "mining.png");
    const uint8_t *const hole = en_png_pixels(t, &on, "hole.png");
    const uint8_t *const hole_no_boxes = en_png_pixels(t, &no_boxes, "hole.png");
    const uint8_t *const before_no_boxes = en_png_pixels(t, &no_boxes, "before.png");
    const uint8_t *const before_no_mark = en_png_pixels(t, &no_mark, "before.png");
    const uint8_t *const mining_no_mark = en_png_pixels(t, &no_mark, "mining.png");
    REQUIRE(before != nullptr && mining != nullptr && hole != nullptr && hole_no_boxes != nullptr &&
            before_no_boxes != nullptr && before_no_mark != nullptr && mining_no_mark != nullptr);
    /* With nothing dropped yet the setting changes nothing, and with dirt in the hole it shows where the dirt is. */
    const uint64_t boxes_before = en_png_differs(before, before_no_boxes),
                   boxes_hole = en_png_differs(hole, hole_no_boxes);
    const uint64_t mark_before = en_png_differs(before, before_no_mark),
                   mark_mining = en_png_differs(mining, mining_no_mark);
    printf("  --no-entities: %" PRIu64 " pixels before anything fell, %" PRIu64 " with dirt in the hole\n",
           boxes_before, boxes_hole);
    printf("  --no-outline: %" PRIu64 " pixels of outline before, and %" PRIu64 " of outline and crack while mining\n",
           mark_before, mark_mining);
    CHECK_EQ_U64(boxes_before, 0u);
    CHECK(boxes_hole > 10u);
    CHECK(mark_before > 10u);
    /* The crack is more than the lines. */
    CHECK(mark_mining > mark_before);
    en_temp_dir_remove(&on);
    en_temp_dir_remove(&again);
    en_temp_dir_remove(&no_boxes);
    en_temp_dir_remove(&no_mark);
}

TEST(the_program_takes_the_settings_and_refuses_them_with_no_world) {
    const char *const flags[] = {"--no-entities", "--no-outline"};
    for (uint32_t f = 0u; f < 2u; f++) {
        char *const alone[] = {t->borrowed_args[0], (char *)"--headless", (char *)"--ticks",
                               (char *)"1",         (char *)flags[f],     nullptr};
        test_process_t run;
        test_process_run(t, alone, EN_PROGRAM_TIMEOUT_MS, &run);
        REQUIRE(run.started != 0u);
        CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
        CHECK_CONTAINS(run.output, "only mean something with --test-world");
    }
}

#endif
