/* ctest run_display_mode. See README.md. Nothing here opens a card or a console: every test but the last section runs
 * against the fake kernel below, and the last section only ever gives the real program /dev/null for stdin and a card
 * path that does not exist. */
#include "harness/index.h"

#include "display/index.h"
#include "heap/index.h"
#include "machine/index.h"
#include "main/index.h"
#include "run/display/mode/fixtures/fake_card.h"
#include "time/index.h"

#include <drm/drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* ---- Fixture modes ----------------------------------------------------------------------------------------------- */

/* Real timings, as a monitor's EDID gives them: 148.5 MHz over 2200x1125 is the 60 Hz of CEA-861. */
static struct drm_mode_modeinfo mode_timed(uint16_t width, uint16_t height, uint32_t clock_khz, uint16_t htotal,
                                           uint16_t vtotal) {
    struct drm_mode_modeinfo mode = {0};
    mode.hdisplay = width;
    mode.vdisplay = height;
    mode.clock = clock_khz;
    mode.htotal = htotal;
    mode.vtotal = vtotal;
    return mode;
}

/* A mode of this size at this refresh. Totals that divide well keep the refresh exact to the millihertz. */
static struct drm_mode_modeinfo mode_at(uint16_t width, uint16_t height, uint32_t refresh_hz) {
    const uint16_t htotal = 2000u + 1000u;
    const uint16_t vtotal = 1000u + 1000u;
    return mode_timed(width, height, (uint32_t)htotal * vtotal / 1000u * refresh_hz, htotal, vtotal);
}

static const display_mode_wanted_t wanted_1080p120 = {
    .connector_name = nullptr, .refresh_mhz = 120000u, .width = 1920u, .height = 1080u};

TEST(refresh_comes_from_the_timings_and_not_the_rounded_vrefresh) {
    const struct drm_mode_modeinfo cea_60 = mode_timed(1920u, 1080u, 148500u, 2200u, 1125u);
    CHECK_EQ_U64(display_mode_refresh_mhz(&cea_60), 60000u);
    const struct drm_mode_modeinfo cea_120 = mode_timed(1920u, 1080u, 297000u, 2200u, 1125u);
    CHECK_EQ_U64(display_mode_refresh_mhz(&cea_120), 120000u);
    struct drm_mode_modeinfo ntsc_120 = mode_timed(1920u, 1080u, 296703u, 2200u, 1125u);
    ntsc_120.vrefresh = 120u;
    CHECK_EQ_U64(display_mode_refresh_mhz(&ntsc_120), 119880u);

    struct drm_mode_modeinfo doubled = cea_120;
    doubled.flags = DRM_MODE_FLAG_DBLSCAN;
    CHECK_EQ_U64(display_mode_refresh_mhz(&doubled), 60000u);
    struct drm_mode_modeinfo scanned = cea_120;
    scanned.vscan = 3u;
    CHECK_EQ_U64(display_mode_refresh_mhz(&scanned), 40000u);
    scanned.vscan = 1u; /* 0 and 1 both mean once */
    CHECK_EQ_U64(display_mode_refresh_mhz(&scanned), 120000u);

    const struct drm_mode_modeinfo no_clock = mode_timed(1920u, 1080u, 0u, 2200u, 1125u);
    const struct drm_mode_modeinfo no_total = mode_timed(1920u, 1080u, 297000u, 0u, 1125u);
    CHECK_EQ_U64(display_mode_refresh_mhz(&no_clock), 0u);
    CHECK_EQ_U64(display_mode_refresh_mhz(&no_total), 0u);
}

TEST(modes_nobody_can_flip_on_are_not_usable) {
    struct drm_mode_modeinfo mode = mode_at(1920u, 1080u, 120u);
    CHECK(display_mode_is_usable(&mode));
    mode.flags = DRM_MODE_FLAG_INTERLACE;
    CHECK(!display_mode_is_usable(&mode));
    mode.flags = DRM_MODE_FLAG_3D_FRAME_PACKING;
    CHECK(!display_mode_is_usable(&mode));
    mode = mode_at(0u, 1080u, 120u);
    CHECK(!display_mode_is_usable(&mode));
    mode = mode_at(1920u, 0u, 120u);
    CHECK(!display_mode_is_usable(&mode));
    mode = mode_timed(1920u, 1080u, 0u, 2200u, 1125u);
    CHECK(!display_mode_is_usable(&mode));
}

TEST(tiers_put_the_size_and_refresh_in_the_spikes_order) {
    const struct drm_mode_modeinfo exact = mode_at(1920u, 1080u, 120u);
    const struct drm_mode_modeinfo fast_other_size = mode_at(2560u, 1440u, 120u);
    const struct drm_mode_modeinfo slow_exact = mode_at(1920u, 1080u, 60u);
    const struct drm_mode_modeinfo neither = mode_at(1280u, 720u, 60u);
    CHECK_EQ_U64(display_mode_tier(&exact, &wanted_1080p120), 4u);
    CHECK_EQ_U64(display_mode_tier(&fast_other_size, &wanted_1080p120), 3u);
    CHECK_EQ_U64(display_mode_tier(&slow_exact, &wanted_1080p120), 2u);
    CHECK_EQ_U64(display_mode_tier(&neither, &wanted_1080p120), 1u);

    /* 119.88 Hz is what a "120 Hz" monitor often is. 118.8 Hz is not: more than 1 Hz short. */
    const struct drm_mode_modeinfo ntsc = mode_timed(1920u, 1080u, 296703u, 2200u, 1125u);
    const struct drm_mode_modeinfo short_of_it = mode_timed(1920u, 1080u, 294030u, 2200u, 1125u);
    CHECK_EQ_U64(display_mode_tier(&ntsc, &wanted_1080p120), 4u);
    CHECK_EQ_U64(display_mode_tier(&short_of_it, &wanted_1080p120), 2u);
}

TEST(pick_finds_the_wanted_mode_wherever_it_is_in_the_list) {
    const struct drm_mode_modeinfo modes[] = {
        mode_at(1280u, 720u, 60u),   mode_at(3840u, 2160u, 30u), mode_at(1920u, 1080u, 60u),
        mode_at(1920u, 1080u, 120u), mode_at(1024u, 768u, 75u),
    };
    const struct drm_mode_modeinfo *const best = display_mode_pick(modes, 5u, &wanted_1080p120);
    CHECK(best == &modes[3]);
}

TEST(pick_prefers_the_nearest_refresh_when_the_wanted_one_is_met) {
    const struct drm_mode_modeinfo modes[] = {
        mode_at(1920u, 1080u, 240u),
        mode_at(1920u, 1080u, 144u),
        mode_at(1920u, 1080u, 120u),
        mode_at(1920u, 1080u, 165u),
    };
    CHECK(display_mode_pick(modes, 4u, &wanted_1080p120) == &modes[2]);
    /* With 120 itself gone, 144 is nearer than 165 or 240. */
    CHECK(display_mode_pick(modes, 2u, &wanted_1080p120) == &modes[1]);

    /* Exactly 120 Hz beats 119.88 Hz. Either order gives the same answer. */
    const struct drm_mode_modeinfo close[] = {mode_timed(1920u, 1080u, 296703u, 2200u, 1125u),
                                              mode_timed(1920u, 1080u, 297000u, 2200u, 1125u)};
    CHECK(display_mode_pick(close, 2u, &wanted_1080p120) == &close[1]);
    const struct drm_mode_modeinfo close_swapped[] = {close[1], close[0]};
    CHECK(display_mode_pick(close_swapped, 2u, &wanted_1080p120) == &close_swapped[0]);
}

TEST(pick_takes_the_fastest_when_the_wanted_refresh_is_not_there) {
    const struct drm_mode_modeinfo modes[] = {
        mode_at(1920u, 1080u, 30u),
        mode_at(1920u, 1080u, 75u),
        mode_at(1920u, 1080u, 60u),
    };
    CHECK(display_mode_pick(modes, 3u, &wanted_1080p120) == &modes[1]);
}

TEST(pick_lets_a_fast_mode_of_another_size_beat_a_slow_exact_one) {
    const struct drm_mode_modeinfo modes[] = {mode_at(1920u, 1080u, 60u), mode_at(2560u, 1440u, 120u)};
    CHECK(display_mode_pick(modes, 2u, &wanted_1080p120) == &modes[1]);
}

TEST(pick_breaks_a_tie_by_what_the_monitor_prefers_then_by_area) {
    struct drm_mode_modeinfo modes[] = {mode_at(2560u, 1440u, 120u), mode_at(1280u, 720u, 120u)};
    CHECK(display_mode_pick(modes, 2u, &wanted_1080p120) == &modes[0]);
    modes[1].type = DRM_MODE_TYPE_PREFERRED;
    CHECK(display_mode_pick(modes, 2u, &wanted_1080p120) == &modes[1]);
}

TEST(pick_skips_what_is_not_usable_and_may_find_nothing) {
    struct drm_mode_modeinfo modes[] = {mode_at(1920u, 1080u, 120u), mode_at(1920u, 1080u, 60u)};
    modes[0].flags = DRM_MODE_FLAG_INTERLACE;
    CHECK(display_mode_pick(modes, 2u, &wanted_1080p120) == &modes[1]);
    modes[1].flags = DRM_MODE_FLAG_INTERLACE;
    CHECK(display_mode_pick(modes, 2u, &wanted_1080p120) == nullptr);
    CHECK(display_mode_pick(modes, 0u, &wanted_1080p120) == nullptr);
}

