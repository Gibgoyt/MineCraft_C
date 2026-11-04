/* ctest render_feature_sky: the built program, from script to file. Here and not in index.c to keep that file under the 1500 lines where
 * plans/CODING_STANDARD.md section 5 asks for a split. Included once, by index.c, after everything these name. */
#ifndef MC_TESTS_CUDA_SKY_FIXTURES_PROGRAM_H
#define MC_TESTS_CUDA_SKY_FIXTURES_PROGRAM_H

/* How many pixels two PNGs of one size have apart, and how many of those by more than one step of a byte. */
static void sk_png_compare(test_context_t *t, const uint8_t *a, uint64_t a_bytes, const uint8_t *b, uint64_t b_bytes,
                           uint32_t *out_off, uint32_t *out_far_off) {
    *out_off = UINT32_MAX;
    *out_far_off = UINT32_MAX;
    png_info_t info_a, info_b;
    png_error_t error;
    REQUIRE(png_probe(a, a_bytes, &info_a, &error) == PNG_OK);
    REQUIRE(png_probe(b, b_bytes, &info_b, &error) == PNG_OK);
    REQUIRE(info_a.width == info_b.width && info_a.height == info_b.height);
    uint8_t *const rgba_a = test_alloc(t, info_a.pixel_bytes);
    uint8_t *const rgba_b = test_alloc(t, info_b.pixel_bytes);
    uint8_t *const work = test_alloc(t, info_a.work_bytes);
    REQUIRE(png_decode(a, a_bytes, rgba_a, info_a.pixel_bytes, work, info_a.work_bytes, &info_a, &error) == PNG_OK);
    REQUIRE(png_decode(b, b_bytes, rgba_b, info_b.pixel_bytes, work, info_b.work_bytes, &info_b, &error) == PNG_OK);
    uint32_t off = 0u, far_off = 0u;
    const uint64_t pixels = (uint64_t)info_a.width * info_a.height;
    for (uint64_t i = 0u; i < pixels; i++) {
        const uint32_t pixel_a = post_xrgb(rgba_a[i * 4u], rgba_a[i * 4u + 1u], rgba_a[i * 4u + 2u]);
        const uint32_t pixel_b = post_xrgb(rgba_b[i * 4u], rgba_b[i * 4u + 1u], rgba_b[i * 4u + 2u]);
        off += pixel_a != pixel_b ? 1u : 0u;
        far_off += sk_bytes_near(pixel_a, pixel_b, 1u) ? 0u : 1u;
    }
    *out_off = off;
    *out_far_off = far_off;
}

typedef struct sk_temp_dir_t {
    char path[64]; /* 0 */
} sk_temp_dir_t;
static_assert(sizeof(sk_temp_dir_t) == 64);

static bool sk_temp_dir_make(sk_temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_render_sky_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void sk_temp_dir_remove(const sk_temp_dir_t *dir) {
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

/* Looks down a little at the world, then again a good while later, past the first light. */
enum : uint32_t { SK_FROZEN_PIXELS_MOST = 8u };

static const char sk_script[] = "0 move 0 120\n"
                                "2 screenshot early.png\n"
                                "200 screenshot late.png\n"
                                "201 end\n";

static void sk_program_run(test_context_t *t, const sk_temp_dir_t *dir, const char *script, const char *flag,
                           test_process_t *out) {
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--test-world",
                          (char *)"--render-distance",
                          (char *)"4",
                          (char *)"--time-of-day",
                          (char *)"0.26",
                          (char *)"--script",
                          (char *)script,
                          (char *)"--screenshot-dir",
                          (char *)dir->path,
                          (char *)"--width",
                          (char *)"320",
                          (char *)"--height",
                          (char *)"180",
                          (char *)"--no-taa",
                          (char *)"--no-ao",
                          (char *)flag,
                          nullptr};
    test_process_run(t, argv, SK_PROGRAM_TIMEOUT_MS, out);
}

static uint8_t *sk_program_picture(test_context_t *t, const sk_temp_dir_t *dir, const char *name,
                                   uint64_t *out_length) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir->path, name);
    const uint64_t capacity = display_png_bytes_needed(320u, 180u);
    uint8_t *const bytes = test_alloc(t, capacity);
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, capacity, out_length), FILE_OK)) {
        *out_length = 0u;
    }
    return bytes;
}

