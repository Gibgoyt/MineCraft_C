/* The fake kernel behind display_ops_t, for ctest run_display_mode. See ../README.md. It answers as the DRM UAPI says the real
 * one does, down to the kernel copying modes only if all of them fit, and it records what was asked, so that a test can
 * check the order of the calls. Included once, by ../index.c. */
#ifndef MC_TESTS_RUN_DISPLAY_MODE_FIXTURES_FAKE_CARD_H
#define MC_TESTS_RUN_DISPLAY_MODE_FIXTURES_FAKE_CARD_H

#include "harness/index.h"

#include "display/index.h"
#include "heap/index.h"

#include <drm/drm_fourcc.h>
#include <errno.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>

enum : uint32_t {
    FAKE_MAX_CONNECTORS = 20u, /* more than DISPLAY_MODE_MAX_CONNECTORS, to test the limit */
    FAKE_MAX_CRTCS = 4u,
    FAKE_MAX_CALLS = 1024u,
    FAKE_EVENT_BYTES = 4096u,
    FAKE_CRTC_BASE = 50u,
    FAKE_ENCODER_BASE = 100u,
    FAKE_CONNECTOR_BASE = 200u,
    FAKE_SAVED_FB = 77u,
};
enum : int32_t {
    FAKE_CONSOLE_FD = 5,
    FAKE_CARD_FD = 7,
};

typedef struct fake_connector_t {
    const struct drm_mode_modeinfo *modes;
    uint32_t mode_count;
    uint32_t type;
    uint32_t type_id;
    uint32_t connection;
    uint32_t current_crtc;   /* 0 if its encoder drives nothing */
    uint32_t possible_crtcs; /* bit 0 is the first CRTC */
} fake_connector_t;
static_assert(sizeof(fake_connector_t) == 32);

typedef struct fake_call_t {
    uint64_t request;
    uint32_t value; /* SETCRTC: the framebuffer. KDSETMODE: the mode. */
    uint32_t count; /* SETCRTC: the connectors */
} fake_call_t;
static_assert(sizeof(fake_call_t) == 16);

typedef struct fake_card_t {
    test_context_t *borrowed_test;
    const char *env_display;
    const char *env_wayland;
    uint64_t fail_request; /* this request fails, after fail_skip successes, with fail_errno */
    uint64_t flips;
    uint64_t flip_period_us;
    uint64_t clock_us;
    uint64_t unmapped_bytes;
    fake_connector_t connectors[FAKE_MAX_CONNECTORS];
    fake_call_t calls[FAKE_MAX_CALLS];
    uint32_t crtc_valid[FAKE_MAX_CRTCS];
    uint8_t events[FAKE_EVENT_BYTES];
    uint32_t event_bytes;
    uint32_t connector_count;
    uint32_t crtc_count;
    uint32_t call_count;
    uint32_t fail_skip;
    int32_t fail_errno;
    int32_t open_errno;
    int32_t kd_mode;
    uint32_t console_major;
    uint32_t console_minor;
    uint32_t active_vt;
    uint32_t open_count;
    uint32_t close_count;
    uint32_t next_handle;
    uint32_t next_fb;
    uint32_t live_handles;
    uint32_t live_fbs;
    uint32_t live_maps;
    uint32_t pitch_override;
    uint32_t extra_pitch;
    uint32_t eintr_left;
    uint32_t grow_connector_after; /* one more mode appears after the count call, this many times */
    uint32_t sigterm_after_flips;
    uint32_t vt_change_after_flips;
    uint8_t no_dumb;
    uint8_t monotonic;
    uint8_t no_flip_events;
    uint8_t is_master;
    uint8_t has_saved_fb;
    uint8_t _pad0[3];
} fake_card_t;
static_assert(sizeof(fake_card_t) == 64 + 640 + 16384 + 16 + 4096 + 104);

static void fake_record(fake_card_t *card, uint64_t request, uint32_t value, uint32_t count) {
    if (card->call_count < FAKE_MAX_CALLS) {
        card->calls[card->call_count] = (fake_call_t){.request = request, .value = value, .count = count};
        card->call_count++;
    }
}

static int32_t fake_open_card(void *context, const char *path) {
    fake_card_t *const card = context;
    (void)path;
    card->open_count++;
    return card->open_errno != 0 ? -card->open_errno : FAKE_CARD_FD;
}

static int32_t fake_close_fd(void *context, int32_t fd) {
    fake_card_t *const card = context;
    card->close_count += fd == FAKE_CARD_FD ? 1u : 0u;
    return 0;
}

