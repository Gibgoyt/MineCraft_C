/* ctest run_ui_census. See README.md. Plan 004 phase 07's census screen with a report: its layout at three scales, golden
 * pictures at 640 by 480 and 1280 by 720 with every event, 8 named systems and the failed line, Continue greyed until the file
 * is written, the progress bar, and no heap in 200 frames. */
#include "harness/index.h"

#include "calendar/index.h"
#include "climate/systems/index.h"
#include "display/headless/png/index.h"
#include "file/index.h"
#include "heap/index.h"
#include "texture/index.h"
#include "ui/index.h"
#include "ui/screen_census/index.h"

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    UC_WIDTH_MAX = 1280u,
    UC_HEIGHT_MAX = 720u,
};

typedef struct uc_rig_t {
    texture_t *texture; /* 0 */
    ui_t *ui;           /* 8 */
    uint8_t *picture;   /* 16: a gradient, for the world under the screen, UC_WIDTH_MAX by UC_HEIGHT_MAX */
    uint8_t *out;       /* 24: the frame drawn */
    uint32_t width;     /* 32 */
    uint32_t height;    /* 36 */
} uc_rig_t;
static_assert(sizeof(uc_rig_t) == 40);

static void *uc_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool uc_start(test_context_t *t, uc_rig_t *rig) {
    memset(rig, 0, sizeof(*rig));
    rig->texture = uc_aligned(t, sizeof(texture_t));
    rig->ui = uc_aligned(t, sizeof(ui_t));
    const uint64_t bytes = (uint64_t)UC_WIDTH_MAX * UC_HEIGHT_MAX * 4u;
    rig->picture = test_alloc(t, bytes);
    rig->out = test_alloc(t, bytes);
    return texture_init(rig->texture) && ui_init(rig->ui, rig->texture);
}

static void uc_stop(uc_rig_t *rig) {
    ui_deinit(rig->ui);
    texture_deinit(rig->texture);
}

static void uc_size(uc_rig_t *rig, uint32_t width, uint32_t height) {
    rig->width = width;
    rig->height = height;
    uint32_t *const pixels = (uint32_t *)(void *)rig->picture;
    for (uint32_t y = 0u; y < height; y++) {
        for (uint32_t x = 0u; x < width; x++) {
            pixels[y * width + x] = 0xFF000000u | ((x * 255u / width) << 16u) | ((y * 255u / height) << 8u) | 0x60u;
        }
    }
}

/* A report with something in every part: all 8 events, 4 hurricanes named, 8 systems, and the file failed. */
static void uc_report_full(ui_census_report_t *r) {
    memset(r, 0, sizeof(*r));
    r->here_known = 1u;
    r->here.t_max = 31.4f32;
    r->here.t_min = -2.6f32;
    r->here.rain_mm = 187.0f32;
    r->here.snow_mm = 12.0f32;
    r->here.wind[0] = -6.0f32;
    r->here.wind[1] = -4.0f32;
    r->here.day_kind[2] = CLIMATE_DAY_SNOW;
    for (uint32_t e = 0u; e < UI_CENSUS_EVENT_COUNT; e++) {
        r->event_cells[e] = 3u + 41u * e;
    }
    static const char *const NAMES[UI_CENSUS_NAMES_MAX] = {"Hurricane Tavela", "Hurricane Orin",
                                                           "Tropical storm Mabeku", "Hurricane Zolarine"};
    for (uint32_t i = 0u; i < UI_CENSUS_NAMES_MAX; i++) {
        snprintf(r->hurricane_names[i], UI_CENSUS_NAME_BYTES, "%s", NAMES[i]);
    }
    r->hurricane_count = UI_CENSUS_NAMES_MAX;
    r->outlook_known = 1u;
    climate_outlook_t *const o = &r->outlook;
    for (uint32_t d = 0u; d < CLIMATE_OUTLOOK_DAYS; d++) {
        o->days[d].rain = 0.15f32 * (float32_t)d;
        o->days[d].thunder = 0.05f32 * (float32_t)d;
        o->days[d].storm = d == 1u ? 0.93f32 : 0.1f32;
        o->days[d].hurricane = d == 1u ? 0.93f32 : 0.02f32;
        o->days[d].high_c = 28.0f32 - (float32_t)d;
        o->days[d].low_c = -12.0f32 + (float32_t)d;
        o->days[d].spread_c = 4.5f32;
    }
    static const char *const SYSTEMS[CLIMATE_OUTLOOK_SYSTEMS_MAX] = {
        "Hurricane Tavela", "Tropical storm Zolarine", "Low Orin", "Low Kaka", "High Mabeku", "High Zo",
        "Low Ella",         "Hurricane Rinni"};
    o->system_count = CLIMATE_OUTLOOK_SYSTEMS_MAX;
    for (uint32_t s = 0u; s < CLIMATE_OUTLOOK_SYSTEMS_MAX; s++) {
        snprintf(o->systems[s].name, sizeof(o->systems[s].name), "%s", SYSTEMS[s]);
        o->systems[s].id = 100u + s;
        o->systems[s].kind = s == 0u || s == 1u || s == 7u ? CLIMATE_SYSTEM_HURRICANE
                             : (s == 4u || s == 5u)        ? CLIMATE_SYSTEM_HIGH
                                                           : CLIMATE_SYSTEM_LOW;
        for (uint32_t d = 0u; d < CLIMATE_OUTLOOK_DAYS; d++) {
            o->systems[s].chance[d] = d == (s % 7u) ? 0.93f32 - 0.1f32 * (float32_t)s : 0.01f32;
        }
    }
    r->week = 154u;
    r->weeks_total = 14u;
    r->weeks_done = 14u;
    r->tiles_total = 100u;
    r->tiles_done = 100u;
    r->failed = 1u;
    r->failed_status = FILE_ERROR_DISK_FULL;
}