TEST(connector_names_match_sysfs) {
    char name[DISPLAY_MODE_NAME_BYTES];
    display_mode_connector_name(DRM_MODE_CONNECTOR_HDMIA, 1u, name, sizeof(name));
    CHECK_STR_EQ(name, "HDMI-A-1");
    display_mode_connector_name(DRM_MODE_CONNECTOR_eDP, 1u, name, sizeof(name));
    CHECK_STR_EQ(name, "eDP-1");
    display_mode_connector_name(DRM_MODE_CONNECTOR_DisplayPort, 2u, name, sizeof(name));
    CHECK_STR_EQ(name, "DP-2");
    display_mode_connector_name(999u, 3u, name, sizeof(name));
    CHECK_STR_EQ(name, "Unknown-3");
}

static display_connector_t connector_with(uint32_t type, uint32_t type_id, struct drm_mode_modeinfo best) {
    display_connector_t connector;
    memset(&connector, 0, sizeof(connector));
    connector.type = type;
    connector.type_id = type_id;
    connector.best_mode = best;
    connector.best_mhz = display_mode_refresh_mhz(&best);
    return connector;
}

TEST(auto_takes_the_connector_whose_best_mode_is_best) {
    const display_connector_t connectors[] = {
        connector_with(DRM_MODE_CONNECTOR_eDP, 1u, mode_at(1920u, 1080u, 60u)),
        connector_with(DRM_MODE_CONNECTOR_HDMIA, 1u, mode_at(1920u, 1080u, 120u)),
        connector_with(DRM_MODE_CONNECTOR_DisplayPort, 1u, mode_at(1920u, 1080u, 75u)),
    };
    CHECK(display_mode_pick_connector(connectors, 3u, &wanted_1080p120) == &connectors[1]);

    display_mode_wanted_t wanted = wanted_1080p120;
    wanted.connector_name = "auto";
    CHECK(display_mode_pick_connector(connectors, 3u, &wanted) == &connectors[1]);
    CHECK(display_mode_pick_connector(connectors, 0u, &wanted) == nullptr);
}

TEST(auto_keeps_the_earliest_connector_on_a_tie) {
    const display_connector_t connectors[] = {
        connector_with(DRM_MODE_CONNECTOR_eDP, 1u, mode_at(1920u, 1080u, 120u)),
        connector_with(DRM_MODE_CONNECTOR_HDMIA, 1u, mode_at(1920u, 1080u, 120u)),
    };
    CHECK(display_mode_pick_connector(connectors, 2u, &wanted_1080p120) == &connectors[0]);
}

TEST(a_named_connector_wins_even_when_slower_and_never_falls_back) {
    const display_connector_t connectors[] = {
        connector_with(DRM_MODE_CONNECTOR_HDMIA, 1u, mode_at(1920u, 1080u, 120u)),
        connector_with(DRM_MODE_CONNECTOR_eDP, 1u, mode_at(1920u, 1080u, 60u)),
    };
    display_mode_wanted_t wanted = wanted_1080p120;
    wanted.connector_name = "eDP-1";
    CHECK(display_mode_pick_connector(connectors, 2u, &wanted) == &connectors[1]);
    wanted.connector_name = "DP-7";
    CHECK(display_mode_pick_connector(connectors, 2u, &wanted) == nullptr);
}

/* ---- Refusing ---------------------------------------------------------------------------------------------------- */

static void check_refused_untouched(test_context_t *t, fake_card_t *card, display_refusal_t refusal) {
    display_config_t config = fake_config();
    display_t *const display = test_alloc(t, sizeof(display_t));
    CHECK_EQ_U64(display_init(display, &config, &fake_ops, card), DISPLAY_STATUS_REFUSED);
    CHECK_EQ_U64(display->refusal, refusal);
    /* The card was never opened, and nothing asked was able to change anything. */
    CHECK_EQ_U64(card->open_count, 0u);
    CHECK_EQ_U64(card->call_count, 0u);
    display_deinit(display);
    check_nothing_left(t, card);
}

TEST(a_session_of_x_is_refused_before_anything_else) {
    fake_card_t *const card = fake_card_new(t);
    card->env_display = ":0";
    check_refused_untouched(t, card, DISPLAY_REFUSAL_X_SESSION);
}

TEST(a_session_of_wayland_is_refused_before_anything_else) {
    fake_card_t *const card = fake_card_new(t);
    card->env_wayland = "wayland-0";
    check_refused_untouched(t, card, DISPLAY_REFUSAL_WAYLAND_SESSION);
}

TEST(an_empty_display_variable_does_not_mean_a_session) {
    fake_card_t *const card = fake_card_new(t);
    card->env_display = "";
    card->env_wayland = "";
    uint32_t minor = 0;
    CHECK_EQ_U64(display_device_check_console(&fake_ops, card, FAKE_CONSOLE_FD, &minor), DISPLAY_REFUSAL_NONE);
    CHECK_EQ_U64(minor, 3u);
}

TEST(stdin_must_be_a_virtual_console) {
    fake_card_t *card = fake_card_new(t);
    card->console_major = 0u; /* a pipe or a file */
    check_refused_untouched(t, card, DISPLAY_REFUSAL_NOT_A_VT);

    card = fake_card_new(t);
    card->console_major = 136u; /* a pseudo terminal: xterm, ssh */
    check_refused_untouched(t, card, DISPLAY_REFUSAL_NOT_A_VT);

    card = fake_card_new(t);
    card->console_major = 1u; /* /dev/null */
    card->console_minor = 3u;
    check_refused_untouched(t, card, DISPLAY_REFUSAL_NOT_A_VT);

    card = fake_card_new(t);
    card->console_minor = 0u; /* /dev/tty0 */
    check_refused_untouched(t, card, DISPLAY_REFUSAL_NOT_A_VT);

    card = fake_card_new(t);
    card->console_minor = 64u; /* /dev/ttyS0 has the same major */
    check_refused_untouched(t, card, DISPLAY_REFUSAL_NOT_A_VT);
}

TEST(the_console_must_be_the_one_on_screen) {
    fake_card_t *const card = fake_card_new(t);
    card->active_vt = 1u;
    check_refused_untouched(t, card, DISPLAY_REFUSAL_VT_NOT_VISIBLE);
}

TEST(the_console_must_be_in_text_mode) {
    fake_card_t *const card = fake_card_new(t);
    card->kd_mode = KD_GRAPHICS;
    check_refused_untouched(t, card, DISPLAY_REFUSAL_VT_NOT_TEXT);
}

TEST(a_card_that_will_not_open_is_refused_with_the_reason) {
    const int32_t errors[] = {EBUSY, EINVAL, EACCES, EPERM, ENOENT};
    const display_refusal_t refusals[] = {DISPLAY_REFUSAL_CARD_BUSY, DISPLAY_REFUSAL_CARD_BUSY,
                                          DISPLAY_REFUSAL_CARD_ACCESS, DISPLAY_REFUSAL_CARD_ACCESS,
                                          DISPLAY_REFUSAL_CARD_OPEN};
    for (uint32_t i = 0; i < 5u; i++) {
        fake_card_t *const card = fake_card_new(t);
        card->open_errno = errors[i];
        display_config_t config = fake_config();
        display_t *const display = test_alloc(t, sizeof(display_t));
        CHECK_EQ_U64(display_init(display, &config, &fake_ops, card), DISPLAY_STATUS_REFUSED);
        CHECK_EQ_U64(display->refusal, refusals[i]);
        CHECK_EQ_U64(card->open_count, 1u);
        CHECK_EQ_U64(card->call_count, 0u);
        check_nothing_left(t, card);
    }
}

TEST(a_card_with_another_master_is_refused_and_closed) {
    const int32_t errors[] = {EBUSY, EINVAL, EACCES};
    for (uint32_t i = 0; i < 3u; i++) {
        fake_card_t *const card = fake_card_new(t);
        card->fail_request = DRM_IOCTL_SET_MASTER;
        card->fail_errno = errors[i];
        display_config_t config = fake_config();
        display_t *const display = test_alloc(t, sizeof(display_t));
        CHECK_EQ_U64(display_init(display, &config, &fake_ops, card), DISPLAY_STATUS_REFUSED);
        CHECK_EQ_U64(display->refusal, DISPLAY_REFUSAL_NOT_MASTER);
        CHECK_EQ_U64(card->close_count, 1u);
        /* Not being master, it has nothing to drop. */
        CHECK_EQ_I64(fake_find_call(card, DRM_IOCTL_DROP_MASTER, 0u), -1);
        check_nothing_left(t, card);
    }
}

TEST(every_refusal_says_what_to_do_about_it) {
    for (uint32_t i = DISPLAY_REFUSAL_X_SESSION; i <= DISPLAY_REFUSAL_NOT_MASTER; i++) {
        const char *const text = display_device_refusal_text((display_refusal_t)i);
        CHECK_CONTAINS(text, "refused:");
        CHECK(strstr(text, "cannot name") == nullptr);
    }
    CHECK_CONTAINS(display_device_refusal_text(DISPLAY_REFUSAL_X_SESSION), "inside X");
    CHECK_CONTAINS(display_device_refusal_text(DISPLAY_REFUSAL_X_SESSION), "Ctrl+Alt+F3");
    CHECK_CONTAINS(display_device_refusal_text(DISPLAY_REFUSAL_NOT_MASTER), "display server");
}

