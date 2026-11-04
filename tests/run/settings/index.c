/* ctest run_settings. See README.md. */
#include "harness/index.h"

#include "run/display/mode/fixtures/fake_card.h"

#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "game/view/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "png/index.h"
#include "settings/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    PATH_BYTES = 256u,
    SHEET_WIDTH = 640u, /* scale 2 */
    SHEET_HEIGHT = 480u,
};

/* The layout at 640 by 480, worked out by hand from the numbers at the top of run/src/ui/screen_settings/index.c. */
enum : int32_t {
    AT_MIDDLE_X = 320,
    AT_ROW_0_Y = 68, /* the first row starts at 48, and a row is 40 tall */
    AT_ROW_STEP = 48,
    AT_SLIDER_LEFT_X = 120,
    AT_SLIDER_RIGHT_X = 519,
    AT_VSYNC_X = 130,
    AT_COORDINATES_X = 300, /* 120 + 2 * (46 + 4) is where it starts, and 2 * 82 how wide */
    AT_EFFECTS_X = 420,
    AT_ROW_6_Y = 356,
    AT_DONE_Y = 452,
    AT_EFFECT_LEFT_X = 100,
    AT_EFFECT_RIGHT_X = 340,
};

static void *aligned_alloc_64(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static char *temp_directory(test_context_t *t) {
    char *const directory = test_alloc(t, PATH_BYTES);
    snprintf(directory, PATH_BYTES, "/tmp/mc_settings_XXXXXX");
    return mkdtemp(directory);
}

static void temp_directory_remove(test_context_t *t, char *directory) {
    if (directory == nullptr || strncmp(directory, "/tmp/mc_settings_", 17u) != 0) {
        return;
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", directory, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static char *path_join(test_context_t *t, const char *left, const char *right) {
    char *const path = test_alloc(t, PATH_BYTES);
    snprintf(path, PATH_BYTES, "%s/%s", left, right);
    return path;
}

static const uint8_t *file_slurp(test_context_t *t, const char *path, uint64_t *out_bytes) {
    uint64_t size = 0u;
    if (file_path_size(path, &size) != FILE_OK) {
        return nullptr;
    }
    uint8_t *const bytes = test_alloc(t, size + 1u);
    if (file_read_whole(path, bytes, size + 1u, out_bytes) != FILE_OK) {
        return nullptr;
    }
    return bytes;
}

static uint64_t count_of(const char *text, const char *needle) {
    uint64_t count = 0u;
    const uint64_t length = strlen(needle);
    for (const char *at = strstr(text, needle); at != nullptr; at = strstr(at + length, needle)) {
        count++;
    }
    return count;
}

/* ---- What is kept ---------------------------------------------------------------------------------------------------- */

static settings_values_t values_defaults(void) {
    settings_values_t values;
    settings_persist_defaults(&values);
    return values;
}

static settings_persist_report_t parse_text(const char *text, settings_values_t *values) {
    settings_persist_report_t report;
    settings_persist_parse((const uint8_t *)text, strlen(text), values, &report);
    return report;
}

TEST(the_defaults_are_the_plans_and_every_effect_has_its_own_bit) {
    const settings_values_t values = values_defaults();
    CHECK_EQ_U64(values.render_distance, 32u);
    CHECK_EQ_U64(values.full_detail, 32u);
    CHECK_EQ_U64(values.fov_degrees, 70u);
    CHECK_EQ_U64(values.sensitivity_percent, 100u);
    CHECK_EQ_U64(values.render_scale_percent, 100u);
    CHECK_EQ_U64(values.worker_count, MC_THREADS_WORKERS);
    CHECK_EQ_U64(values.effects, SHADE_FEATURES_PLAY);
    CHECK_EQ_U64(values.vsync, 1u);
    CHECK_EQ_U64(values.show_coordinates, 1u);
    CHECK_EQ_U64(SETTINGS_RENDER_DISTANCE_MIN, 2u);
    CHECK_EQ_U64(SETTINGS_RENDER_DISTANCE_MAX, 512u);
    uint32_t seen = 0u;
    for (uint32_t i = 0u; i < SETTINGS_EFFECT_COUNT; i++) {
        const settings_effect_t *const effect = settings_persist_effect(i);
        CHECK_EQ_U64(seen & effect->bit, 0u);
        CHECK_EQ_U64(effect->bit & (effect->bit - 1u), 0u);
        seen |= effect->bit;
    }
    /* All of them: nothing a frame can draw with has no setting. */
    CHECK_EQ_U64(seen, SHADE_FEATURES_EVERY);
}

TEST(the_file_for_the_defaults_is_exactly_this) {
    static const char expected[] = "version: 1\n"
                                   "# minecraft_c's settings: what the Settings screen wrote. A line that is wrong is\n"
                                   "# told about at the start and ignored.\n"
                                   "render_distance: 32\n"
                                   "full_detail: 32\n"
                                   "fov_degrees: 70\n"
                                   "sensitivity_percent: 100\n"
                                   "render_scale_percent: 100\n"
                                   "worker_count: 16\n"
                                   "vsync: true\n"
                                   "show_coordinates: true\n"
                                   "effect_shapes: true\n"
                                   "effect_alpha: true\n"
                                   "effect_sky: true\n"
                                   "effect_day_cycle: true\n"
                                   "effect_fog: true\n"
                                   "effect_shadows: true\n"
                                   "effect_stored_light: true\n"
                                   "effect_transparency: true\n"
                                   "effect_water_reflection: true\n"
                                   "effect_water_tint: true\n"
                                   "effect_ao: true\n"
                                   "effect_taa: true\n"
                                   "effect_god_rays: false\n"
                                   "effect_entities: true\n"
                                   "effect_outline: true\n"
                                   "effect_far_field: true\n";
    static_assert(MC_THREADS_WORKERS == 16);
    const settings_values_t values = values_defaults();
    uint8_t text[SETTINGS_TEXT_BYTES];
    uint64_t bytes = 0u;
    REQUIRE(settings_persist_write(&values, text, sizeof(text), &bytes));
    REQUIRE(bytes == sizeof(expected) - 1u);
    CHECK_MEM_EQ(text, expected, bytes);
    /* Too small a place is said, at every size. */
    for (uint64_t capacity = 0u; capacity < bytes; capacity++) {
        uint64_t ignored = 0u;
        CHECK(!settings_persist_write(&values, text, capacity, &ignored));
    }
}

TEST(any_values_written_read_back_the_same) {
    uint64_t seed = 0x5EEDu;
    for (uint32_t round = 0u; round < 20000u; round++) {
        uint32_t random[8];
        for (uint32_t i = 0u; i < 8u; i++) {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            random[i] = (uint32_t)(seed >> 32u);
        }
        settings_values_t values = {.render_distance = random[0],
                                    .full_detail = random[1],
                                    .fov_degrees = random[2],
                                    .sensitivity_percent = random[3],
                                    .render_scale_percent = random[4],
                                    .worker_count = random[5],
                                    .effects = random[6],
                                    .vsync = (uint8_t)(random[7] & 1u),
                                    .show_coordinates = (uint8_t)((random[7] >> 1u) & 1u),
                                    ._pad0 = {0}};
        settings_persist_clamp(&values);
        CHECK(values.render_distance >= 2u && values.render_distance <= 512u);
        uint8_t text[SETTINGS_TEXT_BYTES];
        uint64_t bytes = 0u;
        REQUIRE(settings_persist_write(&values, text, sizeof(text), &bytes));
        /* Over values that are as far from these as can be, so that each line has to have done it. */
        settings_values_t back = {.render_distance = values.render_distance == 2u ? 3u : 2u,
                                  .effects = ~values.effects & SHADE_FEATURES_EVERY,
                                  .vsync = (uint8_t)(values.vsync != 0u ? 0u : 1u),
                                  .show_coordinates = (uint8_t)(values.show_coordinates != 0u ? 0u : 1u)};
        settings_persist_report_t report;
        settings_persist_parse(text, bytes, &back, &report);
        CHECK_EQ_U64(report.problems, 0u);
        CHECK_EQ_U64(report.applied, 6u + 2u + SETTINGS_EFFECT_COUNT);
        CHECK_MEM_EQ(&back, &values, sizeof(values));
    }
}

TEST(a_file_is_read_in_any_order_with_comments_and_the_last_line_wins) {
    settings_values_t values = values_defaults();
    const settings_persist_report_t report = parse_text("\n# first\n   \nversion: 1   # this one\n"
                                                        "fov_degrees:90\r\n"
                                                        "  render_distance :   128  # far\n"
                                                        "vsync: false\n"
                                                        "effect_shadows: false\n"
                                                        "effect_god_rays: true\n"
                                                        "fov_degrees: 95",
                                                        &values);
    CHECK_EQ_U64(report.problems, 0u);
    CHECK_EQ_U64(report.applied, 6u);
    CHECK_EQ_U64(values.render_distance, 128u);
    CHECK_EQ_U64(values.fov_degrees, 95u);
    CHECK_EQ_U64(values.vsync, 0u);
    CHECK_EQ_U64(values.effects & SHADE_FEATURE_SHADOW, 0u);
    CHECK_EQ_U64(values.effects & SHADE_FEATURE_GOD_RAYS, SHADE_FEATURE_GOD_RAYS);
    /* What was not said stays. */
    CHECK_EQ_U64(values.full_detail, 32u);
    CHECK_EQ_U64(values.effects & SHADE_FEATURE_FOG, SHADE_FEATURE_FOG);
}

/* What a file written before the setting existed says, every line of it: not a thing to warn about, and it comes up as it does on a new install. */
TEST(a_file_from_before_show_coordinates_still_loads_and_shows_them) {
    static const char old_file[] = "version: 1\n"
                                   "render_distance: 12\n"
                                   "full_detail: 8\n"
                                   "fov_degrees: 80\n"
                                   "sensitivity_percent: 100\n"
                                   "render_scale_percent: 100\n"
                                   "worker_count: 5\n"
                                   "vsync: false\n"
                                   "effect_fog: false\n";
    settings_values_t values = values_defaults();
    const settings_persist_report_t report = parse_text(old_file, &values);
    CHECK_EQ_U64(report.problems, 0u);
    CHECK_EQ_U64(report.applied, 8u);
    CHECK_EQ_U64(values.render_distance, 12u);
    CHECK_EQ_U64(values.vsync, 0u);
    CHECK_EQ_U64(values.show_coordinates, 1u);
    /* And each way it can be said from then on. */
    CHECK_EQ_U64(parse_text("version: 1\nshow_coordinates: false\n", &values).problems, 0u);
    CHECK_EQ_U64(values.show_coordinates, 0u);
    CHECK_EQ_U64(values.vsync, 0u);
    CHECK_EQ_U64(parse_text("version: 1\nshow_coordinates: true\n", &values).problems, 0u);
    CHECK_EQ_U64(values.show_coordinates, 1u);
    /* Whatever a flag or a person left in the byte, it comes out as one of the two. */
    values.show_coordinates = 7u;
    settings_persist_clamp(&values);
    CHECK_EQ_U64(values.show_coordinates, 1u);
}

TEST(a_line_that_is_wrong_is_ignored_and_the_rest_are_read) {
    static const char *const wrong[] = {
        "render_distance",
        "render_distance:",
        ": 64",
        "render_distance: -5",
        "render_distance: 0x40",
        "render_distance: 1e2",
        "render_distance: 6 4",
        "render_distance: tree",
        "render_distance: 99999999999",
        "render_distance: 4294967296",
        "no_such_setting: 64",
        "RENDER_DISTANCE: 64",
        "vsync: 1",
        "vsync: yes",
        "vsync: True",
        "show_coordinates: 0",
        "show_coordinates: no",
        "effect_fog: maybe",
        "effect_no_such: true",
        "render_distance = 64",
    };
    for (uint32_t i = 0u; i < sizeof(wrong) / sizeof(wrong[0]); i++) {
        char text[128];
        snprintf(text, sizeof(text), "version: 1\nfull_detail: 8\n%s\nfov_degrees: 80\n", wrong[i]);
        settings_values_t values = values_defaults();
        const settings_persist_report_t report = parse_text(text, &values);
        if (!CHECK_EQ_U64(report.problems, 1u)) {
            printf("line: %s\n", wrong[i]);
        }
        CHECK_EQ_U64(report.applied, 2u);
        CHECK_EQ_U64(values.render_distance, 32u);
        CHECK_EQ_U64(values.vsync, 1u);
        CHECK_EQ_U64(values.show_coordinates, 1u);
        CHECK_EQ_U64(values.full_detail, 8u);
        CHECK_EQ_U64(values.fov_degrees, 80u);
    }
    /* A NUL in a line, and a line too long. */
    static const uint8_t with_nul[] = "version: 1\nfov_degrees: 8\0000\nfull_detail: 8\n";
    settings_values_t values = values_defaults();
    settings_persist_report_t report;
    settings_persist_parse(with_nul, sizeof(with_nul) - 1u, &values, &report);
    CHECK_EQ_U64(report.problems, 1u);
    CHECK_EQ_U64(values.fov_degrees, 70u);
    CHECK_EQ_U64(values.full_detail, 8u);
    char long_line[256];
    char padding[120];
    memset(padding, ' ', sizeof(padding) - 1u);
    padding[sizeof(padding) - 1u] = '\0';
    snprintf(long_line, sizeof(long_line), "version: 1\nfov_degrees: 90%s\nfull_detail: 8\n", padding);
    values = values_defaults();
    report = parse_text(long_line, &values);
    CHECK_EQ_U64(report.problems, 1u);
    CHECK_EQ_U64(values.fov_degrees, 70u);
    CHECK_EQ_U64(values.full_detail, 8u);
}

TEST(a_number_past_its_range_is_held_to_it_and_said) {
    typedef struct range_case_t {
        const char *line;
        uint32_t offset;
        uint32_t expected;
    } range_case_t;
    static_assert(sizeof(range_case_t) == 16);
    static const range_case_t cases[] = {
        {"render_distance: 1", offsetof(settings_values_t, render_distance), 2u},
        {"render_distance: 513", offsetof(settings_values_t, render_distance), 512u},
        {"full_detail: 0", offsetof(settings_values_t, full_detail), 2u},
        {"full_detail: 49", offsetof(settings_values_t, full_detail), 48u},
        {"fov_degrees: 29", offsetof(settings_values_t, fov_degrees), 30u},
        {"fov_degrees: 111", offsetof(settings_values_t, fov_degrees), 110u},
        {"sensitivity_percent: 9", offsetof(settings_values_t, sensitivity_percent), 10u},
        {"sensitivity_percent: 401", offsetof(settings_values_t, sensitivity_percent), 400u},
        {"render_scale_percent: 24", offsetof(settings_values_t, render_scale_percent), 25u},
        {"render_scale_percent: 101", offsetof(settings_values_t, render_scale_percent), 100u},
        {"worker_count: 0", offsetof(settings_values_t, worker_count), 1u},
        {"worker_count: 17", offsetof(settings_values_t, worker_count), 16u},
    };
    for (uint32_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char text[64];
        snprintf(text, sizeof(text), "version: 1\n%s\n", cases[i].line);
        settings_values_t values = values_defaults();
        const settings_persist_report_t report = parse_text(text, &values);
        CHECK_EQ_U64(report.problems, 1u);
        CHECK_EQ_U64(report.applied, 1u);
        uint32_t value = 0u;
        memcpy(&value, (const uint8_t *)&values + cases[i].offset, sizeof(value));
        CHECK_EQ_U64(value, cases[i].expected);
    }
}

TEST(a_file_of_another_version_sets_nothing_at_all) {
    static const char *const others[] = {"version: 2\nrender_distance: 64\n", "render_distance: 64\nversion: 1\n",
                                         "version: one\nrender_distance: 64\n", "version:\nrender_distance: 64\n",
                                         "render_distance: 64\n"};
    const settings_values_t defaults = values_defaults();
    for (uint32_t i = 0u; i < sizeof(others) / sizeof(others[0]); i++) {
        settings_values_t values = defaults;
        const settings_persist_report_t report = parse_text(others[i], &values);
        CHECK_EQ_U64(report.problems, 1u);
        CHECK_EQ_U64(report.applied, 0u);
        CHECK_MEM_EQ(&values, &defaults, sizeof(values));
    }
    settings_values_t values = defaults;
    const settings_persist_report_t empty = parse_text("", &values);
    CHECK_EQ_U64(empty.problems, 0u);
    CHECK_MEM_EQ(&values, &defaults, sizeof(values));
}

TEST(settings_are_kept_and_found_again_and_a_failed_save_leaves_what_was_there) {
    char *const root = temp_directory(t);
    REQUIRE(root != nullptr);
    char *const path = path_join(t, root, "settings.yaml");
    settings_t *const settings = aligned_alloc_64(t, sizeof(settings_t));
    heap_counters_t before;
    heap_counters_read(&before);
    REQUIRE(settings_init(settings));
    /* Nothing there yet is a first run. */
    settings_load(settings, path);
    const settings_values_t defaults = values_defaults();
    CHECK_MEM_EQ(&settings->values, &defaults, sizeof(defaults));
    uint64_t size = 0u;
    CHECK(file_path_size(path, &size) == FILE_ERROR_NOT_FOUND);

    settings->values.render_distance = 317u;
    settings->values.full_detail = 7u;
    settings->values.fov_degrees = 103u;
    settings->values.sensitivity_percent = 55u;
    settings->values.render_scale_percent = 75u;
    settings->values.worker_count = 3u;
    settings->values.vsync = 0u;
    settings->values.show_coordinates = 0u;
    settings->values.effects = SHADE_FEATURE_SKY | SHADE_FEATURE_GOD_RAYS;
    const settings_values_t chosen = settings->values;
    REQUIRE(settings_save(settings) == FILE_OK);
    /* Nothing left beside it. */
    CHECK(file_path_size(path_join(t, root, "settings.yaml.tmp"), &size) == FILE_ERROR_NOT_FOUND);
    settings_deinit(settings);

    REQUIRE(settings_init(settings));
    settings_load(settings, path);
    CHECK_MEM_EQ(&settings->values, &chosen, sizeof(chosen));
    settings_deinit(settings);

    /* A directory where the file should go: nothing is lost, and what went wrong comes back. */
    char *const in_the_way = path_join(t, root, "in_the_way");
    REQUIRE(file_make_dirs(in_the_way) == FILE_OK);
    REQUIRE(settings_init(settings));
    settings_load(settings, in_the_way);
    CHECK_MEM_EQ(&settings->values, &defaults, sizeof(defaults));
    CHECK(settings_save(settings) != FILE_OK);
    settings_deinit(settings);

    /* Past what a file can be, from a person with too much to say: the defaults, and it says so. */
    char *const huge = path_join(t, root, "huge.yaml");
    uint8_t *const large = test_alloc(t, SETTINGS_TEXT_BYTES + 1u);
    memset(large, '#', SETTINGS_TEXT_BYTES + 1u);
    REQUIRE(file_write_atomic(huge, large, SETTINGS_TEXT_BYTES + 1u) == FILE_OK);
    REQUIRE(settings_init(settings));
    settings_load(settings, huge);
    CHECK_MEM_EQ(&settings->values, &defaults, sizeof(defaults));
    settings_deinit(settings);

    /* Saving with nowhere to save: no load has said where. */
    REQUIRE(settings_init(settings));
    CHECK(settings_save(settings) == FILE_ERROR_NOT_FOUND);
    settings_deinit(settings);
    /* Twice is safe, and everything has gone back. */
    settings_deinit(settings);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    temp_directory_remove(t, root);
}

/* ---- The screen, on made-up input ------------------------------------------------------------------------------------- */

typedef struct screen_rig_t {
    texture_t *texture;       /* 0 */
    ui_t *ui;                 /* 8 */
    settings_values_t values; /* 16 */
    uint32_t width;           /* 48 */
    uint32_t height;          /* 52 */
    int32_t pointer_x;        /* 56 */
    int32_t pointer_y;        /* 60 */
} screen_rig_t;
static_assert(sizeof(screen_rig_t) == 64);

static ui_screen_settings_action_t rig_frame(screen_rig_t *rig, const input_frame_t *frame, bool escape) {
    ui_t *const ui = rig->ui;
    ui_widget_input_feed(&ui->widget, frame);
    if (!ui_frame_begin_blank(ui, rig->width, rig->height)) {
        return (ui_screen_settings_action_t)255u;
    }
    const ui_screen_settings_action_t action = ui_screen_settings_frame(&ui->settings_screen, escape);
    (void)ui_frame_end(ui, true);
    return action;
}

static bool rig_begin(test_context_t *t, screen_rig_t *rig, uint32_t width, uint32_t height) {
    memset(rig, 0, sizeof(*rig));
    rig->texture = aligned_alloc_64(t, sizeof(texture_t));
    rig->ui = aligned_alloc_64(t, sizeof(ui_t));
    rig->width = width;
    rig->height = height;
    settings_persist_defaults(&rig->values);
    if (!CHECK(texture_init(rig->texture))) {
        return false;
    }
    if (!CHECK(ui_init(rig->ui, rig->texture))) {
        texture_deinit(rig->texture);
        return false;
    }
    ui_screen_settings_open(&rig->ui->settings_screen, &rig->ui->widget, &rig->values, 0u, MC_THREADS_WORKERS, false);
    /* A first frame, so that the widgets know the canvas and the pointer is in its middle. */
    const input_frame_t nothing = {0};
    (void)rig_frame(rig, &nothing, false);
    rig->pointer_x = (int32_t)(width / 2u);
    rig->pointer_y = (int32_t)(height / 2u);
    return true;
}

static void rig_end(screen_rig_t *rig) {
    ui_deinit(rig->ui);
    texture_deinit(rig->texture);
}

static void keys_set(input_keys_t *keys, uint32_t code) {
    keys->words[code / 64u] |= (uint64_t)1 << (code % 64u);
}

static ui_screen_settings_action_t rig_move(screen_rig_t *rig, int32_t x, int32_t y) {
    input_frame_t frame = {0};
    frame.mouse_dx = x - rig->pointer_x;
    frame.mouse_dy = y - rig->pointer_y;
    rig->pointer_x = x;
    rig->pointer_y = y;
    return rig_frame(rig, &frame, false);
}

/* The same with the button held. */
static ui_screen_settings_action_t rig_drag(screen_rig_t *rig, int32_t x, int32_t y) {
    input_frame_t frame = {0};
    frame.mouse_dx = x - rig->pointer_x;
    frame.mouse_dy = y - rig->pointer_y;
    keys_set(&frame.down, BTN_LEFT);
    rig->pointer_x = x;
    rig->pointer_y = y;
    return rig_frame(rig, &frame, false);
}

static ui_screen_settings_action_t rig_press(screen_rig_t *rig) {
    input_frame_t frame = {0};
    keys_set(&frame.pressed, BTN_LEFT);
    keys_set(&frame.down, BTN_LEFT);
    return rig_frame(rig, &frame, false);
}

static ui_screen_settings_action_t rig_release(screen_rig_t *rig) {
    input_frame_t frame = {0};
    keys_set(&frame.released, BTN_LEFT);
    return rig_frame(rig, &frame, false);
}

/* What the last frame returned is the click's. */
static ui_screen_settings_action_t rig_click(screen_rig_t *rig, int32_t x, int32_t y) {
    (void)rig_move(rig, x, y);
    (void)rig_press(rig);
    return rig_release(rig);
}

static ui_screen_settings_action_t rig_wheel(screen_rig_t *rig, int64_t detents) {
    input_frame_t frame = {0};
    frame.wheel = detents;
    return rig_frame(rig, &frame, false);
}

static ui_screen_settings_action_t rig_escape(screen_rig_t *rig) {
    const input_frame_t nothing = {0};
    return rig_frame(rig, &nothing, true);
}

/* Every value a slider has, end to end by the wheel and by dragging. */
static void slider_check(test_context_t *t, screen_rig_t *rig, int32_t row, uint32_t *value, uint32_t minimum,
                         uint32_t maximum) {
    const int32_t y = AT_ROW_0_Y + row * AT_ROW_STEP;
    (void)rig_move(rig, AT_MIDDLE_X, y);
    *value = minimum;
    CHECK_EQ_U64(rig_wheel(rig, -1), UI_SCREEN_SETTINGS_NONE); /* it is at the end already */
    CHECK_EQ_U64(*value, minimum);
    for (uint32_t expected = minimum + 1u; expected <= maximum; expected++) {
        CHECK_EQ_U64(rig_wheel(rig, 1), UI_SCREEN_SETTINGS_CHANGED);
        CHECK_EQ_U64(*value, expected);
    }
    CHECK_EQ_U64(rig_wheel(rig, 1), UI_SCREEN_SETTINGS_NONE);
    CHECK_EQ_U64(*value, maximum);
    /* A drag: from where it is pressed, all the way left and all the way right, whatever the pointer does past the ends. */
    (void)rig_press(rig);
    CHECK_EQ_U64(rig_drag(rig, AT_SLIDER_LEFT_X - 50, y), UI_SCREEN_SETTINGS_CHANGED);
    CHECK_EQ_U64(*value, minimum);
    CHECK_EQ_U64(rig_drag(rig, AT_SLIDER_RIGHT_X + 50, y + 100), UI_SCREEN_SETTINGS_CHANGED);
    CHECK_EQ_U64(*value, maximum);
    (void)rig_release(rig);
    (void)rig_move(rig, AT_MIDDLE_X, y);
    CHECK_EQ_U64(*value, maximum);
}

TEST(each_slider_goes_through_every_value_its_setting_has) {
    screen_rig_t *const rig = aligned_alloc_64(t, sizeof(screen_rig_t));
    REQUIRE(rig_begin(t, rig, SHEET_WIDTH, SHEET_HEIGHT));
    slider_check(t, rig, 0, &rig->values.render_distance, 2u, 512u);
    slider_check(t, rig, 1, &rig->values.full_detail, 2u, 48u);
    slider_check(t, rig, 2, &rig->values.fov_degrees, 30u, 110u);
    slider_check(t, rig, 3, &rig->values.sensitivity_percent, 10u, 400u);
    slider_check(t, rig, 4, &rig->values.render_scale_percent, 25u, 100u);
    slider_check(t, rig, 5, &rig->values.worker_count, 1u, MC_THREADS_WORKERS);
    rig_end(rig);
}

TEST(the_wheel_over_nothing_changes_nothing_and_many_detents_are_held_at_the_end) {
    screen_rig_t *const rig = aligned_alloc_64(t, sizeof(screen_rig_t));
    REQUIRE(rig_begin(t, rig, SHEET_WIDTH, SHEET_HEIGHT));
    const settings_values_t before = rig->values;
    (void)rig_move(rig, 5, 5);
    CHECK_EQ_U64(rig_wheel(rig, 40), UI_SCREEN_SETTINGS_NONE);
    CHECK_MEM_EQ(&rig->values, &before, sizeof(before));
    (void)rig_move(rig, AT_MIDDLE_X, AT_ROW_0_Y);
    CHECK_EQ_U64(rig_wheel(rig, 100000), UI_SCREEN_SETTINGS_CHANGED);
    CHECK_EQ_U64(rig->values.render_distance, 512u);
    CHECK_EQ_U64(rig_wheel(rig, -100000), UI_SCREEN_SETTINGS_CHANGED);
    CHECK_EQ_U64(rig->values.render_distance, 2u);
    rig_end(rig);
}

TEST(vsync_coordinates_and_each_effect_flip_alone_and_done_and_esc_go_where_they_say) {
    screen_rig_t *const rig = aligned_alloc_64(t, sizeof(screen_rig_t));
    REQUIRE(rig_begin(t, rig, SHEET_WIDTH, SHEET_HEIGHT));
    settings_values_t expected = rig->values;
    CHECK_EQ_U64(rig_click(rig, AT_VSYNC_X, AT_ROW_6_Y), UI_SCREEN_SETTINGS_CHANGED);
    expected.vsync = 0u;
    CHECK_MEM_EQ(&rig->values, &expected, sizeof(expected));
    (void)rig_click(rig, AT_VSYNC_X, AT_ROW_6_Y);
    expected.vsync = 1u;
    CHECK_MEM_EQ(&rig->values, &expected, sizeof(expected));
    CHECK_EQ_U64(rig_click(rig, AT_COORDINATES_X, AT_ROW_6_Y), UI_SCREEN_SETTINGS_CHANGED);
    expected.show_coordinates = 0u;
    CHECK_MEM_EQ(&rig->values, &expected, sizeof(expected));
    (void)rig_click(rig, AT_COORDINATES_X, AT_ROW_6_Y);
    expected.show_coordinates = 1u;
    CHECK_MEM_EQ(&rig->values, &expected, sizeof(expected));

    CHECK_EQ_U64(rig_click(rig, AT_EFFECTS_X, AT_ROW_6_Y), UI_SCREEN_SETTINGS_NONE);
    CHECK_EQ_U64(rig->ui->settings_screen.page, UI_SCREEN_SETTINGS_PAGE_EFFECTS);
    for (uint32_t i = 0u; i < SETTINGS_EFFECT_COUNT; i++) {
        const int32_t x = i < 8u ? AT_EFFECT_LEFT_X : AT_EFFECT_RIGHT_X;
        const int32_t y = AT_ROW_0_Y + (int32_t)(i % 8u) * AT_ROW_STEP;
        CHECK_EQ_U64(rig_click(rig, x, y), UI_SCREEN_SETTINGS_CHANGED);
        expected.effects ^= settings_persist_effect(i)->bit;
        CHECK_MEM_EQ(&rig->values, &expected, sizeof(expected));
        (void)rig_click(rig, x, y);
        expected.effects ^= settings_persist_effect(i)->bit;
        CHECK_MEM_EQ(&rig->values, &expected, sizeof(expected));
    }
    /* Esc from this page goes back one, and is not done; Back does the same. */
    CHECK_EQ_U64(rig_escape(rig), UI_SCREEN_SETTINGS_NONE);
    CHECK_EQ_U64(rig->ui->settings_screen.page, UI_SCREEN_SETTINGS_PAGE_MAIN);
    (void)rig_click(rig, AT_EFFECTS_X, AT_ROW_6_Y);
    CHECK_EQ_U64(rig->ui->settings_screen.page, UI_SCREEN_SETTINGS_PAGE_EFFECTS);
    CHECK_EQ_U64(rig_click(rig, AT_MIDDLE_X, AT_DONE_Y), UI_SCREEN_SETTINGS_NONE);
    CHECK_EQ_U64(rig->ui->settings_screen.page, UI_SCREEN_SETTINGS_PAGE_MAIN);
    /* Back there was not Done here. */
    CHECK_EQ_U64(rig_click(rig, AT_MIDDLE_X, AT_DONE_Y), UI_SCREEN_SETTINGS_DONE);
    CHECK_EQ_U64(rig_escape(rig), UI_SCREEN_SETTINGS_DONE);
    rig_end(rig);
}

TEST(no_heap_in_200_frames_of_either_page_and_it_fits_every_size) {
    screen_rig_t *const rig = aligned_alloc_64(t, sizeof(screen_rig_t));
    REQUIRE(rig_begin(t, rig, SHEET_WIDTH, SHEET_HEIGHT));
    const input_frame_t nothing = {0};
    (void)rig_frame(rig, &nothing, false);
    const uint64_t used = rig->ui->arena_tables.used;
    heap_counters_t before;
    heap_counters_read(&before);
    for (uint32_t i = 0u; i < 200u; i++) {
        rig->ui->settings_screen.page = (uint8_t)(i & 1u);
        CHECK_EQ_U64(rig_frame(rig, &nothing, false), UI_SCREEN_SETTINGS_NONE);
    }
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(rig->ui->arena_tables.used, used);
    rig_end(rig);

    /* Each row stays on the canvas at the smallest size that scale takes, and any other. */
    static const uint32_t sizes[][2] = {{320u, 240u}, {640u, 480u}, {1920u, 1080u}, {800u, 300u}, {321u, 900u}};
    for (uint32_t s = 0u; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        REQUIRE(rig_begin(t, rig, sizes[s][0], sizes[s][1]));
        const int32_t scale = (int32_t)ui_widget_scale_for(sizes[s][0], sizes[s][1]);
        const int32_t top = ((int32_t)sizes[s][1] - 240 * scale) / 2;
        CHECK(top >= 0);
        /* Done, the lowest thing there is, ends inside it. */
        CHECK(top + (24 + 8 * 24 + 20) * scale <= (int32_t)sizes[s][1]);
        CHECK(((int32_t)sizes[s][0] - 2 * (120 + 2) * scale) / 2 >= 0);
        rig->ui->settings_screen.page = 1u;
        (void)rig_frame(rig, &nothing, false);
        rig->ui->settings_screen.page = 0u;
        (void)rig_frame(rig, &nothing, false);
        rig_end(rig);
    }
}

/* The label says so: a worker count of another run, and full detail past what a world's table holds, by looking at what is drawn. */
TEST(what_takes_more_than_this_screen_says_so) {
    screen_rig_t *const rig = aligned_alloc_64(t, sizeof(screen_rig_t));
    REQUIRE(rig_begin(t, rig, SHEET_WIDTH, SHEET_HEIGHT));
    const input_frame_t nothing = {0};
    (void)rig_frame(rig, &nothing, false);
    const uint64_t pixels = (uint64_t)SHEET_WIDTH * SHEET_HEIGHT * 4u;
    uint8_t *const plain = test_alloc(t, pixels);
    memcpy(plain, rig->ui->canvas.borrowed_pixels, pixels);
    /* Nothing is past anything with no world, and a world of 32 holds all of 32. */
    ui_screen_settings_open(&rig->ui->settings_screen, &rig->ui->widget, &rig->values, 32u, MC_THREADS_WORKERS, false);
    (void)rig_frame(rig, &nothing, false);
    CHECK_MEM_EQ(rig->ui->canvas.borrowed_pixels, plain, pixels);
    ui_screen_settings_open(&rig->ui->settings_screen, &rig->ui->widget, &rig->values, 31u, MC_THREADS_WORKERS, false);
    (void)rig_frame(rig, &nothing, false);
    CHECK(memcmp(rig->ui->canvas.borrowed_pixels, plain, pixels) != 0);
    ui_screen_settings_open(&rig->ui->settings_screen, &rig->ui->widget, &rig->values, 32u, 3u, false);
    (void)rig_frame(rig, &nothing, false);
    CHECK(memcmp(rig->ui->canvas.borrowed_pixels, plain, pixels) != 0);
    /* And what went wrong with keeping them, under everything else. */
    ui_screen_settings_open(&rig->ui->settings_screen, &rig->ui->widget, &rig->values, 32u, MC_THREADS_WORKERS, false);
    ui_screen_settings_message_set(&rig->ui->settings_screen, "Could not keep them: disk full");
    (void)rig_frame(rig, &nothing, false);
    CHECK(memcmp(rig->ui->canvas.borrowed_pixels, plain, pixels) != 0);
    rig_end(rig);
}

/* ---- What a config comes to, by hand ----------------------------------------------------------------------------------- */

TEST(a_config_comes_to_this_by_hand) {
    game_config_t config = {.render_distance = 32u, .width = 1920u, .height = 1080u, ._pad0 = {0}};
    game_view_t view;
    /* All at full detail, by default: no far field, and fog a chunk short of it. */
    CHECK_EQ_U64(game_view_table_chunks(&config), 32u);
    game_view_work_out(&config, 32u, &view);
    CHECK_EQ_U64(view.full_detail, 32u);
    CHECK_EQ_U64(view.lod.level_count, 0u);
    CHECK_EQ_U64((uint64_t)view.fog_end, 31u * 16u);
    CHECK_EQ_U64(view.width, 1920u);
    CHECK_EQ_U64(view.height, 1080u);
    CHECK_EQ_U64(view.features, SHADE_FEATURES_PLAY);
    /* Nearer than the table. The table stays for what the world opened with. */
    config.render_distance = 2u;
    config.table_chunks = 32u;
    CHECK_EQ_U64(game_view_table_chunks(&config), 32u);
    game_view_work_out(&config, 32u, &view);
    CHECK_EQ_U64(view.full_detail, 2u);
    CHECK_EQ_U64(view.lod.level_count, 0u);
    CHECK_EQ_U64((uint64_t)view.fog_end, 16u);
    /* Further: the far field, and fog at what it reaches. */
    config.render_distance = 512u;
    game_view_work_out(&config, 32u, &view);
    CHECK_EQ_U64(view.full_detail, 32u);
    CHECK(view.lod.level_count != 0u);
    CHECK_EQ_U64((uint64_t)view.fog_end, 511u * 16u);
    const render_lod_plan_t plan = render_lod_plan(32u, 512u);
    CHECK_MEM_EQ(&view.lod, &plan, sizeof(plan));
    /* With the far field off it stops at full detail, and the fog comes in. */
    config.features_off = SHADE_FEATURE_LOD;
    game_view_work_out(&config, 32u, &view);
    CHECK_EQ_U64(view.lod.level_count, 0u);
    CHECK_EQ_U64((uint64_t)view.fog_end, 31u * 16u);
    CHECK_EQ_U64(view.features, SHADE_FEATURES_PLAY & ~(uint32_t)SHADE_FEATURE_LOD);
    /* Full detail past the table is held to it, and the far field starts there. */
    config.features_off = 0u;
    config.full_detail = 48u;
    config.table_chunks = 32u;
    CHECK_EQ_U64(game_view_table_chunks(&config), 48u);
    game_view_work_out(&config, 32u, &view);
    CHECK_EQ_U64(view.full_detail, 32u);
    /* Size, and what cannot be nothing. */
    config.render_scale_percent = 50u;
    game_view_work_out(&config, 32u, &view);
    CHECK_EQ_U64(view.width, 960u);
    CHECK_EQ_U64(view.height, 540u);
    config.width = 1u;
    config.height = 1u;
    game_view_work_out(&config, 32u, &view);
    CHECK_EQ_U64(view.width, 1u);
    CHECK_EQ_U64(view.height, 1u);
    config.god_rays = 1u;
    game_view_work_out(&config, 32u, &view);
    CHECK_EQ_U64(view.features, SHADE_FEATURES_EVERY);
}

TEST(the_field_of_view_and_the_mouse_by_hand) {
    CHECK(game_view_half_fov_tangent(0u) == RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT);
    CHECK(game_view_half_fov_tangent(70u) == RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT);
    CHECK_NEAR(game_view_half_fov_tangent(90u), 1.0f32, 0.00001f32);
    CHECK_NEAR(game_view_half_fov_tangent(30u), 0.26795f32, 0.00001f32);
    float64_t rest = 0.0f64;
    /* At its usual value a count is a count, and nothing is kept. */
    CHECK_EQ_I64(game_view_look_scaled(-37, 100u, &rest), -37);
    CHECK_EQ_I64(game_view_look_scaled(-37, 0u, &rest), -37);
    CHECK(rest == 0.0f64);
    /* Double and half. */
    CHECK_EQ_I64(game_view_look_scaled(7, 200u, &rest), 14);
    CHECK_EQ_I64(game_view_look_scaled(-7, 50u, &rest), -3);
    CHECK_NEAR(rest, -0.5f64, 0.000001f64);
    CHECK_EQ_I64(game_view_look_scaled(-1, 50u, &rest), -1);
    CHECK_NEAR(rest, 0.0f64, 0.000001f64);
    /* A slow hand still turns: a thousand counts one at a time are as far as a thousand at once. */
    rest = 0.0f64;
    int64_t sum = 0;
    for (uint32_t i = 0u; i < 1000u; i++) {
        sum += game_view_look_scaled(1, 13u, &rest);
    }
    CHECK_EQ_I64(sum, 130);
    CHECK_EQ_I64(sum + game_view_look_scaled(0, 13u, &rest), 130);
}

TEST(a_small_picture_is_brought_up_by_the_nearest_pixel) {
    static const uint32_t from[] = {1u, 2u, 3u, 4u, 5u, 6u};
    uint32_t to[6u * 4u];
    /* Two by three to four by six: each pixel twice each way. */
    game_view_bring_up((const uint8_t *)from, 2u, 3u, (uint8_t *)to, 4u, 6u);
    static const uint32_t doubled[] = {1u, 1u, 2u, 2u, 1u, 1u, 2u, 2u, 3u, 3u, 4u, 4u,
                                       3u, 3u, 4u, 4u, 5u, 5u, 6u, 6u, 5u, 5u, 6u, 6u};
    CHECK_MEM_EQ(to, doubled, sizeof(doubled));
    /* Not a whole number of times: the place each comes from, by the formula in the source. */
    uint32_t wide[5u];
    static const uint32_t row[] = {7u, 8u, 9u};
    game_view_bring_up((const uint8_t *)row, 3u, 1u, (uint8_t *)wide, 5u, 1u);
    static const uint32_t expected[] = {7u, 7u, 8u, 8u, 9u};
    CHECK_MEM_EQ(wide, expected, sizeof(expected));
    /* The same size is a copy. */
    uint32_t same[6];
    game_view_bring_up((const uint8_t *)from, 2u, 3u, (uint8_t *)same, 2u, 3u);
    CHECK_MEM_EQ(same, from, sizeof(from));
}

/* ---- The workers ------------------------------------------------------------------------------------------------------- */

static void count_job(job_worker_t *worker, void *borrowed_argument) {
    _Atomic uint64_t *const counts = borrowed_argument;
    atomic_fetch_add_explicit(&counts[worker->index], 1u, memory_order_relaxed);
    /* Long enough that more than one is wanted. */
    time_sleep_ns(20000);
}

TEST(a_worker_count_is_how_many_run) {
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    REQUIRE(thread_topology_read(topology));
    job_system_t *const job = aligned_alloc_64(t, sizeof(job_system_t));
    static const uint32_t asked[][2] = {{3u, 3u}, {1u, 1u}, {0u, 1u}, {999u, MC_THREADS_WORKERS}};
    for (uint32_t c = 0u; c < sizeof(asked) / sizeof(asked[0]); c++) {
        REQUIRE(job_init_count(job, topology, asked[c][0]));
        CHECK_EQ_U64(job->worker_count, asked[c][1]);
        _Atomic uint64_t counts[MC_THREADS_WORKERS];
        memset(counts, 0, sizeof(counts));
        job_group_t group;
        job_group_init(&group);
        for (uint32_t i = 0u; i < 3000u; i++) {
            job_submit(job, &group, count_job, counts);
        }
        job_group_wait(job, &group);
        job_group_deinit(&group);
        uint64_t sum = 0u;
        uint32_t busy = 0u;
        for (uint32_t i = 0u; i < MC_THREADS_WORKERS; i++) {
            const uint64_t count = atomic_load_explicit(&counts[i], memory_order_relaxed);
            sum += count;
            busy += count != 0u ? 1u : 0u;
            if (i >= asked[c][1]) {
                CHECK_EQ_U64(count, 0u);
            }
        }
        CHECK_EQ_U64(sum, 3000u);
        CHECK(busy <= asked[c][1]);
        CHECK(busy >= (asked[c][1] > 1u ? 2u : 1u));
        job_deinit(job);
    }
}

/* ---- Flips: on a fake kernel ------------------------------------------------------------------------------------------- */

typedef struct flip_watch_t {
    fake_card_t *card;    /* 0 */
    uint32_t last_flags;  /* 8 */
    uint32_t refused;     /* 12 */
    uint8_t refuse_async; /* 16 */
    uint8_t _pad0[7];     /* 17 */
} flip_watch_t;
static_assert(sizeof(flip_watch_t) == 24);

static flip_watch_t flip_watch;

static int32_t flip_watch_ioctl(void *context, int32_t fd, uint64_t request, void *argument) {
    (void)context;
    if (request == DRM_IOCTL_MODE_PAGE_FLIP) {
        const struct drm_mode_crtc_page_flip *const flip = argument;
        flip_watch.last_flags = flip->flags;
        if (flip_watch.refuse_async != 0u && (flip->flags & DRM_MODE_PAGE_FLIP_ASYNC) != 0u) {
            flip_watch.refused++;
            return -EINVAL;
        }
    }
    return fake_ioctl_call(flip_watch.card, fd, request, argument);
}

static bool flip_one(display_flip_t *flip) {
    if (!display_flip_submit(flip)) {
        return false;
    }
    return display_flip_wait(flip, 100) == DISPLAY_FLIP_WAIT_EVENT;
}

TEST(flips_wait_for_a_vblank_or_do_not_as_the_setting_says_and_a_driver_can_refuse) {
    (void)fake_find_call;
    memset(&flip_watch, 0, sizeof(flip_watch));
    flip_watch.card = fake_card_new(t);
    fake_card_use_small_mode(flip_watch.card);
    display_ops_t ops = fake_ops;
    ops.ioctl_call = flip_watch_ioctl;
    /* The rest of the ops are the fake's, and take it as their context. */
    display_t *const display = aligned_alloc_64(t, sizeof(display_t));
    const display_config_t config = fake_config();
    REQUIRE(display_init(display, &config, &ops, flip_watch.card) == DISPLAY_STATUS_OK);
    display_flip_t *const flip = &display->flip;
    REQUIRE(display_flip_take_over(flip));

    /* The default, as a test pattern's and a --test-world's have always been. */
    REQUIRE(flip_one(flip));
    CHECK_EQ_U64(flip_watch.last_flags, DRM_MODE_PAGE_FLIP_EVENT);
    display_flip_vsync_set(flip, false);
    REQUIRE(flip_one(flip));
    CHECK_EQ_U64(flip_watch.last_flags, DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_PAGE_FLIP_ASYNC);
    display_flip_vsync_set(flip, true);
    REQUIRE(flip_one(flip));
    CHECK_EQ_U64(flip_watch.last_flags, DRM_MODE_PAGE_FLIP_EVENT);

    /* A driver that says no is asked once, and the flip still happens. */
    flip_watch.refuse_async = 1u;
    display_flip_vsync_set(flip, false);
    REQUIRE(flip_one(flip));
    CHECK_EQ_U64(flip_watch.refused, 1u);
    CHECK_EQ_U64(flip_watch.last_flags, DRM_MODE_PAGE_FLIP_EVENT);
    REQUIRE(flip_one(flip));
    CHECK_EQ_U64(flip_watch.refused, 1u);
    CHECK_EQ_U64(flip_watch.last_flags, DRM_MODE_PAGE_FLIP_EVENT);
    CHECK(flip->failed_call == nullptr);

    /* Anything else it says is what it was. */
    display_flip_vsync_set(flip, true);
    flip_watch.card->fail_request = DRM_IOCTL_MODE_PAGE_FLIP;
    flip_watch.card->fail_errno = EIO;
    CHECK(!display_flip_submit(flip));
    CHECK(flip->failed_call != nullptr);
    flip_watch.card->fail_request = 0u;
    display_deinit(display);
    check_nothing_left(t, flip_watch.card);
}

/* Columns in a disc of this radius, by counting. */
static uint32_t disc_columns(int32_t radius) {
    uint32_t count = 0u;
    for (int32_t y = -radius; y <= radius; y++) {
        for (int32_t x = -radius; x <= radius; x++) {
            count += x * x + y * y <= radius * radius ? 1u : 0u;
        }
    }
    return count;
}

/* How many pixels differ by more than a little, between two pictures of one size. */
static uint64_t pixels_different(const uint8_t *a, const uint8_t *b, uint32_t pixels) {
    uint64_t count = 0u;
    for (uint32_t i = 0u; i < pixels * 4u; i += 4u) {
        for (uint32_t c = 0u; c < 3u; c++) {
            const int32_t gap = (int32_t)a[i + c] - (int32_t)b[i + c];
            if (gap > 8 || gap < -8) {
                count++;
                break;
            }
        }
    }
    return count;
}

/* ---- The built program --------------------------------------------------------------------------------------------------- */

typedef struct picture_t {
    uint8_t *rgba;   /* 0 */
    uint32_t width;  /* 8 */
    uint32_t height; /* 12 */
} picture_t;
static_assert(sizeof(picture_t) == 16);

static bool picture_load(test_context_t *t, const char *path, picture_t *out) {
    uint64_t bytes = 0u;
    const uint8_t *const file = file_slurp(t, path, &bytes);
    if (!CHECK(file != nullptr)) {
        printf("no picture at %s\n", path);
        return false;
    }
    png_info_t info;
    png_error_t error;
    if (!CHECK(png_probe(file, bytes, &info, &error) == PNG_OK)) {
        return false;
    }
    out->rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    if (!CHECK(png_decode(file, bytes, out->rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK)) {
        return false;
    }
    out->width = info.width;
    out->height = info.height;
    return true;
}

/* The share of pixels, in thousandths, that are more than a little different. Under the HUD's rows only: both pictures have it. */
static uint32_t picture_different(const picture_t *a, const picture_t *b) {
    if (a->width != b->width || a->height != b->height) {
        return 1000u;
    }
    const uint32_t pixels = a->width * a->height;
    return (uint32_t)(pixels_different(a->rgba, b->rgba, pixels) * 1000u / pixels);
}

static void run_program(test_context_t *t, char *const *argv, test_process_t *out) {
    test_process_run(t, argv, 280000u, out);
    REQUIRE(out->started != 0u);
    CHECK(out->timed_out == 0u);
    if (!CHECK_EQ_I64(out->exit_code, 0)) {
        printf("%s\n", out->output);
    }
}

/* What the file starts as: no day going by and no history, so that what is compared is the same world at the same time. And no shadows: a column
 * kept for a while past the render distance, as one that has just gone out of range is, still shades what is drawn, as it does for a body walking
 * away, and a new program has never loaded it. With those off, what is drawn depends on nothing past the distance. */
static const char PROGRAM_START[] = "version: 1\nrender_distance: 8\nfull_detail: 8\neffect_day_cycle: false\n"
                                    "effect_ao: false\neffect_taa: false\neffect_shadows: false\nworker_count: 5\n";

static void settings_write(test_context_t *t, const char *path, const char *text) {
    REQUIRE(file_write_atomic(path, text, strlen(text)) == FILE_OK);
}

TEST(render_distance_changes_live_in_the_built_program_and_is_kept_and_the_pictures_prove_it) {
    REQUIRE(t->arg_count >= 1);
    char *const root = temp_directory(t);
    REQUIRE(root != nullptr);
    char *const saves = path_join(t, root, "saves");
    char *const shots = path_join(t, root, "shots");
    char *const again_2 = path_join(t, root, "again_2");
    char *const again_4 = path_join(t, root, "again_4");
    char *const file = path_join(t, root, "settings.yaml");
    settings_write(t, file, PROGRAM_START);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--saves",
                          saves,
                          (char *)"--settings",
                          file,
                          (char *)"--script",
                          (char *)"tests/run/settings/fixtures/live.script",
                          (char *)"--screenshot-dir",
                          shots,
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          nullptr};
    test_process_t process;
    run_program(t, argv, &process);
    if (t->failure_count != 0u) {
        return;
    }
    /* One world, all the way through: never opened twice, never closed but at the end. */
    CHECK_EQ_U64(count_of(process.output, "session: world 'live' is open"), 1u);
    CHECK_EQ_U64(count_of(process.output, "session: back on the title screen"), 1u);
    /* Each change, as it took. */
    CHECK_CONTAINS(process.output, "game: now drawing 2 chunks, 2 of them block by block");
    CHECK_CONTAINS(process.output, "game: now drawing 8 chunks, 8 of them block by block");
    CHECK_CONTAINS(process.output, "game: now drawing 4 chunks, 4 of them block by block");
    /* What was loaded each time, by counting. */
    char line[128];
    snprintf(line, sizeof(line),
             "game: settled on %" PRIu32 " columns, with the map's view at 2 chunks and 0 far levels", disc_columns(4));
    CHECK_CONTAINS(process.output, line);
    snprintf(line, sizeof(line),
             "game: settled on %" PRIu32 " columns, with the map's view at 8 chunks and 0 far levels", disc_columns(8));
    CHECK_CONTAINS(process.output, line);
    CHECK_CONTAINS(process.output, "render distance 8, full detail 8, 5 workers");
    CHECK_CONTAINS(process.output, "settings: kept in ");
    CHECK_CONTAINS(process.output, " 0 bytes live");

    /* It is in the file, with what it did not touch. */
    uint64_t bytes = 0u;
    const uint8_t *const kept = file_slurp(t, file, &bytes);
    REQUIRE(kept != nullptr);
    CHECK_CONTAINS((const char *)kept, "render_distance: 4\n");
    CHECK_CONTAINS((const char *)kept, "full_detail: 8\n");
    CHECK_CONTAINS((const char *)kept, "worker_count: 5\n");
    CHECK_CONTAINS((const char *)kept, "effect_taa: false\n");

    picture_t at_8, at_2, at_8_again, at_4, behind_2, screen, pause_after_2;
    REQUIRE(picture_load(t, path_join(t, shots, "at_8.png"), &at_8));
    REQUIRE(picture_load(t, path_join(t, shots, "at_2.png"), &at_2));
    REQUIRE(picture_load(t, path_join(t, shots, "at_8_again.png"), &at_8_again));
    REQUIRE(picture_load(t, path_join(t, shots, "at_4.png"), &at_4));
    REQUIRE(picture_load(t, path_join(t, shots, "behind_2.png"), &behind_2));
    REQUIRE(picture_load(t, path_join(t, shots, "screen.png"), &screen));
    REQUIRE(picture_load(t, path_join(t, shots, "pause_after_2.png"), &pause_after_2));
    const uint32_t two_from_eight = picture_different(&at_2, &at_8);
    const uint32_t four_from_eight = picture_different(&at_4, &at_8);
    printf("thousandths different: 2 from 8 %" PRIu32 ", 4 from 8 %" PRIu32 ", 8 from 8 again %" PRIu32 "\n",
           two_from_eight, four_from_eight, picture_different(&at_8_again, &at_8));
    /* Each distance is a different picture, by far more than noise. */
    CHECK(two_from_eight > 50u);
    CHECK(four_from_eight > 10u);
    /* And the world's own picture comes back: within a pixel or two of a thousand. */
    CHECK(picture_different(&at_8_again, &at_8) <= 2u);
    /* The change shows behind the screen before it is left: the slider's text aside, the sky has come in. */
    CHECK(picture_different(&behind_2, &screen) > 20u);
    CHECK(picture_different(&pause_after_2, &behind_2) > 20u); /* another screen over it */

    /* What a new program makes of the file: straight to what it says, the same as it looked before the program ended. */
    char *const argv_again[] = {t->borrowed_args[0],
                                (char *)"--headless",
                                (char *)"--menu",
                                (char *)"--saves",
                                saves,
                                (char *)"--settings",
                                file,
                                (char *)"--script",
                                (char *)"tests/run/settings/fixtures/again.script",
                                (char *)"--screenshot-dir",
                                again_4,
                                (char *)"--width",
                                (char *)"640",
                                (char *)"--height",
                                (char *)"480",
                                nullptr};
    run_program(t, argv_again, &process);
    CHECK_CONTAINS(process.output, "render distance 4, full detail 8");
    CHECK_CONTAINS(process.output, "session: world 'live' is open");
    CHECK(strstr(process.output, "game: now drawing") == nullptr); /* nothing needed changing */
    picture_t opened_4;
    REQUIRE(picture_load(t, path_join(t, again_4, "opened.png"), &opened_4));
    const uint32_t fresh_4 = picture_different(&opened_4, &at_4);
    printf("thousandths different: live 4 from a new run of 4: %" PRIu32 "\n", fresh_4);
    CHECK(fresh_4 <= 2u);

    /* And what is 2 live is what 2 is from the start, in a new program. */
    settings_write(t, file,
                   "version: 1\nrender_distance: 2\nfull_detail: 8\neffect_day_cycle: false\n"
                   "effect_ao: false\neffect_taa: false\neffect_shadows: false\n");
    char *const argv_2[] = {t->borrowed_args[0],
                            (char *)"--headless",
                            (char *)"--menu",
                            (char *)"--saves",
                            saves,
                            (char *)"--settings",
                            file,
                            (char *)"--script",
                            (char *)"tests/run/settings/fixtures/again.script",
                            (char *)"--screenshot-dir",
                            again_2,
                            (char *)"--width",
                            (char *)"640",
                            (char *)"--height",
                            (char *)"480",
                            nullptr};
    run_program(t, argv_2, &process);
    picture_t opened_2;
    REQUIRE(picture_load(t, path_join(t, again_2, "opened.png"), &opened_2));
    const uint32_t fresh_2 = picture_different(&opened_2, &at_2);
    printf("thousandths different: live 2 from a new run of 2: %" PRIu32 "\n", fresh_2);
    CHECK(fresh_2 <= 2u);

    /* If SETTINGS_PNG_DIR names a directory, the pictures stay to be looked at. */
    const char *const keep = getenv("SETTINGS_PNG_DIR");
    if (keep != nullptr) {
        static const char *const names[] = {"at_8.png", "screen.png", "behind_2.png",  "pause_after_2.png",
                                            "at_2.png", "at_4.png",   "at_8_again.png"};
        for (uint32_t i = 0u; i < sizeof(names) / sizeof(names[0]); i++) {
            uint64_t size = 0u;
            const uint8_t *const png = file_slurp(t, path_join(t, shots, names[i]), &size);
            REQUIRE(png != nullptr);
            REQUIRE(file_write_atomic(path_join(t, keep, names[i]), png, size) == FILE_OK);
        }
    }
    temp_directory_remove(t, root);
}

/* Past full detail in the built program, on request and on an optimized build: at -O0 a far field takes minutes to settle. From a run of the release
 * preset: SETTINGS_FAR=1 build/release/bin/tests/run_settings build/release/bin/minecraft_c. */
TEST(render_distance_past_full_detail_in_the_built_program_on_request) {
    if (getenv("SETTINGS_FAR") == nullptr) {
        printf(
            "not run, and so proves nothing here: it needs SETTINGS_FAR=1 and a release build, as the README says\n");
        return;
    }
    REQUIRE(t->arg_count >= 1);
    char *const root = temp_directory(t);
    REQUIRE(root != nullptr);
    char *const saves = path_join(t, root, "saves");
    char *const shots = path_join(t, root, "shots");
    char *const again = path_join(t, root, "again");
    char *const file = path_join(t, root, "settings.yaml");
    settings_write(t, file, PROGRAM_START);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--saves",
                          saves,
                          (char *)"--settings",
                          file,
                          (char *)"--script",
                          (char *)"tests/run/settings/fixtures/far.script",
                          (char *)"--screenshot-dir",
                          shots,
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          nullptr};
    test_process_t process;
    run_program(t, argv, &process);
    CHECK_EQ_U64(count_of(process.output, "session: world 'live' is open"), 1u);
    CHECK_CONTAINS(process.output, "game: now drawing 14 chunks, 8 of them block by block, 1 far levels");
    CHECK_CONTAINS(process.output, "game: now drawing 12 chunks, 8 of them block by block, 1 far levels");
    picture_t at_8, at_14, at_12;
    REQUIRE(picture_load(t, path_join(t, shots, "at_8.png"), &at_8));
    REQUIRE(picture_load(t, path_join(t, shots, "at_14.png"), &at_14));
    REQUIRE(picture_load(t, path_join(t, shots, "at_12.png"), &at_12));
    char *const argv_again[] = {t->borrowed_args[0],
                                (char *)"--headless",
                                (char *)"--menu",
                                (char *)"--saves",
                                saves,
                                (char *)"--settings",
                                file,
                                (char *)"--script",
                                (char *)"tests/run/settings/fixtures/again.script",
                                (char *)"--screenshot-dir",
                                again,
                                (char *)"--width",
                                (char *)"640",
                                (char *)"--height",
                                (char *)"480",
                                nullptr};
    run_program(t, argv_again, &process);
    CHECK_CONTAINS(process.output, "render distance 12, full detail 8");
    picture_t opened;
    REQUIRE(picture_load(t, path_join(t, again, "opened.png"), &opened));
    const uint32_t far_from_8 = picture_different(&at_14, &at_8);
    const uint32_t fresh_12 = picture_different(&opened, &at_12);
    printf("thousandths different: 14 from 8 %" PRIu32 ", live 12 from a new run of 12: %" PRIu32 "\n", far_from_8,
           fresh_12);
    CHECK(far_from_8 > 5u);
    CHECK(fresh_12 <= 2u);
    const char *const keep = getenv("SETTINGS_PNG_DIR");
    if (keep != nullptr) {
        static const char *const names[] = {"at_8.png", "at_14.png", "at_12.png"};
        for (uint32_t i = 0u; i < sizeof(names) / sizeof(names[0]); i++) {
            uint64_t size = 0u;
            const uint8_t *const png = file_slurp(t, path_join(t, shots, names[i]), &size);
            REQUIRE(png != nullptr);
            char kept[64];
            snprintf(kept, sizeof(kept), "far_%s", names[i]);
            REQUIRE(file_write_atomic(path_join(t, keep, kept), png, size) == FILE_OK);
        }
    }
    temp_directory_remove(t, root);
}

TEST(from_the_title_screen_it_is_kept_for_the_first_world_and_a_full_disk_does_not_trap_anyone) {
    REQUIRE(t->arg_count >= 1);
    char *const root = temp_directory(t);
    REQUIRE(root != nullptr);
    char *const saves = path_join(t, root, "saves");
    char *const shots = path_join(t, root, "shots");
    char *const file = path_join(t, root, "settings.yaml");
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--saves",
                          saves,
                          (char *)"--settings",
                          file,
                          (char *)"--script",
                          (char *)"tests/run/settings/fixtures/title.script",
                          (char *)"--screenshot-dir",
                          shots,
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          nullptr};
    test_process_t process;
    run_program(t, argv, &process);
    uint64_t bytes = 0u;
    const uint8_t *const kept = file_slurp(t, file, &bytes);
    REQUIRE(kept != nullptr);
    /* 32 and the wheel's 5. */
    CHECK_CONTAINS((const char *)kept, "render_distance: 37\n");
    CHECK_CONTAINS((const char *)kept, "effect_shapes: false\n");
    picture_t title, screen, effects, left;
    REQUIRE(picture_load(t, path_join(t, shots, "title.png"), &title));
    REQUIRE(picture_load(t, path_join(t, shots, "screen.png"), &screen));
    REQUIRE(picture_load(t, path_join(t, shots, "effects.png"), &effects));
    REQUIRE(picture_load(t, path_join(t, shots, "left.png"), &left));
    CHECK(picture_different(&screen, &title) > 100u);
    CHECK(picture_different(&effects, &screen) > 20u);
    CHECK_EQ_U64(picture_different(&left, &title), 0u);

    /* No way to write the file: a directory stands where it goes. Done says so and stays, and Done again leaves. */
    char *const in_the_way = path_join(t, root, "in_the_way");
    REQUIRE(file_make_dirs(in_the_way) == FILE_OK);
    char *const shots_2 = path_join(t, root, "shots_2");
    char *const argv_full[] = {t->borrowed_args[0],
                               (char *)"--headless",
                               (char *)"--menu",
                               (char *)"--saves",
                               saves,
                               (char *)"--settings",
                               in_the_way,
                               (char *)"--script",
                               (char *)"tests/run/settings/fixtures/title.script",
                               (char *)"--screenshot-dir",
                               shots_2,
                               (char *)"--width",
                               (char *)"640",
                               (char *)"--height",
                               (char *)"480",
                               nullptr};
    run_program(t, argv_full, &process);
    picture_t stayed, title_2, left_2;
    REQUIRE(picture_load(t, path_join(t, shots_2, "title.png"), &title_2));
    REQUIRE(picture_load(t, path_join(t, shots_2, "left.png"), &stayed));
    REQUIRE(picture_load(t, path_join(t, shots_2, "left_again.png"), &left_2));
    CHECK(picture_different(&stayed, &title_2) > 100u); /* it is still the screen */
    CHECK_EQ_U64(picture_different(&left_2, &title_2), 0u);
    temp_directory_remove(t, root);
}