/* A frame at the rig's size, with the pointer moved by (dx, dy), a click if asked, and Esc if asked. */
static ui_screen_census_action_t uc_frame(uc_rig_t *rig, const ui_census_report_t *report, int64_t dx, int64_t dy,
                                          bool press, bool release, bool escape) {
    ui_t *const ui = rig->ui;
    input_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.mouse_dx = dx;
    frame.mouse_dy = dy;
    if (press) {
        frame.pressed.words[BTN_LEFT / 64u] |= 1ull << (BTN_LEFT % 64u);
        frame.down.words[BTN_LEFT / 64u] |= 1ull << (BTN_LEFT % 64u);
    }
    if (release) {
        frame.released.words[BTN_LEFT / 64u] |= 1ull << (BTN_LEFT % 64u);
    }
    ui_widget_input_feed(&ui->widget, &frame);
    if (!ui_frame_begin_over(ui, rig->picture, rig->width, rig->height)) {
        return (ui_screen_census_action_t)255u;
    }
    const ui_screen_census_action_t action = ui_screen_census_frame(&ui->census_screen, &ui->widget, escape, report);
    memcpy(rig->out, ui_frame_end(ui, false), (uint64_t)rig->width * rig->height * 4u);
    return action;
}

static void uc_golden(test_context_t *t, const uc_rig_t *rig, const char *path) {
    const uint64_t capacity = display_png_bytes_needed(rig->width, rig->height);
    uint8_t *const ours = test_alloc(t, capacity);
    uint8_t *const file = test_alloc(t, capacity);
    uint64_t written = 0u;
    REQUIRE(display_png_encode(rig->out, rig->width * 4u, rig->width, rig->height, ours, capacity, &written));
    if (getenv("MC_GOLDEN_WRITE") != nullptr) {
        REQUIRE(file_write_atomic(path, ours, written) == FILE_OK);
        printf("  wrote %s\n", path);
        return;
    }
    uint64_t length = 0u;
    if (!CHECK_EQ_U64(file_read_whole(path, file, capacity, &length), FILE_OK)) {
        printf("  cannot read %s: run with MC_GOLDEN_WRITE=1, then look at what it wrote\n", path);
        return;
    }
    uint64_t wrong = 0u;
    for (uint64_t i = 0u; i < written && i < length; i++) {
        wrong += ours[i] != file[i] ? 1u : 0u;
    }
    printf("  %s: %" PRIu64 " bytes differ\n", path, wrong);
    CHECK_EQ_U64(length, written);
    CHECK_EQ_U64(wrong, 0u);
}

static const calendar_clock_t UC_CLOCK = 1078ull * CALENDAR_CLOCK_DAY + CALENDAR_CLOCK_DAY_START; /* week 165 */