TEST(the_program_takes_the_settings_and_they_change_the_picture) {
    sk_temp_dir_t base;
    REQUIRE(sk_temp_dir_make(&base));
    char script[128];
    snprintf(script, sizeof(script), "%s/view.script", base.path);
    REQUIRE(file_write_atomic(script, sk_script, sizeof(sk_script) - 1u) == FILE_OK);
    test_process_t run;
    sk_program_run(t, &base, script, nullptr, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    uint64_t base_early_bytes = 0u, base_late_bytes = 0u;
    const uint8_t *const base_early = sk_program_picture(t, &base, "early.png", &base_early_bytes);
    const uint8_t *const base_late = sk_program_picture(t, &base, "late.png", &base_late_bytes);
    REQUIRE(base_early_bytes != 0u && base_late_bytes != 0u);
    /* The sun moved. */
    CHECK(base_early_bytes != base_late_bytes || memcmp(base_early, base_late, base_early_bytes) != 0);

    /* One script gives one set of pictures. */
    sk_temp_dir_t again;
    REQUIRE(sk_temp_dir_make(&again));
    sk_program_run(t, &again, script, nullptr, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    uint64_t again_bytes = 0u;
    const uint8_t *const again_late = sk_program_picture(t, &again, "late.png", &again_bytes);
    CHECK_EQ_U64(again_bytes, base_late_bytes);
    CHECK(again_bytes == base_late_bytes && memcmp(again_late, base_late, again_bytes) == 0);
    sk_temp_dir_remove(&again);

    /* With the day cycle off, the two are the same; and at the first picture, it changes nothing yet to speak of. */
    sk_temp_dir_t frozen;
    REQUIRE(sk_temp_dir_make(&frozen));
    sk_program_run(t, &frozen, script, "--no-day-cycle", &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    uint64_t frozen_early_bytes = 0u, frozen_late_bytes = 0u;
    const uint8_t *const frozen_early = sk_program_picture(t, &frozen, "early.png", &frozen_early_bytes);
    const uint8_t *const frozen_late = sk_program_picture(t, &frozen, "late.png", &frozen_late_bytes);
    /* The clock moving changes most of a picture: see the sun moved, above. What the rest of the game does in 198 ticks may touch a pixel or a few,
     * as a block's own tick can show on the ground. */
    uint32_t frozen_off = 0u, frozen_far_off = 0u;
    sk_png_compare(t, frozen_early, frozen_early_bytes, frozen_late, frozen_late_bytes, &frozen_off, &frozen_far_off);
    printf("  with the day cycle off, %" PRIu32 " pixels moved between the two, %" PRIu32 " by more than a step\n",
           frozen_off, frozen_far_off);
    CHECK(frozen_off <= SK_FROZEN_PIXELS_MOST);
    CHECK(frozen_far_off <= SK_FROZEN_PIXELS_MOST);
    uint32_t moved_off = 0u, moved_far_off = 0u;
    sk_png_compare(t, base_early, base_early_bytes, base_late, base_late_bytes, &moved_off, &moved_far_off);
    CHECK(moved_far_off > SK_FROZEN_PIXELS_MOST);
    sk_temp_dir_remove(&frozen);

    const char *const flags[] = {"--no-sky", "--no-fog", "--no-shadows"};
    for (uint32_t f = 0u; f < 3u; f++) {
        sk_temp_dir_t dir;
        REQUIRE(sk_temp_dir_make(&dir));
        sk_program_run(t, &dir, script, flags[f], &run);
        CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
        uint64_t bytes = 0u;
        const uint8_t *const picture = sk_program_picture(t, &dir, "late.png", &bytes);
        if (!CHECK(bytes != base_late_bytes || memcmp(picture, base_late, bytes) != 0)) {
            printf("  %s changed nothing\n", flags[f]);
        }
        sk_temp_dir_remove(&dir);
    }
    /* Taken and run. What it changes on real terrain waits for Stage 11.1 to store some: a floor with it is above. */
    sk_temp_dir_t open_sky;
    REQUIRE(sk_temp_dir_make(&open_sky));
    sk_program_run(t, &open_sky, script, "--no-stored-light", &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    sk_temp_dir_remove(&open_sky);
    sk_temp_dir_remove(&base);

    /* They mean nothing without a world to draw, and a time must be one. */
    char *const alone[] = {t->borrowed_args[0], (char *)"--headless", (char *)"--ticks",
                           (char *)"1",         (char *)"--no-sky",   nullptr};
    test_process_run(t, alone, SK_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "only mean something with --test-world");
    char *const time_alone[] = {t->borrowed_args[0],
                                (char *)"--headless",
                                (char *)"--ticks",
                                (char *)"1",
                                (char *)"--time-of-day",
                                (char *)"0.5",
                                nullptr};
    test_process_run(t, time_alone, SK_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "only means something with --test-world");
    char *const bad_time[] = {t->borrowed_args[0], (char *)"--headless",    (char *)"--test-world", (char *)"--ticks",
                              (char *)"1",         (char *)"--time-of-day", (char *)"noon",         nullptr};
    test_process_run(t, bad_time, SK_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "--time-of-day takes a number from 0 to 1");
    char *const late_time[] = {t->borrowed_args[0], (char *)"--headless",    (char *)"--test-world", (char *)"--ticks",
                               (char *)"1",         (char *)"--time-of-day", (char *)"1.5",          nullptr};
    test_process_run(t, late_time, SK_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
}

#endif