static int32_t fake_map_shared(void *context, int32_t fd, uint64_t bytes, uint64_t offset, uint8_t **out) {
    fake_card_t *const card = context;
    (void)offset;
    if (fd != FAKE_CARD_FD) {
        return -EBADF;
    }
    if (card->fail_request == UINT64_MAX) {
        return -ENOMEM;
    }
    *out = test_alloc(card->borrowed_test, bytes);
    card->live_maps++;
    return 0;
}

static void fake_unmap(void *context, uint8_t *memory, uint64_t bytes) {
    fake_card_t *const card = context;
    (void)memory;
    card->live_maps--;
    card->unmapped_bytes += bytes;
}

static int32_t fake_wait_readable(void *context, int32_t fd, int32_t timeout_ms) {
    fake_card_t *const card = context;
    (void)timeout_ms;
    if (fd != FAKE_CARD_FD) {
        return -EBADF;
    }
    return card->event_bytes != 0u ? 1 : 0;
}

static int64_t fake_read_fd(void *context, int32_t fd, uint8_t *out, uint64_t bytes) {
    fake_card_t *const card = context;
    if (fd != FAKE_CARD_FD) {
        return -EBADF;
    }
    const uint64_t count = card->event_bytes < bytes ? card->event_bytes : bytes;
    memcpy(out, card->events, count);
    card->event_bytes = 0u;
    return (int64_t)count;
}

static int32_t fake_char_device(void *context, int32_t fd, uint32_t *out_major, uint32_t *out_minor) {
    fake_card_t *const card = context;
    if (fd != FAKE_CONSOLE_FD) {
        return -EBADF;
    }
    if (card->console_major == 0u) {
        return -ENOTTY;
    }
    *out_major = card->console_major;
    *out_minor = card->console_minor;
    return 0;
}

static const char *fake_get_env(void *context, const char *name) {
    fake_card_t *const card = context;
    if (strcmp(name, "DISPLAY") == 0) {
        return card->env_display;
    }
    if (strcmp(name, "WAYLAND_DISPLAY") == 0) {
        return card->env_wayland;
    }
    return nullptr;
}

static int32_t fake_get_resources(fake_card_t *card, struct drm_mode_card_res *res) {
    /* As the kernel does: copies as many as fit, and reports how many there are. */
    uint32_t *const crtcs = (uint32_t *)(uintptr_t)res->crtc_id_ptr;
    uint32_t *const connectors = (uint32_t *)(uintptr_t)res->connector_id_ptr;
    for (uint32_t i = 0; crtcs != nullptr && i < card->crtc_count && i < res->count_crtcs; i++) {
        crtcs[i] = FAKE_CRTC_BASE + i;
    }
    for (uint32_t i = 0; connectors != nullptr && i < card->connector_count && i < res->count_connectors; i++) {
        connectors[i] = FAKE_CONNECTOR_BASE + i;
    }
    res->count_crtcs = card->crtc_count;
    res->count_connectors = card->connector_count;
    res->count_encoders = card->connector_count;
    res->count_fbs = 0u;
    return 0;
}

static int32_t fake_get_connector(fake_card_t *card, struct drm_mode_get_connector *info) {
    const uint32_t index = info->connector_id - FAKE_CONNECTOR_BASE;
    if (index >= card->connector_count) {
        return -ENOENT;
    }
    const fake_connector_t *const connector = &card->connectors[index];
    const bool asking_to_copy = info->count_modes != 0u;
    uint32_t mode_count = connector->mode_count;
    /* A monitor that gains a mode between the two calls: it is counted now, and it is reported as more than asked. */
    if (asking_to_copy && card->grow_connector_after != 0u) {
        card->grow_connector_after--;
        mode_count++;
    }
    /* As the kernel does: the modes are copied only if all of them fit. */
    if (asking_to_copy && info->count_modes >= mode_count && info->modes_ptr != 0u) {
        memcpy((void *)(uintptr_t)info->modes_ptr, connector->modes, sizeof(struct drm_mode_modeinfo) * mode_count);
    }
    if (info->encoders_ptr != 0u && info->count_encoders >= 1u) {
        ((uint32_t *)(uintptr_t)info->encoders_ptr)[0] = FAKE_ENCODER_BASE + index;
    }
    info->count_modes = mode_count;
    info->count_encoders = 1u;
    info->connection = connector->connection;
    info->connector_type = connector->type;
    info->connector_type_id = connector->type_id;
    info->encoder_id = connector->current_crtc != 0u ? FAKE_ENCODER_BASE + index : 0u;
    return 0;
}

static int32_t fake_get_encoder(fake_card_t *card, struct drm_mode_get_encoder *encoder) {
    const uint32_t index = encoder->encoder_id - FAKE_ENCODER_BASE;
    if (index >= card->connector_count) {
        return -ENOENT;
    }
    encoder->crtc_id = card->connectors[index].current_crtc;
    encoder->possible_crtcs = card->connectors[index].possible_crtcs;
    return 0;
}