TEST(golden_pictures_with_every_part_of_the_report) {
    uc_rig_t rig;
    REQUIRE(uc_start(t, &rig));
    ui_census_report_t *const report = test_alloc(t, sizeof(ui_census_report_t));
    uc_report_full(report);
    static const uint32_t SIZES[2][2] = {{640u, 480u}, {1280u, 720u}};
    static const char *const PATHS[2] = {"tests/run/ui/census/fixtures/report_640x480.png",
                                         "tests/run/ui/census/fixtures/report_1280x720.png"};
    for (uint32_t s = 0u; s < 2u; s++) {
        uc_size(&rig, SIZES[s][0], SIZES[s][1]);
        ui_screen_census_open(&rig.ui->census_screen, &rig.ui->widget, CALENDAR_DAY_ZERO, UC_CLOCK, 14u, false);
        /* The pointer to the top left corner, far from the button, so the picture has no hover in it. */
        (void)uc_frame(&rig, report, -100000, -100000, false, false, false);
        CHECK_EQ_U64(uc_frame(&rig, report, 0, 0, false, false, false), UI_SCREEN_CENSUS_NONE);
        uc_golden(t, &rig, PATHS[s]);
    }
    /* The working picture: a progress bar and Continue greyed. */
    uc_size(&rig, 640u, 480u);
    report->failed = 0u;
    report->written = 0u;
    report->weeks_done = 2u;
    report->tiles_done = 41u;
    (void)uc_frame(&rig, report, 0, 0, false, false, false);
    uc_golden(t, &rig, "tests/run/ui/census/fixtures/working_640x480.png");
    uc_stop(&rig);
}

TEST(the_layout_fits_at_three_scales) {
    /* By hand: the screen is 320 by 240 units, centred, times the scale the widget picks; Continue is 200 by 20 units at 60, 212. */
    static const uint32_t SIZES[3][3] = {{320u, 240u, 1u}, {640u, 480u, 2u}, {960u, 720u, 3u}};
    uc_rig_t rig;
    REQUIRE(uc_start(t, &rig));
    ui_census_report_t *const report = test_alloc(t, sizeof(ui_census_report_t));
    uc_report_full(report);
    report->failed = 0u;
    report->written = 1u;
    for (uint32_t s = 0u; s < 3u; s++) {
        const uint32_t width = SIZES[s][0], height = SIZES[s][1], scale = SIZES[s][2];
        CHECK_EQ_U64(ui_widget_scale_for(width, height), scale);
        uc_size(&rig, width, height);
        ui_screen_census_open(&rig.ui->census_screen, &rig.ui->widget, CALENDAR_DAY_ZERO, UC_CLOCK, 1u, false);
        (void)uc_frame(&rig, report, -100000, -100000, false, false, false);
        /* Nothing is drawn outside the screen's 320 by 240 units but the shade: the corners outside it are the shaded world. */
        const int32_t left = ((int32_t)width - 320 * (int32_t)scale) / 2;
        const int32_t top = ((int32_t)height - 240 * (int32_t)scale) / 2;
        CHECK(left >= 0 && top >= 0);
        /* Continue's middle, by hand, presses it, and a click a unit above its top edge does not. */
        const int32_t button_x = left + 160 * (int32_t)scale, button_y = top + 222 * (int32_t)scale;
        const int32_t above_y = top + 210 * (int32_t)scale;
        (void)uc_frame(&rig, report, (int64_t)button_x - rig.ui->widget.input.pointer_x,
                       (int64_t)above_y - rig.ui->widget.input.pointer_y, false, false, false);
        const ui_screen_census_action_t a = uc_frame(&rig, report, 0, 0, true, false, false);
        const ui_screen_census_action_t b = uc_frame(&rig, report, 0, 0, false, true, false);
        CHECK(a == UI_SCREEN_CENSUS_NONE && b == UI_SCREEN_CENSUS_NONE);
        (void)uc_frame(&rig, report, 0, (int64_t)button_y - rig.ui->widget.input.pointer_y, false, false, false);
        const ui_screen_census_action_t c = uc_frame(&rig, report, 0, 0, true, false, false);
        const ui_screen_census_action_t d = uc_frame(&rig, report, 0, 0, false, true, false);
        printf("  %ux%u at scale %u: the screen at %d, %d; Continue %s\n", width, height, scale, left, top,
               (c == UI_SCREEN_CENSUS_CONTINUE) != (d == UI_SCREEN_CENSUS_CONTINUE) ? "pressed" : "not pressed");
        CHECK((c == UI_SCREEN_CENSUS_CONTINUE) != (d == UI_SCREEN_CENSUS_CONTINUE));
    }
    uc_stop(&rig);
}