/* Pixels more than a little different inside a rectangle of both pictures, and outside it. */
static void picture_different_in(const picture_t *a, const picture_t *b, uint32_t right, uint32_t bottom,
                                 uint64_t *out_inside, uint64_t *out_outside) {
    *out_inside = 0u;
    *out_outside = 0u;
    for (uint32_t y = 0u; y < a->height; y++) {
        for (uint32_t x = 0u; x < a->width; x++) {
            const uint64_t at = ((uint64_t)y * a->width + x) * 4u;
            bool differs = false;
            for (uint32_t channel = 0u; channel < 3u; channel++) {
                const int32_t step = (int32_t)a->rgba[at + channel] - (int32_t)b->rgba[at + channel];
                differs = differs || step > 8 || step < -8;
            }
            if (differs) {
                *(x < right && y < bottom ? out_inside : out_outside) += 1u;
            }
        }
    }
}

/* A file from before the setting, so it comes up on; the box takes it off in a world that stays open, with nothing else changing; a new program finds it
 * off in the file, and on the screen. At 640 by 480 the panel is 2 from the corner, and well inside this. */
TEST(coordinates_come_off_live_in_the_built_program_and_stay_off_in_the_next) {
    REQUIRE(t->arg_count >= 1);
    enum : uint32_t { PANEL_RIGHT = 160u, PANEL_BOTTOM = 16u };
    char *const root = temp_directory(t);
    REQUIRE(root != nullptr);
    char *const saves = path_join(t, root, "saves");
    char *const shots = path_join(t, root, "shots");
    char *const again = path_join(t, root, "again");
    char *const file = path_join(t, root, "settings.yaml");
    settings_write(t, file, PROGRAM_START);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--saves",
                          saves,
                          (char *)"--settings",
                          file,
                          (char *)"--script",
                          (char *)"tests/run/settings/fixtures/coordinates.script",
                          (char *)"--screenshot-dir",
                          shots,
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          nullptr};
    test_process_t process;
    run_program(t, argv, &process);
    if (t->failure_count != 0u) {
        return;
    }
    CHECK_EQ_U64(count_of(process.output, "session: world 'live' is open"), 1u);
    CHECK(strstr(process.output, "game: now drawing") == nullptr); /* a box on the HUD asks nothing of the card */
    uint64_t bytes = 0u;
    const uint8_t *const kept = file_slurp(t, file, &bytes);
    REQUIRE(kept != nullptr);
    CHECK_CONTAINS((const char *)kept, "show_coordinates: false\n");
    picture_t on, off, on_again, opened;
    REQUIRE(picture_load(t, path_join(t, shots, "on.png"), &on));
    REQUIRE(picture_load(t, path_join(t, shots, "off.png"), &off));
    REQUIRE(picture_load(t, path_join(t, shots, "on_again.png"), &on_again));
    uint64_t inside = 0u, outside = 0u;
    picture_different_in(&on, &off, PANEL_RIGHT, PANEL_BOTTOM, &inside, &outside);
    printf("on from off: %" PRIu64 " pixels in the corner, %" PRIu64 " past it\n", inside, outside);
    CHECK(inside > 200u);
    CHECK(outside < 1000u); /* of 307200: the world stands still, near enough */
    picture_different_in(&on, &on_again, PANEL_RIGHT, PANEL_BOTTOM, &inside, &outside);
    printf("on from on again: %" PRIu64 " pixels in the corner, %" PRIu64 " past it\n", inside, outside);
    CHECK(inside < 10u);

    char *const argv_again[] = {t->borrowed_args[0],
                                (char *)"--headless",
                                (char *)"--menu",
                                (char *)"--saves",
                                saves,
                                (char *)"--settings",
                                file,
                                (char *)"--script",
                                (char *)"tests/run/settings/fixtures/again.script",
                                (char *)"--screenshot-dir",
                                again,
                                (char *)"--width",
                                (char *)"640",
                                (char *)"--height",
                                (char *)"480",
                                nullptr};
    run_program(t, argv_again, &process);
    CHECK_CONTAINS(process.output, "session: world 'live' is open");
    REQUIRE(picture_load(t, path_join(t, again, "opened.png"), &opened));
    picture_different_in(&opened, &off, PANEL_RIGHT, PANEL_BOTTOM, &inside, &outside);
    printf("a new run from off: %" PRIu64 " pixels in the corner, %" PRIu64 " past it\n", inside, outside);
    CHECK(inside < 10u);
    picture_different_in(&opened, &on, PANEL_RIGHT, PANEL_BOTTOM, &inside, &outside);
    printf("a new run from on: %" PRIu64 " pixels in the corner, %" PRIu64 " past it\n", inside, outside);
    CHECK(inside > 200u);
    const char *const keep = getenv("SETTINGS_PNG_DIR");
    if (keep != nullptr) {
        static const char *const names[] = {"on.png", "off.png", "on_again.png", "screen_coordinates.png"};
        for (uint32_t i = 0u; i < sizeof(names) / sizeof(names[0]); i++) {
            uint64_t size = 0u;
            const uint8_t *const png = file_slurp(t, path_join(t, shots, names[i]), &size);
            REQUIRE(png != nullptr);
            REQUIRE(file_write_atomic(path_join(t, keep, names[i]), png, size) == FILE_OK);
        }
    }
    temp_directory_remove(t, root);
}