static int32_t fake_get_crtc(const fake_card_t *card, struct drm_mode_crtc *crtc) {
    const uint32_t index = crtc->crtc_id - FAKE_CRTC_BASE;
    if (index >= card->crtc_count) {
        return -ENOENT;
    }
    crtc->mode_valid = card->crtc_valid[index];
    crtc->fb_id = card->crtc_valid[index] != 0u && card->has_saved_fb != 0u ? FAKE_SAVED_FB : 0u;
    return 0;
}

static void fake_queue_flip_event(fake_card_t *card, uint32_t crtc_id) {
    card->clock_us += card->flip_period_us;
    struct drm_event_vblank event = {0};
    event.base.type = DRM_EVENT_FLIP_COMPLETE;
    event.base.length = sizeof(event);
    event.tv_sec = (uint32_t)(card->clock_us / 1000000u);
    event.tv_usec = (uint32_t)(card->clock_us % 1000000u);
    event.sequence = (uint32_t)card->flips;
    event.crtc_id = crtc_id;
    memcpy(card->events + card->event_bytes, &event, sizeof(event));
    card->event_bytes += sizeof(event);
}

static int32_t fake_page_flip(fake_card_t *card, const struct drm_mode_crtc_page_flip *flip) {
    if (card->event_bytes != 0u) {
        return -EBUSY; /* the real driver's answer to a flip with one pending, which must never happen */
    }
    card->flips++;
    if (card->no_flip_events == 0u) {
        fake_queue_flip_event(card, flip->crtc_id);
    }
    if (card->sigterm_after_flips != 0u && card->flips == card->sigterm_after_flips) {
        raise(SIGTERM);
    }
    if (card->vt_change_after_flips != 0u && card->flips == card->vt_change_after_flips) {
        card->active_vt = card->console_minor + 1u;
    }
    return 0;
}

static int32_t fake_console_ioctl(fake_card_t *card, uint64_t request, void *argument) {
    switch (request) {
    case VT_GETSTATE:
        ((struct vt_stat *)argument)->v_active = (uint16_t)card->active_vt;
        return 0;
    case KDGETMODE:
        *(int32_t *)argument = card->kd_mode;
        return 0;
    case KDSETMODE:
        card->kd_mode = (int32_t)(uintptr_t)argument;
        fake_record(card, request, (uint32_t)(uintptr_t)argument, 0u);
        return 0;
    default:
        return -ENOTTY;
    }
}

static int32_t fake_card_ioctl(fake_card_t *card, uint64_t request, void *argument) {
    switch (request) {
    case DRM_IOCTL_SET_MASTER:
        card->is_master = 1u;
        fake_record(card, request, 0u, 0u);
        return 0;
    case DRM_IOCTL_DROP_MASTER:
        card->is_master = 0u;
        fake_record(card, request, 0u, 0u);
        return 0;
    case DRM_IOCTL_VERSION: {
        struct drm_version *const version = argument;
        strcpy(version->name, "fake");
        version->version_major = 1;
        return 0;
    }
    case DRM_IOCTL_GET_CAP: {
        struct drm_get_cap *const capability = argument;
        capability->value = capability->capability == DRM_CAP_DUMB_BUFFER           ? (card->no_dumb != 0u ? 0u : 1u)
                            : capability->capability == DRM_CAP_TIMESTAMP_MONOTONIC ? card->monotonic
                                                                                    : 0u;
        return 0;
    }
    case DRM_IOCTL_MODE_GETRESOURCES:
        return fake_get_resources(card, argument);
    case DRM_IOCTL_MODE_GETCONNECTOR:
        return fake_get_connector(card, argument);
    case DRM_IOCTL_MODE_GETENCODER:
        return fake_get_encoder(card, argument);
    case DRM_IOCTL_MODE_GETCRTC:
        return fake_get_crtc(card, argument);
    case DRM_IOCTL_MODE_SETCRTC: {
        const struct drm_mode_crtc *const crtc = argument;
        fake_record(card, request, crtc->fb_id, crtc->count_connectors);
        return 0;
    }
    case DRM_IOCTL_MODE_CREATE_DUMB: {
        struct drm_mode_create_dumb *const create = argument;
        card->next_handle++;
        card->live_handles++;
        create->handle = card->next_handle;
        create->pitch = card->pitch_override != 0u ? card->pitch_override : create->width * 4u + card->extra_pitch;
        create->size = (uint64_t)create->pitch * create->height;
        return 0;
    }
    case DRM_IOCTL_MODE_MAP_DUMB:
        ((struct drm_mode_map_dumb *)argument)->offset = 4096u * card->next_handle;
        return 0;
    case DRM_IOCTL_MODE_ADDFB2: {
        struct drm_mode_fb_cmd2 *const framebuffer = argument;
        if (framebuffer->pixel_format != DRM_FORMAT_XRGB8888 || framebuffer->handles[0] == 0u) {
            return -EINVAL;
        }
        card->next_fb++;
        card->live_fbs++;
        framebuffer->fb_id = card->next_fb;
        return 0;
    }
    case DRM_IOCTL_MODE_RMFB:
        card->live_fbs--;
        fake_record(card, request, *(uint32_t *)argument, 0u);
        return 0;
    case DRM_IOCTL_MODE_DESTROY_DUMB:
        card->live_handles--;
        fake_record(card, request, ((struct drm_mode_destroy_dumb *)argument)->handle, 0u);
        return 0;
    case DRM_IOCTL_MODE_PAGE_FLIP:
        return fake_page_flip(card, argument);
    default:
        return -ENOTTY;
    }
}