TEST(continue_is_greyed_until_the_file_is_written) {
    uc_rig_t rig;
    REQUIRE(uc_start(t, &rig));
    uc_size(&rig, 640u, 480u);
    ui_census_report_t *const report = test_alloc(t, sizeof(ui_census_report_t));
    uc_report_full(report);
    report->failed = 0u;
    report->written = 0u;
    ui_screen_census_open(&rig.ui->census_screen, &rig.ui->widget, CALENDAR_DAY_ZERO, UC_CLOCK, 1u, false);
    (void)uc_frame(&rig, report, -100000, -100000, false, false, false);
    const int64_t x = 320 - rig.ui->widget.input.pointer_x, y = 444 - rig.ui->widget.input.pointer_y;
    (void)uc_frame(&rig, report, x, y, false, false, false);
    uint32_t answered = 0u;
    for (uint32_t i = 0u; i < 5u; i++) {
        answered += uc_frame(&rig, report, 0, 0, true, false, false) != UI_SCREEN_CENSUS_NONE ? 1u : 0u;
        answered += uc_frame(&rig, report, 0, 0, false, true, false) != UI_SCREEN_CENSUS_NONE ? 1u : 0u;
        answered += uc_frame(&rig, report, 0, 0, false, false, true) != UI_SCREEN_CENSUS_NONE ? 1u : 0u; /* Esc */
    }
    CHECK_EQ_U64(answered, 0u);
    report->written = 1u;
    CHECK_EQ_U64(uc_frame(&rig, report, 0, 0, false, false, true), UI_SCREEN_CENSUS_CONTINUE);
    const ui_screen_census_action_t a = uc_frame(&rig, report, 0, 0, true, false, false);
    const ui_screen_census_action_t b = uc_frame(&rig, report, 0, 0, false, true, false);
    CHECK((a == UI_SCREEN_CENSUS_CONTINUE) != (b == UI_SCREEN_CENSUS_CONTINUE));
    /* A failed file lets the game go on too. */
    report->written = 0u;
    report->failed = 1u;
    CHECK_EQ_U64(uc_frame(&rig, report, 0, 0, false, false, true), UI_SCREEN_CENSUS_CONTINUE);
    /* And the compass. */
    CHECK_STR_EQ(ui_screen_census_wind_from(0.0f32, -5.0f32), "N");
    CHECK_STR_EQ(ui_screen_census_wind_from(-5.0f32, 0.0f32), "E");
    CHECK_STR_EQ(ui_screen_census_wind_from(4.0f32, 4.0f32), "SW");
    CHECK_STR_EQ(ui_screen_census_wind_from(0.1f32, 0.1f32), "calm");
    uc_stop(&rig);
}

TEST(no_heap_in_200_frames) {
    uc_rig_t rig;
    REQUIRE(uc_start(t, &rig));
    uc_size(&rig, 640u, 480u);
    ui_census_report_t *const report = test_alloc(t, sizeof(ui_census_report_t));
    uc_report_full(report);
    ui_screen_census_open(&rig.ui->census_screen, &rig.ui->widget, CALENDAR_DAY_ZERO, UC_CLOCK, 14u, false);
    (void)uc_frame(&rig, report, 0, 0, false, false, false);
    heap_counters_t before, after;
    heap_counters_read(&before);
    for (uint32_t i = 0u; i < 200u; i++) {
        report->written = (uint8_t)(i % 2u);
        report->tiles_done = i % 100u;
        (void)uc_frame(&rig, report, (int64_t)(i % 7u) - 3, (int64_t)(i % 5u) - 2, false, false, false);
    }
    heap_counters_read(&after);
    printf("  200 frames: %" PRIu64 " reserves before, %" PRIu64 " after\n", before.reserve_calls, after.reserve_calls);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    uc_stop(&rig);
}

TEST_MAIN()