TEST(a_card_with_no_dumb_buffers_is_an_error_and_gives_master_back) {
    fake_card_t *const card = fake_card_new(t);
    card->no_dumb = 1u;
    display_config_t config = fake_config();
    display_t *const display = test_alloc(t, sizeof(display_t));
    CHECK_EQ_U64(display_init(display, &config, &fake_ops, card), DISPLAY_STATUS_ERROR);
    CHECK_EQ_U64(card->is_master, 0u);
    check_nothing_left(t, card);
}

TEST(an_interrupted_ioctl_is_retried_but_not_for_ever) {
    fake_card_t *const card = fake_card_new(t);
    display_device_t device;
    REQUIRE(display_device_init(&device, &fake_ops, card, "/fake/card", FAKE_CONSOLE_FD, 3u) == DISPLAY_STATUS_OK);
    card->eintr_left = 10u;
    struct drm_get_cap capability = {.capability = DRM_CAP_DUMB_BUFFER, .value = 0};
    CHECK_EQ_I64(display_device_ioctl(&device, DRM_IOCTL_GET_CAP, &capability), 0);
    CHECK_EQ_U64(capability.value, 1u);

    card->eintr_left = 1000000u;
    CHECK_EQ_I64(display_device_ioctl(&device, DRM_IOCTL_GET_CAP, &capability), -EINTR);
    card->eintr_left = 0u;
    display_device_deinit(&device);
}

/* ---- Picking, through the card ------------------------------------------------------------------------------------ */

typedef struct fake_session_t {
    display_device_t device;
    uint8_t _pad0[32];
    arena_t arena;
    display_mode_t mode;
    uint8_t _pad1[16];
} fake_session_t;
static_assert(sizeof(fake_session_t) == 448);

/* An open device and an arena; the caller then runs display_mode_init. */
static bool fake_session_open(test_context_t *t, fake_session_t *session, fake_card_t *card) {
    enum : uint64_t { BYTES = 1u << 20 };
    if (display_device_init(&session->device, &fake_ops, card, "/fake/card", FAKE_CONSOLE_FD, 3u) !=
        DISPLAY_STATUS_OK) {
        return false;
    }
    arena_init(&session->arena, "test_display", test_alloc(t, BYTES), BYTES);
    return true;
}

/* The arena goes first: it poisons its memory under the sanitizer, and that memory belongs to the test arena. */
static void fake_session_close(fake_session_t *session) {
    arena_deinit(&session->arena);
    display_mode_deinit(&session->mode);
    display_device_deinit(&session->device);
}

static void fake_add_connector(fake_card_t *card, fake_connector_t connector) {
    card->connectors[card->connector_count] = connector;
    card->connector_count++;
}

TEST(the_card_is_asked_and_auto_picks_across_connectors) {
    fake_card_t *const card = fake_card_new(t);
    static const struct drm_mode_modeinfo slow[] = {
        {.clock = 148500u, .hdisplay = 1920u, .htotal = 2200u, .vdisplay = 1080u, .vtotal = 1125u},
    };
    card->connectors[0].modes = slow;
    card->connectors[0].mode_count = 1u;
    card->connectors[0].type = DRM_MODE_CONNECTOR_eDP;
    fake_add_connector(card, (fake_connector_t){.mode_count = 0u,
                                                .type = DRM_MODE_CONNECTOR_DisplayPort,
                                                .type_id = 1u,
                                                .connection = 2u,
                                                .possible_crtcs = 3u});
    fake_add_connector(card, (fake_connector_t){.modes = fake_modes_1080p,
                                                .mode_count = 2u,
                                                .type = DRM_MODE_CONNECTOR_HDMIA,
                                                .type_id = 1u,
                                                .connection = 1u,
                                                .possible_crtcs = 3u});
    fake_session_t session;
    REQUIRE(fake_session_open(t, &session, card));
    REQUIRE(display_mode_init(&session.mode, &session.device, &session.arena, &wanted_1080p120) == DISPLAY_STATUS_OK);
    CHECK_STR_EQ(session.mode.connector_name, "HDMI-A-1");
    CHECK_EQ_U64(session.mode.connector_count, 2u); /* the disconnected one is not in the list */
    CHECK_EQ_U64(session.mode.connector_id, FAKE_CONNECTOR_BASE + 2u);
    CHECK_EQ_U64(session.mode.refresh_mhz, 120000u);
    /* The other monitor keeps its picture on CRTC 0: HDMI goes on the free one. */
    CHECK_EQ_U64(session.mode.crtc_id, FAKE_CRTC_BASE + 1u);
    CHECK_EQ_U64(session.mode.saved_crtc.mode_valid, 0u);
    /* Nothing asked here changes anything. */
    CHECK_EQ_I64(fake_find_call(card, DRM_IOCTL_MODE_SETCRTC, 0u), -1);
    fake_session_close(&session);
}

TEST(a_named_connector_that_is_not_connected_stops_the_program) {
    fake_card_t *const card = fake_card_new(t);
    fake_session_t session;
    REQUIRE(fake_session_open(t, &session, card));
    display_mode_wanted_t wanted = wanted_1080p120;
    wanted.connector_name = "DP-7";
    CHECK_EQ_U64(display_mode_init(&session.mode, &session.device, &session.arena, &wanted), DISPLAY_STATUS_ERROR);
    fake_session_close(&session);
}

TEST(no_monitor_stops_the_program) {
    fake_card_t *const card = fake_card_new(t);
    card->connectors[0].connection = 2u;
    fake_session_t session;
    REQUIRE(fake_session_open(t, &session, card));
    CHECK_EQ_U64(display_mode_init(&session.mode, &session.device, &session.arena, &wanted_1080p120),
                 DISPLAY_STATUS_ERROR);
    fake_session_close(&session);
}

TEST(the_crtc_already_driving_the_connector_is_reused_and_saved_with_all_its_connectors) {
    fake_card_t *const card = fake_card_new(t);
    fake_add_connector(card, (fake_connector_t){.modes = fake_modes_1080p,
                                                .mode_count = 2u,
                                                .type = DRM_MODE_CONNECTOR_DisplayPort,
                                                .type_id = 2u,
                                                .connection = 1u,
                                                .current_crtc = FAKE_CRTC_BASE, /* a clone on the same CRTC */
                                                .possible_crtcs = 3u});
    fake_session_t session;
    REQUIRE(fake_session_open(t, &session, card));
    REQUIRE(display_mode_init(&session.mode, &session.device, &session.arena, &wanted_1080p120) == DISPLAY_STATUS_OK);
    CHECK_EQ_U64(session.mode.crtc_id, FAKE_CRTC_BASE);
    CHECK_EQ_U64(session.mode.saved_crtc.fb_id, FAKE_SAVED_FB);
    CHECK_EQ_U64(session.mode.saved_connector_count, 2u);
    CHECK_EQ_U64(session.mode.saved_connectors[0], FAKE_CONNECTOR_BASE);
    CHECK_EQ_U64(session.mode.saved_connectors[1], FAKE_CONNECTOR_BASE + 1u);
    fake_session_close(&session);
}

TEST(no_possible_crtc_stops_the_program) {
    fake_card_t *const card = fake_card_new(t);
    card->connectors[0].current_crtc = 0u;
    card->connectors[0].possible_crtcs = 0u;
    fake_session_t session;
    REQUIRE(fake_session_open(t, &session, card));
    CHECK_EQ_U64(display_mode_init(&session.mode, &session.device, &session.arena, &wanted_1080p120),
                 DISPLAY_STATUS_ERROR);
    fake_session_close(&session);
}

TEST(a_busy_crtc_is_used_when_no_other_is_possible) {
    fake_card_t *const card = fake_card_new(t);
    card->connectors[0].current_crtc = 0u;
    card->crtc_valid[1] = 1u;
    fake_session_t session;
    REQUIRE(fake_session_open(t, &session, card));
    REQUIRE(display_mode_init(&session.mode, &session.device, &session.arena, &wanted_1080p120) == DISPLAY_STATUS_OK);
    CHECK_EQ_U64(session.mode.crtc_id, FAKE_CRTC_BASE);
    fake_session_close(&session);
}

TEST(a_connector_that_grows_between_the_two_calls_is_asked_again) {
    fake_card_t *const card = fake_card_new(t);
    /* Three tries see more modes than they asked for; the fourth does not. */
    static struct drm_mode_modeinfo grown[4];
    grown[0] = mode_at(1920u, 1080u, 60u);
    grown[1] = mode_at(1920u, 1080u, 120u);
    grown[2] = mode_at(1280u, 720u, 60u);
    grown[3] = mode_at(1024u, 768u, 60u);
    card->connectors[0].modes = grown;
    card->connectors[0].mode_count = 3u;
    card->grow_connector_after = 3u;
    fake_session_t session;
    REQUIRE(fake_session_open(t, &session, card));
    REQUIRE(display_mode_init(&session.mode, &session.device, &session.arena, &wanted_1080p120) == DISPLAY_STATUS_OK);
    CHECK_EQ_U64(session.mode.refresh_mhz, 120000u);
    fake_session_close(&session);
}