TEST(a_flag_past_what_settings_can_hold_is_held_to_it) {
    REQUIRE(t->arg_count >= 1);
    char *const root = temp_directory(t);
    REQUIRE(root != nullptr);
    char *const saves = path_join(t, root, "saves");
    char *const shots = path_join(t, root, "shots");
    char *const file = path_join(t, root, "settings.yaml");
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--render-distance",
                          (char *)"1",
                          (char *)"--full-detail",
                          (char *)"99",
                          (char *)"--saves",
                          saves,
                          (char *)"--settings",
                          file,
                          (char *)"--script",
                          (char *)"tests/run/settings/fixtures/title.script",
                          (char *)"--screenshot-dir",
                          shots,
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          nullptr};
    test_process_t process;
    run_program(t, argv, &process);
    uint64_t bytes = 0u;
    const uint8_t *const kept = file_slurp(t, file, &bytes);
    REQUIRE(kept != nullptr);
    /* 2, the least a slider has, and the script's 5. */
    CHECK_CONTAINS((const char *)kept, "render_distance: 7\n");
    CHECK_CONTAINS((const char *)kept, "full_detail: 48\n");
    temp_directory_remove(t, root);
}

TEST(the_program_refuses_settings_without_a_menu) {
    REQUIRE(t->arg_count >= 1);
    char *const game = t->borrowed_args[0];
    char *const lone[] = {game, (char *)"--settings", (char *)"/tmp/x", nullptr};
    test_process_t process;
    test_process_run(t, lone, 60000u, &process);
    CHECK_EQ_I64(process.exit_code, 2);
    CHECK_CONTAINS(process.output, "--settings only means something with --menu");
}

TEST_MAIN()