static int32_t fake_ioctl_call(void *context, int32_t fd, uint64_t request, void *argument) {
    fake_card_t *const card = context;
    if (card->eintr_left != 0u) {
        card->eintr_left--;
        return -EINTR;
    }
    if (request == card->fail_request) {
        if (card->fail_skip == 0u) {
            return -card->fail_errno;
        }
        card->fail_skip--;
    }
    if (fd == FAKE_CONSOLE_FD) {
        return fake_console_ioctl(card, request, argument);
    }
    return fd == FAKE_CARD_FD ? fake_card_ioctl(card, request, argument) : -EBADF;
}

static const display_ops_t fake_ops = {
    .open_card = fake_open_card,
    .close_fd = fake_close_fd,
    .ioctl_call = fake_ioctl_call,
    .map_shared = fake_map_shared,
    .unmap = fake_unmap,
    .wait_readable = fake_wait_readable,
    .read_fd = fake_read_fd,
    .char_device = fake_char_device,
    .get_env = fake_get_env,
};

/* A text console on tty3, with one 1080p120 monitor on CRTC 0, ready to be taken. */
static const struct drm_mode_modeinfo fake_modes_1080p[] = {
    {.clock = 148500u, .hdisplay = 1920u, .htotal = 2200u, .vdisplay = 1080u, .vtotal = 1125u},
    {.clock = 297000u, .hdisplay = 1920u, .htotal = 2200u, .vdisplay = 1080u, .vtotal = 1125u},
};
static const struct drm_mode_modeinfo fake_modes_small[] = {
    {.clock = 8640u, .hdisplay = 320u, .htotal = 400u, .vdisplay = 200u, .vtotal = 180u},
};

static fake_card_t *fake_card_new(test_context_t *t) {
    fake_card_t *const card = test_alloc(t, sizeof(fake_card_t));
    card->borrowed_test = t;
    card->console_major = 4u;
    card->console_minor = 3u;
    card->active_vt = 3u;
    card->kd_mode = KD_TEXT;
    card->crtc_count = 2u;
    card->crtc_valid[0] = 1u;
    card->has_saved_fb = 1u;
    card->monotonic = 1u;
    card->flip_period_us = 8333u;
    card->connector_count = 1u;
    card->connectors[0] = (fake_connector_t){.modes = fake_modes_1080p,
                                             .mode_count = 2u,
                                             .type = DRM_MODE_CONNECTOR_HDMIA,
                                             .type_id = 1u,
                                             .connection = 1u,
                                             .current_crtc = FAKE_CRTC_BASE,
                                             .possible_crtcs = 3u};
    return card;
}

static void fake_card_use_small_mode(fake_card_t *card) {
    card->connectors[0].modes = fake_modes_small;
    card->connectors[0].mode_count = 1u;
}

/* Where the first call of this kind came in the order of recorded calls, or -1. */
static int32_t fake_find_call(const fake_card_t *card, uint64_t request, uint32_t value) {
    for (uint32_t i = 0; i < card->call_count; i++) {
        if (card->calls[i].request == request && card->calls[i].value == value) {
            return (int32_t)i;
        }
    }
    return -1;
}

static display_config_t fake_config(void) {
    return (display_config_t){.card_path = "/fake/card",
                              .connector_name = "auto",
                              .width = 1920u,
                              .height = 1080u,
                              .refresh_hz = 120u,
                              .console_fd = FAKE_CONSOLE_FD};
}

static void check_nothing_left(test_context_t *t, const fake_card_t *card) {
    CHECK_EQ_U64(card->live_handles, 0u);
    CHECK_EQ_U64(card->live_fbs, 0u);
    CHECK_EQ_U64(card->live_maps, 0u);
    CHECK_EQ_U64(card->close_count, card->open_errno != 0 ? 0u : card->open_count);
    heap_counters_t counters;
    heap_counters_read(&counters);
    CHECK_EQ_U64(counters.live_bytes, 0u);
}

#endif