TEST(a_connector_that_never_settles_is_left_out) {
    fake_card_t *const card = fake_card_new(t);
    static struct drm_mode_modeinfo grown[3];
    grown[0] = mode_at(1920u, 1080u, 60u);
    grown[1] = mode_at(1920u, 1080u, 120u);
    grown[2] = mode_at(1280u, 720u, 60u);
    card->connectors[0].modes = grown;
    card->grow_connector_after = 100u;
    card->connectors[0].mode_count = 2u;
    fake_session_t session;
    REQUIRE(fake_session_open(t, &session, card));
    CHECK_EQ_U64(display_mode_init(&session.mode, &session.device, &session.arena, &wanted_1080p120),
                 DISPLAY_STATUS_ERROR);
    fake_session_close(&session);
}

TEST(a_monitor_with_too_many_modes_does_not_stop_one_that_fits) {
    fake_card_t *const card = fake_card_new(t);
    /* It is left out from its count alone, so its modes are never read. */
    card->connectors[0].modes = nullptr;
    card->connectors[0].mode_count = DISPLAY_MODE_MAX_MODES + 1u;
    fake_add_connector(card, (fake_connector_t){.modes = fake_modes_1080p,
                                                .mode_count = 2u,
                                                .type = DRM_MODE_CONNECTOR_DisplayPort,
                                                .type_id = 1u,
                                                .connection = 1u,
                                                .possible_crtcs = 3u});
    fake_session_t session;
    REQUIRE(fake_session_open(t, &session, card));
    REQUIRE(display_mode_init(&session.mode, &session.device, &session.arena, &wanted_1080p120) == DISPLAY_STATUS_OK);
    CHECK_STR_EQ(session.mode.connector_name, "DP-1");
    fake_session_close(&session);
}

TEST(more_connectors_than_fit_do_not_overrun) {
    fake_card_t *const card = fake_card_new(t);
    for (uint32_t i = 1; i < FAKE_MAX_CONNECTORS; i++) {
        card->connectors[i] = card->connectors[0];
        card->connectors[i].type_id = i + 1u;
        card->connectors[i].current_crtc = FAKE_CRTC_BASE;
    }
    card->connector_count = FAKE_MAX_CONNECTORS;
    fake_session_t session;
    REQUIRE(fake_session_open(t, &session, card));
    REQUIRE(display_mode_init(&session.mode, &session.device, &session.arena, &wanted_1080p120) == DISPLAY_STATUS_OK);
    CHECK_EQ_U64(session.mode.connector_count, DISPLAY_MODE_MAX_CONNECTORS);
    CHECK_EQ_U64(session.mode.saved_connector_count, DISPLAY_MODE_MAX_CONNECTORS);
    fake_session_close(&session);
}

/* ---- Buffers, flipping and giving back ---------------------------------------------------------------------------- */

typedef struct fake_flip_session_t {
    fake_session_t session;
    display_flip_t flip;
    uint8_t _pad0[32];
} fake_flip_session_t;
static_assert(sizeof(fake_flip_session_t) == 704);

static bool fake_flip_open(test_context_t *t, fake_flip_session_t *both, fake_card_t *card) {
    fake_session_t *const session = &both->session;
    return fake_session_open(t, session, card) &&
           display_mode_init(&session->mode, &session->device, &session->arena, &wanted_1080p120) ==
               DISPLAY_STATUS_OK &&
           display_flip_init(&both->flip, &session->device, &session->mode) == DISPLAY_STATUS_OK;
}

static void fake_flip_close(fake_flip_session_t *both) {
    display_flip_deinit(&both->flip);
    fake_session_close(&both->session);
}

TEST(two_buffers_of_the_modes_size_are_made_and_all_removed) {
    fake_card_t *const card = fake_card_new(t);
    card->extra_pitch = 64u;
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    CHECK_EQ_U64(card->live_handles, 2u);
    CHECK_EQ_U64(card->live_fbs, 2u);
    CHECK_EQ_U64(card->live_maps, 2u);
    CHECK_EQ_U64(both.flip.buffers[0].width, 1920u);
    CHECK_EQ_U64(both.flip.buffers[1].height, 1080u);
    CHECK_EQ_U64(both.flip.buffers[0].pitch, 1920u * 4u + 64u);
    CHECK(both.flip.buffers[0].fb_id != both.flip.buffers[1].fb_id);
    CHECK(both.flip.buffers[0].owned_map != both.flip.buffers[1].owned_map);
    /* Nothing was shown. */
    CHECK_EQ_I64(card->kd_mode, KD_TEXT);
    fake_flip_close(&both);
    CHECK_EQ_U64(card->live_handles, 0u);
    CHECK_EQ_U64(card->live_fbs, 0u);
    CHECK_EQ_U64(card->live_maps, 0u);
    CHECK_EQ_U64(card->unmapped_bytes, 2u * (uint64_t)(1920u * 4u + 64u) * 1080u);
    /* Each framebuffer goes before its buffer. */
    CHECK(fake_find_call(card, DRM_IOCTL_MODE_RMFB, 1u) < fake_find_call(card, DRM_IOCTL_MODE_DESTROY_DUMB, 1u));
}

/* Each ioctl that makes a buffer fails in turn, on the first and then the second buffer. */
TEST(a_failure_partway_through_making_buffers_leaves_nothing) {
    const uint64_t requests[] = {DRM_IOCTL_MODE_CREATE_DUMB, DRM_IOCTL_MODE_MAP_DUMB, DRM_IOCTL_MODE_ADDFB2};
    for (uint32_t r = 0; r < 3u; r++) {
        for (uint32_t skip = 0; skip < 2u; skip++) {
            fake_card_t *const card = fake_card_new(t);
            fake_session_t session;
            REQUIRE(fake_session_open(t, &session, card));
            REQUIRE(display_mode_init(&session.mode, &session.device, &session.arena, &wanted_1080p120) ==
                    DISPLAY_STATUS_OK);
            card->fail_request = requests[r];
            card->fail_skip = skip;
            card->fail_errno = ENOMEM;
            display_flip_t flip;
            CHECK_EQ_U64(display_flip_init(&flip, &session.device, &session.mode), DISPLAY_STATUS_ERROR);
            CHECK_EQ_U64(card->live_handles, 0u);
            CHECK_EQ_U64(card->live_fbs, 0u);
            CHECK_EQ_U64(card->live_maps, 0u);
            /* Nothing was taken, so nothing is given back here: master is still the device's to drop. */
            CHECK_EQ_I64(fake_find_call(card, DRM_IOCTL_DROP_MASTER, 0u), -1);
            CHECK_EQ_U64(session.device.is_master, 1u);
            fake_session_close(&session);
            CHECK_EQ_U64(card->is_master, 0u);
        }
    }
}

TEST(a_failed_map_leaves_nothing) {
    fake_card_t *const card = fake_card_new(t);
    fake_session_t session;
    REQUIRE(fake_session_open(t, &session, card));
    REQUIRE(display_mode_init(&session.mode, &session.device, &session.arena, &wanted_1080p120) == DISPLAY_STATUS_OK);
    card->fail_request = UINT64_MAX; /* fake_map_shared looks at this */
    display_flip_t flip;
    CHECK_EQ_U64(display_flip_init(&flip, &session.device, &session.mode), DISPLAY_STATUS_ERROR);
    CHECK_EQ_U64(card->live_handles, 0u);
    CHECK_EQ_U64(card->live_maps, 0u);
    fake_session_close(&session);
}

TEST(a_dumb_buffer_too_small_for_the_mode_is_refused_before_it_is_written) {
    const uint32_t bad_pitches[] = {1920u * 4u - 4u, 1920u * 4u + 2u};
    for (uint32_t i = 0; i < 2u; i++) {
        fake_card_t *const card = fake_card_new(t);
        card->pitch_override = bad_pitches[i];
        fake_session_t session;
        REQUIRE(fake_session_open(t, &session, card));
        REQUIRE(display_mode_init(&session.mode, &session.device, &session.arena, &wanted_1080p120) ==
                DISPLAY_STATUS_OK);
        display_flip_t flip;
        CHECK_EQ_U64(display_flip_init(&flip, &session.device, &session.mode), DISPLAY_STATUS_ERROR);
        CHECK_EQ_U64(card->live_handles, 0u);
        CHECK_EQ_U64(card->live_maps, 0u);
        fake_session_close(&session);
    }
}

TEST(taking_over_goes_to_graphics_before_the_modeset) {
    fake_card_t *const card = fake_card_new(t);
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    REQUIRE(display_flip_take_over(&both.flip));
    const int32_t graphics = fake_find_call(card, KDSETMODE, KD_GRAPHICS);
    const int32_t modeset = fake_find_call(card, DRM_IOCTL_MODE_SETCRTC, both.flip.buffers[0].fb_id);
    CHECK(graphics >= 0);
    CHECK(graphics < modeset);
    CHECK_EQ_U64(card->calls[modeset].count, 1u);
    fake_flip_close(&both);
}

TEST(the_buffers_swap_only_when_the_event_says_so) {
    fake_card_t *const card = fake_card_new(t);
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    REQUIRE(display_flip_take_over(&both.flip));
    display_flip_t *const flip = &both.flip;
    CHECK_EQ_U64(flip->front, 0u);
    CHECK(display_flip_back(flip) == &flip->buffers[1]);

    for (uint32_t i = 0; i < 6u; i++) {
        const uint32_t front_before = flip->front;
        REQUIRE(display_flip_submit(flip));
        CHECK_EQ_U64(flip->flip_pending, 1u);
        CHECK_EQ_U64(flip->front, front_before); /* the old one is on screen until the event */
        /* A second flip with one pending is our bug, and is caught before it reaches the kernel. */
        const uint64_t flips_before = card->flips;
        if (i == 0u) {
            CHECK(!display_flip_submit(flip));
            CHECK_EQ_U64(card->flips, flips_before);
            CHECK_STR_EQ(flip->failed_call, "a flip submitted with one still pending");
            CHECK_EQ_I64(flip->failed_errno, EBUSY);
            flip->failed_call = nullptr;
        }
        CHECK_EQ_U64(display_flip_wait(flip, 0), DISPLAY_FLIP_WAIT_EVENT);
        CHECK_EQ_U64(flip->flip_pending, 0u);
        CHECK_EQ_U64(flip->front, front_before ^ 1u);
    }
    CHECK_EQ_U64(display_flip_wait(flip, 0), DISPLAY_FLIP_WAIT_NOTHING);
    fake_flip_close(&both);
}

static uint64_t event_append(uint8_t *bytes, uint64_t at, uint32_t type, uint32_t crtc_id, uint32_t sequence,
                             uint64_t at_us) {
    struct drm_event_vblank event = {0};
    event.base.type = type;
    event.base.length = sizeof(event);
    event.tv_sec = (uint32_t)(at_us / 1000000u);
    event.tv_usec = (uint32_t)(at_us % 1000000u);
    event.sequence = sequence;
    event.crtc_id = crtc_id;
    memcpy(bytes + at, &event, sizeof(event));
    return at + sizeof(event);
}

TEST(malformed_event_blocks_end_quietly) {
    fake_card_t *const card = fake_card_new(t);
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    display_flip_t *const flip = &both.flip;
    const uint32_t crtc = both.session.mode.crtc_id;
    flip->flip_pending = 1u;

    alignas(8) uint8_t bytes[256] = {0};
    /* Nothing to read, then less than a header. */
    CHECK_EQ_U64(display_flip_take_events(flip, bytes, 0u, 1), 0u);
    CHECK_EQ_U64(display_flip_take_events(flip, bytes, 7u, 1), 0u);

    /* A length of 0 must not spin; one past the end must not be read. */
    struct drm_event header = {.type = DRM_EVENT_FLIP_COMPLETE, .length = 0u};
    memcpy(bytes, &header, sizeof(header));
    CHECK_EQ_U64(display_flip_take_events(flip, bytes, sizeof(bytes), 1), 0u);
    header.length = 4096u;
    memcpy(bytes, &header, sizeof(header));
    CHECK_EQ_U64(display_flip_take_events(flip, bytes, sizeof(bytes), 1), 0u);

    /* A flip event too short to be one, and an event of another kind, are stepped over. */
    header = (struct drm_event){.type = DRM_EVENT_FLIP_COMPLETE, .length = sizeof(header)};
    memcpy(bytes, &header, sizeof(header));
    uint64_t length = event_append(bytes, sizeof(header), DRM_EVENT_VBLANK, crtc, 1u, 100u);
    CHECK_EQ_U64(display_flip_take_events(flip, bytes, length, 1), 0u);
    CHECK_EQ_U64(flip->flip_pending, 1u);

    /* Another CRTC's flip says nothing about ours. */
    length = event_append(bytes, 0u, DRM_EVENT_FLIP_COMPLETE, crtc + 1u, 1u, 100u);
    CHECK_EQ_U64(display_flip_take_events(flip, bytes, length, 1), 0u);
    CHECK_EQ_U64(flip->flip_pending, 1u);
    CHECK_EQ_U64(flip->front, 0u);

    /* A block cut off in the middle: the whole one before it still counts, once. */
    length = event_append(bytes, 0u, DRM_EVENT_FLIP_COMPLETE, crtc, 1u, 100u);
    length = event_append(bytes, length, DRM_EVENT_FLIP_COMPLETE, crtc, 2u, 200u);
    CHECK_EQ_U64(display_flip_take_events(flip, bytes, length - 8u, 1), 1u);
    CHECK_EQ_U64(flip->front, 1u);
    CHECK_EQ_U64(flip->stats.events, 1u);
    fake_flip_close(&both);
}

TEST(an_event_with_no_flip_pending_changes_nothing) {
    fake_card_t *const card = fake_card_new(t);
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    alignas(8) uint8_t bytes[64];
    const uint64_t length = event_append(bytes, 0u, DRM_EVENT_FLIP_COMPLETE, both.session.mode.crtc_id, 1u, 100u);
    CHECK_EQ_U64(display_flip_take_events(&both.flip, bytes, length, 1), 1u);
    CHECK_EQ_U64(both.flip.front, 0u);
    CHECK_EQ_U64(both.flip.stats.events, 0u);
    CHECK_EQ_U64(both.flip.stats.unexpected_events, 1u);
    fake_flip_close(&both);
}

/* Takes one event at a time, as if it had just been flipped for. */
static void feed_event(display_flip_t *flip, uint32_t crtc, uint32_t sequence, uint64_t at_us, int64_t now_ns) {
    alignas(8) uint8_t bytes[64];
    flip->flip_pending = 1u;
    const uint64_t length = event_append(bytes, 0u, DRM_EVENT_FLIP_COMPLETE, crtc, sequence, at_us);
    display_flip_take_events(flip, bytes, length, now_ns);
}

TEST(the_timing_leaves_out_the_warm_up_and_counts_lateness_and_gaps) {
    fake_card_t *const card = fake_card_new(t);
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    display_flip_t *const flip = &both.flip;
    const uint32_t crtc = both.session.mode.crtc_id;
    CHECK_EQ_U64(flip->period_ns, 8333333u);

    uint64_t at_us = 1000000u;
    uint32_t sequence = 100u;
    /* The warm-up, wildly uneven. */
    for (uint32_t i = 0; i < DISPLAY_FLIP_WARMUP_EVENTS; i++) {
        at_us += 50000u;
        sequence += 7u;
        feed_event(flip, crtc, sequence, at_us, 1);
    }
    CHECK_EQ_U64(flip->stats.intervals, 0u);
    CHECK_EQ_U64(flip->stats.late, 0u);
    CHECK_EQ_U64(flip->stats.sequence_gaps, 0u);

    at_us += 8000u;
    sequence++;
    feed_event(flip, crtc, sequence, at_us, 1);
    at_us += 9000u;
    sequence++;
    feed_event(flip, crtc, sequence, at_us, 1);
    /* A missed vblank: twice the period, and a jump in the sequence. */
    at_us += 16000u;
    sequence += 2u;
    feed_event(flip, crtc, sequence, at_us, 1);

    CHECK_EQ_U64(flip->stats.events, DISPLAY_FLIP_WARMUP_EVENTS + 3u);
    CHECK_EQ_U64(flip->stats.intervals, 3u);
    CHECK_EQ_U64(flip->stats.min_ns, 8000000u);
    CHECK_EQ_U64(flip->stats.max_ns, 16000000u);
    CHECK_EQ_U64(flip->stats.sum_ns, 33000000u);
    CHECK_EQ_U64(flip->stats.late, 1u);
    CHECK_EQ_U64(flip->stats.sequence_gaps, 1u);
    fake_flip_close(&both);
}

TEST(without_a_monotonic_clock_our_own_wake_ups_are_timed) {
    fake_card_t *const card = fake_card_new(t);
    card->monotonic = 0u;
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    display_flip_t *const flip = &both.flip;
    const uint32_t crtc = both.session.mode.crtc_id;
    int64_t now_ns = 1000000000;
    for (uint32_t i = 0; i <= DISPLAY_FLIP_WARMUP_EVENTS; i++) {
        now_ns += 5000000;
        feed_event(flip, crtc, i + 1u, 999u, now_ns); /* the kernel's time stays still, and is ignored */
    }
    CHECK_EQ_U64(flip->stats.intervals, 1u);
    CHECK_EQ_U64(flip->stats.sum_ns, 5000000u);
    fake_flip_close(&both);
}

TEST(a_sequence_of_zero_says_nothing_and_a_wrap_is_not_a_gap) {
    fake_card_t *const card = fake_card_new(t);
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    display_flip_t *const flip = &both.flip;
    const uint32_t crtc = both.session.mode.crtc_id;
    uint64_t at_us = 1000000u;
    for (uint32_t i = 0; i < DISPLAY_FLIP_WARMUP_EVENTS + 3u; i++) {
        at_us += 8333u;
        feed_event(flip, crtc, 0u, at_us, 1);
    }
    CHECK_EQ_U64(flip->stats.sequence_gaps, 0u);
    /* A driver that starts counting at last says nothing about what came before. */
    feed_event(flip, crtc, UINT32_MAX - 1u, at_us + 8333u, 1);
    CHECK_EQ_U64(flip->stats.sequence_gaps, 0u);
    /* Across the wrap, one vblank apart and then two. */
    feed_event(flip, crtc, UINT32_MAX, at_us + 16666u, 1);
    CHECK_EQ_U64(flip->stats.sequence_gaps, 0u);
    feed_event(flip, crtc, 1u, at_us + 33332u, 1);
    CHECK_EQ_U64(flip->stats.sequence_gaps, 1u);
    fake_flip_close(&both);
}

TEST(giving_back_puts_the_crtc_back_then_drops_master_then_text_once) {
    fake_card_t *const card = fake_card_new(t);
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    REQUIRE(display_flip_take_over(&both.flip));
    CHECK(display_flip_restore(&both.flip));

    const int32_t crtc_back = fake_find_call(card, DRM_IOCTL_MODE_SETCRTC, FAKE_SAVED_FB);
    const int32_t dropped = fake_find_call(card, DRM_IOCTL_DROP_MASTER, 0u);
    const int32_t text = fake_find_call(card, KDSETMODE, KD_TEXT);
    CHECK(crtc_back >= 0);
    CHECK(crtc_back < dropped);
    CHECK(dropped < text);
    CHECK_EQ_U64(card->calls[crtc_back].count, 1u); /* the saved connector */
    CHECK_EQ_I64(card->kd_mode, KD_TEXT);
    CHECK_EQ_U64(card->is_master, 0u);

    /* A second restore does nothing, as a crash during the first would need. */
    const uint32_t calls = card->call_count;
    CHECK(display_flip_restore(&both.flip));
    CHECK_EQ_U64(card->call_count, calls);

    /* The framebuffers go only after that. */
    fake_flip_close(&both);
    CHECK(text < fake_find_call(card, DRM_IOCTL_MODE_RMFB, 1u));
    CHECK_EQ_U64(card->close_count, 1u);
}

TEST(a_crtc_that_was_off_goes_back_to_off) {
    fake_card_t *const card = fake_card_new(t);
    card->crtc_valid[0] = 0u;
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    REQUIRE(display_flip_take_over(&both.flip));
    CHECK(display_flip_restore(&both.flip));
    const int32_t off = fake_find_call(card, DRM_IOCTL_MODE_SETCRTC, 0u);
    REQUIRE(off >= 0);
    CHECK_EQ_U64(card->calls[off].count, 0u);
    fake_flip_close(&both);
}

TEST(a_restore_that_fails_turns_the_crtc_off_and_still_gives_back_the_rest) {
    fake_card_t *const card = fake_card_new(t);
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    REQUIRE(display_flip_take_over(&both.flip));
    /* The modeset is done, so the next SETCRTC is the restore, and only that one fails. */
    card->fail_request = DRM_IOCTL_MODE_SETCRTC;
    card->fail_errno = EINVAL;
    CHECK(!display_flip_restore(&both.flip));
    CHECK(!display_flip_restore(&both.flip)); /* the same answer again */
    CHECK_EQ_U64(card->is_master, 0u);
    CHECK_EQ_I64(card->kd_mode, KD_TEXT);
    fake_flip_close(&both);
    CHECK_EQ_U64(card->live_fbs, 0u);
}

TEST(a_modeset_that_fails_is_still_undone) {
    fake_card_t *const card = fake_card_new(t);
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    card->fail_request = DRM_IOCTL_MODE_SETCRTC;
    card->fail_errno = EINVAL;
    card->fail_skip = 0u;
    CHECK(!display_flip_take_over(&both.flip));
    CHECK_STR_EQ(both.flip.failed_call, "SETCRTC");
    CHECK_EQ_I64(both.flip.failed_errno, EINVAL);
    CHECK_EQ_U64(both.flip.modeset_done, 1u);
    card->fail_request = 0u;
    CHECK(display_flip_restore(&both.flip));
    CHECK(fake_find_call(card, DRM_IOCTL_MODE_SETCRTC, FAKE_SAVED_FB) >= 0);
    CHECK_EQ_I64(card->kd_mode, KD_TEXT);
    fake_flip_close(&both);
}

TEST(deinit_gives_back_what_nobody_else_did) {
    fake_card_t *const card = fake_card_new(t);
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    REQUIRE(display_flip_take_over(&both.flip));
    fake_flip_close(&both);
    CHECK_EQ_I64(card->kd_mode, KD_TEXT);
    CHECK_EQ_U64(card->is_master, 0u);
    CHECK(fake_find_call(card, DRM_IOCTL_MODE_SETCRTC, FAKE_SAVED_FB) >= 0);
    CHECK_EQ_U64(card->live_fbs, 0u);
}

TEST(a_console_that_will_not_go_graphics_is_not_touched_further) {
    fake_card_t *const card = fake_card_new(t);
    fake_flip_session_t both;
    REQUIRE(fake_flip_open(t, &both, card));
    card->fail_request = KDSETMODE;
    card->fail_errno = EPERM;
    CHECK(!display_flip_take_over(&both.flip));
    CHECK_STR_EQ(both.flip.failed_call, "KDSETMODE(KD_GRAPHICS)");
    CHECK_EQ_U64(both.flip.modeset_done, 0u);
    CHECK_EQ_I64(fake_find_call(card, DRM_IOCTL_MODE_SETCRTC, both.flip.buffers[0].fb_id), -1);
    fake_flip_close(&both);
}

/* ---- The test pattern's pixels ------------------------------------------------------------------------------------ */

enum : uint32_t {
    PATTERN_TEST_WIDTH = 320u,
    PATTERN_TEST_HEIGHT = 200u,
    PATTERN_TEST_PITCH = PATTERN_TEST_WIDTH * 4u + 32u,
    PATTERN_TEST_GAP_FILL = 0xa5a5a5a5u,
    PATTERN_TEST_SQUARE = 0x00ff8000u,
};

static display_buffer_t pattern_buffer(test_context_t *t) {
    display_buffer_t buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.width = PATTERN_TEST_WIDTH;
    buffer.height = PATTERN_TEST_HEIGHT;
    buffer.pitch = PATTERN_TEST_PITCH;
    buffer.map_bytes = (uint64_t)PATTERN_TEST_PITCH * PATTERN_TEST_HEIGHT;
    buffer.owned_map = test_alloc(t, buffer.map_bytes);
    memset(buffer.owned_map, 0xa5, buffer.map_bytes);
    return buffer;
}

static uint32_t pattern_read(const display_buffer_t *buffer, uint32_t x, uint32_t y) {
    uint32_t value;
    memcpy(&value, buffer->owned_map + (uint64_t)y * buffer->pitch + (uint64_t)x * 4u, sizeof(value));
    return value;
}

TEST(the_still_picture_has_a_border_bars_and_a_ramp) {
    const uint32_t w = PATTERN_TEST_WIDTH;
    const uint32_t h = PATTERN_TEST_HEIGHT;
    CHECK_EQ_U64(display_flip_pattern_pixel(0u, 0u, w, h), 0x00ffffffu);
    CHECK_EQ_U64(display_flip_pattern_pixel(w - 1u, h - 1u, w, h), 0x00ffffffu);
    CHECK_EQ_U64(display_flip_pattern_pixel(1u, 50u, w, h), 0x00ffffffu);
    CHECK_EQ_U64(display_flip_pattern_pixel(w - 2u, 50u, w, h), 0x00ffffffu);
    /* Eight bars, each an eighth wide. */
    CHECK_EQ_U64(display_flip_pattern_pixel(10u, 50u, w, h), 0x00c0c0c0u);
    CHECK_EQ_U64(display_flip_pattern_pixel(w / 8u, 50u, w, h), 0x00c0c000u);
    CHECK_EQ_U64(display_flip_pattern_pixel(w * 5u / 8u, 50u, w, h), 0x00c00000u);
    CHECK_EQ_U64(display_flip_pattern_pixel(w - 3u, 50u, w, h), 0x00000000u);
    /* A grey ramp that climbs left to right in the bottom quarter. */
    const uint32_t y = h * 3u / 4u + 1u;
    CHECK_EQ_U64(display_flip_pattern_pixel(w / 2u, y, w, h), 0x00808080u);
    CHECK(display_flip_pattern_pixel(w / 4u, y, w, h) < display_flip_pattern_pixel(w / 2u, y, w, h));
    CHECK(display_flip_pattern_pixel(w - 3u, y, w, h) <= 0x00ffffffu);
}

TEST(fill_writes_every_pixel_and_nothing_in_the_gap_past_each_row) {
    display_buffer_t buffer = pattern_buffer(t);
    display_flip_pattern_fill(&buffer);
    uint64_t wrong_pixels = 0;
    uint64_t touched_gap = 0;
    for (uint32_t y = 0; y < buffer.height; y++) {
        for (uint32_t x = 0; x < buffer.width; x++) {
            wrong_pixels +=
                pattern_read(&buffer, x, y) != display_flip_pattern_pixel(x, y, buffer.width, buffer.height) ? 1u : 0u;
        }
        /* The bytes between the end of the row and the pitch. */
        for (uint32_t x = buffer.width; x < buffer.pitch / 4u; x++) {
            touched_gap += pattern_read(&buffer, x, y) != PATTERN_TEST_GAP_FILL ? 1u : 0u;
        }
    }
    CHECK_EQ_U64(wrong_pixels, 0u);
    CHECK_EQ_U64(touched_gap, 0u);
}

/* Compares the whole picture with the still one, apart from one square. */
static uint64_t pattern_count_differences(const display_buffer_t *buffer, uint32_t square_x, uint32_t square_y,
                                          uint32_t side) {
    uint64_t wrong = 0;
    for (uint32_t y = 0; y < buffer->height; y++) {
        for (uint32_t x = 0; x < buffer->width; x++) {
            const bool in_square = x >= square_x && x < square_x + side && y >= square_y && y < square_y + side;
            const uint32_t expected =
                in_square ? PATTERN_TEST_SQUARE : display_flip_pattern_pixel(x, y, buffer->width, buffer->height);
            wrong += pattern_read(buffer, x, y) != expected ? 1u : 0u;
        }
    }
    return wrong;
}

TEST(the_square_moves_and_repaints_what_it_left) {
    display_buffer_t buffer = pattern_buffer(t);
    display_flip_pattern_fill(&buffer);
    const uint32_t side = 50u; /* a quarter of the shorter side, 200 */
    display_flip_pattern_frame(&buffer, 0u);
    CHECK_EQ_U64(buffer.square_x, 0u);
    CHECK_EQ_U64(buffer.square_y, (PATTERN_TEST_HEIGHT - side) / 2u);
    CHECK_EQ_U64(pattern_count_differences(&buffer, buffer.square_x, buffer.square_y, side), 0u);

    display_flip_pattern_frame(&buffer, 10u);
    CHECK_EQ_U64(buffer.square_x, 40u);
    CHECK_EQ_U64(pattern_count_differences(&buffer, buffer.square_x, buffer.square_y, side), 0u);
}

TEST(the_square_turns_at_each_edge_and_stays_on_screen) {
    display_buffer_t buffer = pattern_buffer(t);
    display_flip_pattern_fill(&buffer);
    const uint32_t side = 50u;
    const uint64_t travel = PATTERN_TEST_WIDTH - side; /* 270 pixels, at 4 per frame */
    uint32_t furthest = 0;
    for (uint64_t frame = 0; frame < travel; frame++) {
        display_flip_pattern_frame(&buffer, frame);
        CHECK(buffer.square_x + side <= PATTERN_TEST_WIDTH);
        furthest = buffer.square_x > furthest ? buffer.square_x : furthest;
    }
    CHECK(furthest >= travel - 3u);
    CHECK_EQ_U64(pattern_count_differences(&buffer, buffer.square_x, buffer.square_y, side), 0u);
    /* 272 pixels along a 270-pixel trip has turned back by two. */
    display_flip_pattern_frame(&buffer, 68u);
    CHECK_EQ_U64(buffer.square_x, 268u);
    /* A trip there and back ends where it began. */
    display_flip_pattern_frame(&buffer, 135u);
    CHECK_EQ_U64(buffer.square_x, 0u);
    CHECK_EQ_U64(pattern_count_differences(&buffer, buffer.square_x, buffer.square_y, side), 0u);
}

TEST(each_buffer_remembers_its_own_square) {
    display_buffer_t first = pattern_buffer(t);
    display_buffer_t second = pattern_buffer(t);
    display_flip_pattern_fill(&first);
    display_flip_pattern_fill(&second);
    /* As in the loop: odd frames go to one buffer, even frames to the other. */
    for (uint64_t frame = 0; frame < 40u; frame++) {
        display_flip_pattern_frame(frame % 2u == 0u ? &first : &second, frame);
    }
    CHECK_EQ_U64(pattern_count_differences(&first, first.square_x, first.square_y, 50u), 0u);
    CHECK_EQ_U64(pattern_count_differences(&second, second.square_x, second.square_y, 50u), 0u);
    CHECK(first.square_x != second.square_x);
}

TEST(a_mode_too_small_for_a_square_is_not_written_past) {
    display_buffer_t buffer = pattern_buffer(t);
    buffer.width = 3u;
    buffer.height = 3u;
    display_flip_pattern_fill(&buffer);
    display_flip_pattern_frame(&buffer, 5u);
    CHECK_EQ_U64(buffer.has_square, 0u);
    CHECK_EQ_U64(pattern_read(&buffer, 1u, 1u), 0x00ffffffu);

    /* The smallest that gets a square: one pixel, with three to travel. */
    buffer = pattern_buffer(t);
    buffer.width = 4u;
    buffer.height = 4u;
    display_flip_pattern_fill(&buffer);
    display_flip_pattern_frame(&buffer, 1u);
    CHECK_EQ_U64(buffer.has_square, 1u);
    CHECK_EQ_U64(buffer.square_x, 2u); /* 4 pixels along a 3-pixel trip has turned back by one */
    CHECK_EQ_U64(buffer.square_y, 1u);
}

/* ---- The whole backend, on the fake card -------------------------------------------------------------------------- */

static void check_given_back(test_context_t *t, const fake_card_t *card) {
    CHECK(fake_find_call(card, DRM_IOCTL_MODE_SETCRTC, FAKE_SAVED_FB) >= 0);
    CHECK(fake_find_call(card, DRM_IOCTL_MODE_SETCRTC, FAKE_SAVED_FB) <
          fake_find_call(card, DRM_IOCTL_DROP_MASTER, 0u));
    CHECK(fake_find_call(card, DRM_IOCTL_DROP_MASTER, 0u) < fake_find_call(card, KDSETMODE, KD_TEXT));
    CHECK_EQ_I64(card->kd_mode, KD_TEXT);
    CHECK_EQ_U64(card->is_master, 0u);
}

static display_status_t run_pattern(test_context_t *t, fake_card_t *card, int64_t duration_ns, display_t **out) {
    display_config_t config = fake_config();
    display_t *const display = test_alloc(t, sizeof(display_t));
    *out = display;
    if (display_init(display, &config, &fake_ops, card) != DISPLAY_STATUS_OK) {
        test_check(t, false, __FILE__, __LINE__, "display_init on the fake card");
        return DISPLAY_STATUS_ERROR;
    }
    return display_test_pattern_run(display, time_now_ns(), duration_ns);
}

TEST(the_test_pattern_runs_flips_gives_back_and_leaves_nothing) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    display_t *display = nullptr;
    CHECK_EQ_U64(run_pattern(t, card, 20000000, &display), DISPLAY_STATUS_OK);
    CHECK(card->flips > DISPLAY_FLIP_WARMUP_EVENTS);
    CHECK_EQ_U64(display->flip.stats.flips_submitted, card->flips);
    CHECK_EQ_U64(display->flip.stats.events, card->flips);
    CHECK_EQ_U64(display->flip.stats.unexpected_events, 0u);
    CHECK_EQ_U64(display->flip.flip_pending, 0u);
    /* The fake's flips are all exactly one period apart. */
    CHECK_EQ_U64(display->flip.stats.min_ns, 8333000u);
    CHECK_EQ_U64(display->flip.stats.max_ns, 8333000u);
    CHECK_EQ_U64(display->flip.stats.late, 0u);
    check_given_back(t, card);
    display_deinit(display);
    check_nothing_left(t, card);
    /* A second deinit, as main's cleanup might, does nothing. */
    display_deinit(display);
    CHECK_EQ_U64(card->close_count, 1u);
}

TEST(the_test_pattern_stops_at_its_deadline_counted_from_its_start) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    display_config_t config = fake_config();
    display_t *const display = test_alloc(t, sizeof(display_t));
    REQUIRE(display_init(display, &config, &fake_ops, card) == DISPLAY_STATUS_OK);
    /* Already out of time when the loop starts, as after a very slow modeset: it shows nothing, and gives back. */
    CHECK_EQ_U64(display_test_pattern_run(display, time_now_ns() - 1000000000, 1000000), DISPLAY_STATUS_OK);
    CHECK_EQ_U64(card->flips, 0u);
    check_given_back(t, card);
    display_deinit(display);
    check_nothing_left(t, card);
}

TEST(a_stop_signal_ends_the_test_pattern_cleanly_and_is_handed_back) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    card->sigterm_after_flips = 20u;
    display_t *display = nullptr;
    /* A long deadline, so that only the signal can end it. */
    CHECK_EQ_U64(run_pattern(t, card, 60000000000, &display), DISPLAY_STATUS_OK);
    CHECK_EQ_U64(card->flips, 20u);
    check_given_back(t, card);
    /* The handler it installed is gone again. */
    struct sigaction now;
    sigaction(SIGTERM, nullptr, &now);
    CHECK(now.sa_handler == SIG_DFL);
    display_deinit(display);
    check_nothing_left(t, card);
}

TEST(a_failed_flip_ends_the_test_pattern_and_gives_back) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    card->fail_request = DRM_IOCTL_MODE_PAGE_FLIP;
    card->fail_errno = EINVAL;
    card->fail_skip = 15u;
    display_t *display = nullptr;
    CHECK_EQ_U64(run_pattern(t, card, 60000000000, &display), DISPLAY_STATUS_ERROR);
    CHECK_STR_EQ(display->flip.failed_call, "PAGE_FLIP");
    CHECK_EQ_I64(display->flip.failed_errno, EINVAL);
    CHECK_EQ_U64(card->flips, 15u);
    check_given_back(t, card);
    display_deinit(display);
    check_nothing_left(t, card);
}

TEST(flips_that_stop_arriving_end_the_test_pattern_and_give_back) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    card->no_flip_events = 1u;
    display_t *display = nullptr;
    CHECK_EQ_U64(run_pattern(t, card, 60000000000, &display), DISPLAY_STATUS_ERROR);
    CHECK_STR_EQ(display->flip.failed_call, "no flip event within a second");
    CHECK_EQ_U64(card->flips, 1u);
    check_given_back(t, card);
    display_deinit(display);
    check_nothing_left(t, card);
}

TEST(a_switched_console_ends_the_test_pattern_and_gives_back) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    card->vt_change_after_flips = 50u;
    display_t *display = nullptr;
    CHECK_EQ_U64(run_pattern(t, card, 60000000000, &display), DISPLAY_STATUS_ERROR);
    CHECK_STR_EQ(display->flip.failed_call, "the visible virtual console changed during the run");
    CHECK(card->flips <= 120u);
    check_given_back(t, card);
    display_deinit(display);
    check_nothing_left(t, card);
}

TEST(display_deinit_is_safe_on_a_display_that_never_ran) {
    display_t *const display = test_alloc(t, sizeof(display_t));
    display_deinit(display);
    heap_counters_t counters;
    heap_counters_read(&counters);
    CHECK_EQ_U64(counters.live_bytes, 0u);
}

TEST(a_failure_in_each_part_of_init_leaves_nothing) {
    const uint64_t requests[] = {DRM_IOCTL_VERSION,           DRM_IOCTL_GET_CAP,      DRM_IOCTL_MODE_GETRESOURCES,
                                 DRM_IOCTL_MODE_GETCONNECTOR, DRM_IOCTL_MODE_GETCRTC, DRM_IOCTL_MODE_CREATE_DUMB};
    for (uint32_t r = 0; r < 6u; r++) {
        fake_card_t *const card = fake_card_new(t);
        card->fail_request = requests[r];
        card->fail_errno = EIO;
        display_config_t config = fake_config();
        display_t *const display = test_alloc(t, sizeof(display_t));
        CHECK_EQ_U64(display_init(display, &config, &fake_ops, card), DISPLAY_STATUS_ERROR);
        CHECK_EQ_U64(card->is_master, 0u);
        check_nothing_left(t, card);
        /* Nothing was ever shown. */
        CHECK_EQ_I64(fake_find_call(card, DRM_IOCTL_MODE_SETCRTC, 1u), -1);
        CHECK_EQ_I64(card->kd_mode, KD_TEXT);
    }
}

TEST(the_machine_yaml_is_what_the_config_comes_from) {
    display_config_t config;
    display_config_from_machine(&config);
    CHECK_STR_EQ(config.card_path, MC_DISPLAY_DEVICE);
    CHECK_STR_EQ(config.connector_name, MC_DISPLAY_CONNECTOR);
    CHECK_EQ_U64(config.width, MC_DISPLAY_WIDTH);
    CHECK_EQ_U64(config.height, MC_DISPLAY_HEIGHT);
    CHECK_EQ_U64(config.refresh_hz, MC_DISPLAY_REFRESH_HZ);
}

/* ---- The real layer, on things that are not a card or a console ---------------------------------------------------- */

TEST(the_real_ops_answer_with_negative_errno) {
    const display_ops_t *const ops = display_device_real_ops();
    CHECK_EQ_I64(ops->open_card(nullptr, "/nonexistent/mc_c_no_such_card"), -ENOENT);
    CHECK_EQ_I64(ops->close_fd(nullptr, -1), -EBADF);

    int32_t fds[2];
    REQUIRE(pipe(fds) == 0);
    /* An ioctl on a pipe, which is nobody's display. */
    uint32_t unused = 0;
    CHECK_EQ_I64(ops->ioctl_call(nullptr, fds[0], DRM_IOCTL_MODE_GETCRTC, &unused), -ENOTTY);

    /* Not a character device. */
    uint32_t major_number = 0;
    uint32_t minor_number = 0;
    CHECK_EQ_I64(ops->char_device(nullptr, fds[0], &major_number, &minor_number), -ENOTTY);
    CHECK_EQ_I64(ops->char_device(nullptr, -1, &major_number, &minor_number), -EBADF);

    /* Waiting and reading. */
    CHECK_EQ_I64(ops->wait_readable(nullptr, fds[0], 0), 0);
    static const uint8_t sent[3] = {1u, 2u, 3u};
    REQUIRE(write(fds[1], sent, sizeof(sent)) == (int64_t)sizeof(sent));
    CHECK_EQ_I64(ops->wait_readable(nullptr, fds[0], 1000), 1);
    uint8_t received[8] = {0};
    CHECK_EQ_I64(ops->read_fd(nullptr, fds[0], received, sizeof(received)), 3);
    CHECK_MEM_EQ(received, sent, sizeof(sent));
    CHECK_EQ_I64(ops->read_fd(nullptr, -1, received, sizeof(received)), -EBADF);
    CHECK_EQ_I64(ops->wait_readable(nullptr, fds[0], 0), 0);
    close(fds[0]);
    close(fds[1]);
}

TEST(the_real_map_maps_and_unmaps) {
    /* An unnamed file, standing in for a card's fd. */
    char path[] = "/tmp/mc_c_display_map_XXXXXX";
    const int32_t fd = mkstemp(path);
    REQUIRE(fd >= 0);
    unlink(path);
    REQUIRE(ftruncate(fd, 4096) == 0);
    const display_ops_t *const ops = display_device_real_ops();
    uint8_t *memory = nullptr;
    REQUIRE(ops->map_shared(nullptr, fd, 4096u, 0u, &memory) == 0);
    memory[100] = 42u;
    uint8_t read_back = 0;
    CHECK_EQ_I64(pread(fd, &read_back, 1u, 100), 1);
    CHECK_EQ_U64(read_back, 42u);
    ops->unmap(nullptr, memory, 4096u);
    uint8_t *unused = nullptr;
    CHECK(ops->map_shared(nullptr, -1, 4096u, 0u, &unused) < 0);
    close(fd);
}

TEST(the_real_check_refuses_dev_null_without_asking_it_anything) {
    const int32_t fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    REQUIRE(unsetenv("DISPLAY") == 0);
    REQUIRE(unsetenv("WAYLAND_DISPLAY") == 0);
    uint32_t minor_number = 0;
    CHECK_EQ_U64(display_device_check_console(display_device_real_ops(), nullptr, fd, &minor_number),
                 DISPLAY_REFUSAL_NOT_A_VT);
    REQUIRE(setenv("DISPLAY", ":0", 1) == 0);
    CHECK_EQ_U64(display_device_check_console(display_device_real_ops(), nullptr, fd, &minor_number),
                 DISPLAY_REFUSAL_X_SESSION);
    REQUIRE(unsetenv("DISPLAY") == 0);
    close(fd);
}

/* ---- The real program: it may only ever get as far as refusing --------------------------------------------------- */

static char pattern_flag[] = "--test-pattern";
static char card_flag[] = "--card";
static char no_card[] = "/nonexistent/mc_c_no_such_card";

/* The harness gives the child /dev/null for stdin, and the card does not exist, so this cannot reach anything real. */
static void run_game_pattern(test_context_t *t, test_process_t *out) {
    REQUIRE(t->arg_count == 1);
    char *const argv[] = {t->borrowed_args[0], pattern_flag, card_flag, no_card, nullptr};
    test_process_run(t, argv, 30000u, out);
    REQUIRE(out->started != 0u);
}

TEST(the_program_inside_x_fails_fast_with_a_clear_message) {
    REQUIRE(setenv("DISPLAY", ":0", 1) == 0);
    REQUIRE(unsetenv("WAYLAND_DISPLAY") == 0);
    test_process_t result;
    run_game_pattern(t, &result);
    REQUIRE(unsetenv("DISPLAY") == 0);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_DISPLAY_REFUSED);
    CHECK_CONTAINS(result.output, "running inside X");
    CHECK_CONTAINS(result.output, "The card was not opened");
    CHECK_CONTAINS(result.output, "Ctrl+Alt+F3");
    /* Refused before any memory, and with nothing to clean up. */
    CHECK_CONTAINS(result.output, "heap at exit: 0 reserved, 0 released, 0 bytes live");
}

TEST(the_program_in_wayland_fails_fast_with_a_clear_message) {
    REQUIRE(unsetenv("DISPLAY") == 0);
    REQUIRE(setenv("WAYLAND_DISPLAY", "wayland-0", 1) == 0);
    test_process_t result;
    run_game_pattern(t, &result);
    REQUIRE(unsetenv("WAYLAND_DISPLAY") == 0);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_DISPLAY_REFUSED);
    CHECK_CONTAINS(result.output, "Wayland");
}

TEST(the_program_with_stdin_not_a_console_fails_fast) {
    REQUIRE(unsetenv("DISPLAY") == 0);
    REQUIRE(unsetenv("WAYLAND_DISPLAY") == 0);
    test_process_t result;
    run_game_pattern(t, &result);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_DISPLAY_REFUSED);
    CHECK_CONTAINS(result.output, "stdin is not a Linux virtual console");
    CHECK_CONTAINS(result.output, "The card was not opened");
}

TEST(card_and_connector_need_the_test_pattern) {
    REQUIRE(t->arg_count == 1);
    char *const argv[] = {t->borrowed_args[0], card_flag, no_card, nullptr};
    test_process_t result;
    test_process_run(t, argv, 30000u, &result);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(result.output, "only mean something with --test-pattern");
}

TEST_MAIN()
