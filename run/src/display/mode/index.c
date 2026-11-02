/* See README.md. Everything above "Asking the card" is pure, and is what the fixtures in tests/run/display/mode test. */
#include "display/mode/index.h"

#include "log/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum : uint64_t {
    MODE_MHZ_TOLERANCE = 1000u, /* a "120 Hz" mode may really be 119.88 Hz */
};
enum : uint32_t {
    MODE_CONNECTED = 1u,  /* drm_mode_get_connector.connection; the UAPI gives this no name */
    MODE_FILL_TRIES = 4u, /* how often a connector may change between being counted and being read */
};

/* ---- Choosing, from data alone ----------------------------------------------------------------------------------- */

uint64_t display_mode_refresh_mhz(const struct drm_mode_modeinfo *mode) {
    uint64_t pixels = (uint64_t)mode->htotal * (uint64_t)mode->vtotal;
    if ((mode->flags & DRM_MODE_FLAG_DBLSCAN) != 0u) {
        pixels *= 2u;
    }
    if (mode->vscan > 1u) {
        pixels *= mode->vscan;
    }
    if (pixels == 0u || mode->clock == 0u) {
        return 0u;
    }
    return (uint64_t)mode->clock * 1000000u / pixels;
}

bool display_mode_is_usable(const struct drm_mode_modeinfo *mode) {
    return (mode->flags & (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_3D_MASK)) == 0u && mode->hdisplay != 0u &&
           mode->vdisplay != 0u && display_mode_refresh_mhz(mode) != 0u;
}

static bool mode_meets_refresh(uint64_t mhz, const display_mode_wanted_t *wanted) {
    return mhz + MODE_MHZ_TOLERANCE >= wanted->refresh_mhz;
}

uint32_t display_mode_tier(const struct drm_mode_modeinfo *mode, const display_mode_wanted_t *wanted) {
    const bool right_size = mode->hdisplay == wanted->width && mode->vdisplay == wanted->height;
    const bool fast = mode_meets_refresh(display_mode_refresh_mhz(mode), wanted);
    if (fast) {
        return right_size ? 4u : 3u;
    }
    return right_size ? 2u : 1u;
}

static uint64_t mode_distance(uint64_t a, uint64_t b) {
    return a > b ? a - b : b - a;
}

bool display_mode_is_better(const struct drm_mode_modeinfo *candidate, const struct drm_mode_modeinfo *current,
                            const display_mode_wanted_t *wanted) {
    const uint32_t candidate_tier = display_mode_tier(candidate, wanted);
    const uint32_t current_tier = display_mode_tier(current, wanted);
    if (candidate_tier != current_tier) {
        return candidate_tier > current_tier;
    }
    const uint64_t candidate_mhz = display_mode_refresh_mhz(candidate);
    const uint64_t current_mhz = display_mode_refresh_mhz(current);
    if (candidate_mhz != current_mhz) {
        if (mode_meets_refresh(candidate_mhz, wanted)) {
            /* Nearest, so a 240 Hz monitor runs at what the yaml asks for and not what the panel can do. */
            const uint64_t candidate_gap = mode_distance(candidate_mhz, wanted->refresh_mhz);
            const uint64_t current_gap = mode_distance(current_mhz, wanted->refresh_mhz);
            if (candidate_gap != current_gap) {
                return candidate_gap < current_gap;
            }
        } else {
            return candidate_mhz > current_mhz;
        }
    }
    const uint32_t candidate_preferred = candidate->type & DRM_MODE_TYPE_PREFERRED;
    const uint32_t current_preferred = current->type & DRM_MODE_TYPE_PREFERRED;
    if (candidate_preferred != current_preferred) {
        return candidate_preferred != 0u;
    }
    return (uint64_t)candidate->hdisplay * candidate->vdisplay > (uint64_t)current->hdisplay * current->vdisplay;
}

const struct drm_mode_modeinfo *display_mode_pick(const struct drm_mode_modeinfo *modes, uint32_t count,
                                                  const display_mode_wanted_t *wanted) {
    const struct drm_mode_modeinfo *best = nullptr;
    for (uint32_t i = 0; i < count; i++) {
        if (display_mode_is_usable(&modes[i]) && (best == nullptr || display_mode_is_better(&modes[i], best, wanted))) {
            best = &modes[i];
        }
    }
    return best;
}

/* The kernel's own names, from drm_connector_enum_list, for what a machine like this one can have. */
static const char *mode_connector_type_name(uint32_t type) {
    switch (type) {
    case DRM_MODE_CONNECTOR_VGA:
        return "VGA";
    case DRM_MODE_CONNECTOR_DVII:
        return "DVI-I";
    case DRM_MODE_CONNECTOR_DVID:
        return "DVI-D";
    case DRM_MODE_CONNECTOR_DVIA:
        return "DVI-A";
    case DRM_MODE_CONNECTOR_LVDS:
        return "LVDS";
    case DRM_MODE_CONNECTOR_DisplayPort:
        return "DP";
    case DRM_MODE_CONNECTOR_HDMIA:
        return "HDMI-A";
    case DRM_MODE_CONNECTOR_HDMIB:
        return "HDMI-B";
    case DRM_MODE_CONNECTOR_eDP:
        return "eDP";
    case DRM_MODE_CONNECTOR_VIRTUAL:
        return "Virtual";
    case DRM_MODE_CONNECTOR_DSI:
        return "DSI";
    case DRM_MODE_CONNECTOR_DPI:
        return "DPI";
    default:
        return "Unknown";
    }
}

void display_mode_connector_name(uint32_t type, uint32_t type_id, char *out, uint64_t capacity) {
    snprintf(out, capacity, "%s-%" PRIu32, mode_connector_type_name(type), type_id);
}

const display_connector_t *display_mode_pick_connector(const display_connector_t *connectors, uint32_t count,
                                                       const display_mode_wanted_t *wanted) {
    const bool automatic = wanted->connector_name == nullptr || strcmp(wanted->connector_name, "auto") == 0;
    const display_connector_t *picked = nullptr;
    for (uint32_t i = 0; i < count; i++) {
        const display_connector_t *const connector = &connectors[i];
        if (automatic) {
            if (picked == nullptr || display_mode_is_better(&connector->best_mode, &picked->best_mode, wanted)) {
                picked = connector;
            }
        } else {
            char name[DISPLAY_MODE_NAME_BYTES];
            display_mode_connector_name(connector->type, connector->type_id, name, sizeof(name));
            if (strcmp(name, wanted->connector_name) == 0) {
                return connector;
            }
        }
    }
    return picked;
}

/* ---- Asking the card --------------------------------------------------------------------------------------------- */

/* Adds this connector to the list if it is connected and has a usable mode. Returns false only on a failed ioctl. A
 * connector too big for the fixed arrays is left out, since a monitor nobody can drive must not stop one that can be. */
static bool mode_query_connector(display_mode_t *mode, const display_device_t *device, uint32_t connector_id,
                                 struct drm_mode_modeinfo *scratch, const display_mode_wanted_t *wanted) {
    uint32_t encoders[DISPLAY_MODE_MAX_CONNECTOR_ENCODERS] = {0};
    struct drm_mode_get_connector info = {0};
    bool filled = false;
    for (uint32_t attempt = 0; attempt < MODE_FILL_TRIES && !filled; attempt++) {
        /* count_modes == 0 asks the kernel to probe the monitor, and only tells how much there is to copy. */
        info = (struct drm_mode_get_connector){.connector_id = connector_id};
        int32_t result = display_device_ioctl(device, DRM_IOCTL_MODE_GETCONNECTOR, &info);
        if (result != 0) {
            LOG_ERROR("display: GETCONNECTOR %" PRIu32 ": %s", connector_id, strerror(-result));
            return false;
        }
        if (info.connection != MODE_CONNECTED || info.count_modes == 0u) {
            return true;
        }
        if (info.count_modes > DISPLAY_MODE_MAX_MODES || info.count_encoders > DISPLAY_MODE_MAX_CONNECTOR_ENCODERS) {
            LOG_WARN("display: leaving out connector %" PRIu32 ": %" PRIu32 " modes and %" PRIu32
                     " encoders do not fit",
                     connector_id, info.count_modes, info.count_encoders);
            return true;
        }
        const uint32_t asked_modes = info.count_modes;
        const uint32_t asked_encoders = info.count_encoders;
        info.modes_ptr = (uint64_t)(uintptr_t)scratch;
        info.encoders_ptr = (uint64_t)(uintptr_t)encoders;
        info.count_props = 0u;
        result = display_device_ioctl(device, DRM_IOCTL_MODE_GETCONNECTOR, &info);
        if (result != 0) {
            LOG_ERROR("display: GETCONNECTOR %" PRIu32 " again: %s", connector_id, strerror(-result));
            return false;
        }
        /* The kernel copies modes only if all of them fit, and otherwise only says how many there are now. */
        filled = info.count_modes <= asked_modes && info.count_encoders <= asked_encoders;
    }
    if (!filled) {
        LOG_WARN("display: leaving out connector %" PRIu32 ": it keeps changing", connector_id);
        return true;
    }
    const struct drm_mode_modeinfo *const best = display_mode_pick(scratch, info.count_modes, wanted);
    if (best == nullptr) {
        return true;
    }
    if (mode->connector_count == DISPLAY_MODE_MAX_CONNECTORS) {
        LOG_WARN("display: leaving out connector %" PRIu32 ": too many are connected", connector_id);
        return true;
    }
    display_connector_t *const connector = &mode->borrowed_connectors[mode->connector_count];
    memset(connector, 0, sizeof(*connector));
    connector->best_mode = *best;
    connector->best_mhz = display_mode_refresh_mhz(best);
    connector->connector_id = connector_id;
    connector->encoder_id = info.encoder_id;
    connector->type = info.connector_type;
    connector->type_id = info.connector_type_id;
    connector->encoder_count = info.count_encoders;
    memcpy(connector->encoders, encoders, sizeof(encoders));
    mode->connector_count++;
    return true;
}

/* Reuses the CRTC already driving the connector, so the restore puts back exactly what was there. Otherwise a free one,
 * so that another monitor keeps its picture. Otherwise any it can use. 0 if there is none. */
static uint32_t mode_pick_crtc(const display_device_t *device, const display_connector_t *connector,
                               const uint32_t *crtcs, uint32_t crtc_count) {
    if (connector->encoder_id != 0u) {
        struct drm_mode_get_encoder encoder = {.encoder_id = connector->encoder_id};
        if (display_device_ioctl(device, DRM_IOCTL_MODE_GETENCODER, &encoder) == 0 && encoder.crtc_id != 0u) {
            return encoder.crtc_id;
        }
    }
    uint32_t first_possible = 0;
    for (uint32_t e = 0; e < connector->encoder_count; e++) {
        struct drm_mode_get_encoder encoder = {.encoder_id = connector->encoders[e]};
        if (display_device_ioctl(device, DRM_IOCTL_MODE_GETENCODER, &encoder) != 0) {
            continue;
        }
        for (uint32_t c = 0; c < crtc_count; c++) {
            if ((encoder.possible_crtcs & (1u << c)) == 0u) {
                continue;
            }
            first_possible = first_possible == 0u ? crtcs[c] : first_possible;
            struct drm_mode_crtc state = {.crtc_id = crtcs[c]};
            if (display_device_ioctl(device, DRM_IOCTL_MODE_GETCRTC, &state) == 0 && state.mode_valid == 0u) {
                return crtcs[c];
            }
        }
    }
    return first_possible;
}

/* Every connector on this CRTC goes back on it at exit, not only the one we drive. */
static void mode_collect_saved_connectors(display_mode_t *mode, const display_device_t *device) {
    mode->saved_connector_count = 0u;
    for (uint32_t i = 0; i < mode->connector_count; i++) {
        const display_connector_t *const connector = &mode->borrowed_connectors[i];
        if (connector->encoder_id == 0u) {
            continue;
        }
        struct drm_mode_get_encoder encoder = {.encoder_id = connector->encoder_id};
        if (display_device_ioctl(device, DRM_IOCTL_MODE_GETENCODER, &encoder) == 0 &&
            encoder.crtc_id == mode->crtc_id) {
            mode->saved_connectors[mode->saved_connector_count] = connector->connector_id;
            mode->saved_connector_count++;
        }
    }
}

static void mode_log_connectors(const display_mode_t *mode) {
    for (uint32_t i = 0; i < mode->connector_count; i++) {
        const display_connector_t *const connector = &mode->borrowed_connectors[i];
        char name[DISPLAY_MODE_NAME_BYTES];
        display_mode_connector_name(connector->type, connector->type_id, name, sizeof(name));
        LOG_INFO("display: connected: %s, best mode %" PRIu32 "x%" PRIu32 " at %" PRIu64 ".%03" PRIu64 " Hz", name,
                 (uint32_t)connector->best_mode.hdisplay, (uint32_t)connector->best_mode.vdisplay,
                 connector->best_mhz / 1000u, connector->best_mhz % 1000u);
    }
}

static uint32_t mode_smaller(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

display_status_t display_mode_init(display_mode_t *mode, const display_device_t *device, arena_t *arena,
                                   const display_mode_wanted_t *wanted) {
    memset(mode, 0, sizeof(*mode));
    uint32_t crtcs[DISPLAY_MODE_MAX_CRTCS] = {0};
    uint32_t connector_ids[DISPLAY_MODE_MAX_CONNECTORS] = {0};
    uint32_t encoder_ids[DISPLAY_MODE_MAX_ENCODERS] = {0};
    struct drm_mode_card_res resources = {0};
    int32_t result = display_device_ioctl(device, DRM_IOCTL_MODE_GETRESOURCES, &resources);
    if (result != 0) {
        LOG_ERROR("display: GETRESOURCES: %s", strerror(-result));
        return DISPLAY_STATUS_ERROR;
    }
    /* The kernel copies as many as fit and reports how many there are, so the sizes asked for are the ones to trust. */
    const uint32_t crtc_count = mode_smaller(resources.count_crtcs, DISPLAY_MODE_MAX_CRTCS);
    const uint32_t connector_count = mode_smaller(resources.count_connectors, DISPLAY_MODE_MAX_CONNECTORS);
    resources.count_fbs = 0u;
    resources.count_crtcs = crtc_count;
    resources.count_connectors = connector_count;
    resources.count_encoders = mode_smaller(resources.count_encoders, DISPLAY_MODE_MAX_ENCODERS);
    resources.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
    resources.connector_id_ptr = (uint64_t)(uintptr_t)connector_ids;
    resources.encoder_id_ptr = (uint64_t)(uintptr_t)encoder_ids;
    result = display_device_ioctl(device, DRM_IOCTL_MODE_GETRESOURCES, &resources);
    if (result != 0) {
        LOG_ERROR("display: GETRESOURCES again: %s", strerror(-result));
        return DISPLAY_STATUS_ERROR;
    }

    mode->borrowed_connectors = arena_alloc_array(arena, DISPLAY_MODE_MAX_CONNECTORS, sizeof(display_connector_t),
                                                  alignof(display_connector_t));
    struct drm_mode_modeinfo *const scratch = arena_alloc_array(
        arena, DISPLAY_MODE_MAX_MODES, sizeof(struct drm_mode_modeinfo), alignof(struct drm_mode_modeinfo));
    for (uint32_t i = 0; i < connector_count; i++) {
        if (!mode_query_connector(mode, device, connector_ids[i], scratch, wanted)) {
            display_mode_deinit(mode);
            return DISPLAY_STATUS_ERROR;
        }
    }
    mode_log_connectors(mode);

    const display_connector_t *const connector =
        display_mode_pick_connector(mode->borrowed_connectors, mode->connector_count, wanted);
    if (connector == nullptr) {
        const bool named = wanted->connector_name != nullptr && strcmp(wanted->connector_name, "auto") != 0;
        LOG_ERROR("display: no usable connected connector%s%s", named ? " named " : "",
                  named ? wanted->connector_name : "");
        display_mode_deinit(mode);
        return DISPLAY_STATUS_ERROR;
    }
    mode->mode = connector->best_mode;
    mode->refresh_mhz = connector->best_mhz;
    mode->connector_id = connector->connector_id;
    display_mode_connector_name(connector->type, connector->type_id, mode->connector_name,
                                sizeof(mode->connector_name));
    mode->crtc_id = mode_pick_crtc(device, connector, crtcs, crtc_count);
    if (mode->crtc_id == 0u) {
        LOG_ERROR("display: no CRTC can drive %s", mode->connector_name);
        display_mode_deinit(mode);
        return DISPLAY_STATUS_ERROR;
    }

    mode->saved_crtc = (struct drm_mode_crtc){.crtc_id = mode->crtc_id};
    result = display_device_ioctl(device, DRM_IOCTL_MODE_GETCRTC, &mode->saved_crtc);
    if (result != 0) {
        LOG_ERROR("display: GETCRTC %" PRIu32 ": %s", mode->crtc_id, strerror(-result));
        display_mode_deinit(mode);
        return DISPLAY_STATUS_ERROR;
    }
    mode_collect_saved_connectors(mode, device);
    LOG_INFO("display: picked %s on crtc %" PRIu32 ", %" PRIu32 "x%" PRIu32 " at %" PRIu64 ".%03" PRIu64 " Hz",
             mode->connector_name, mode->crtc_id, (uint32_t)mode->mode.hdisplay, (uint32_t)mode->mode.vdisplay,
             mode->refresh_mhz / 1000u, mode->refresh_mhz % 1000u);
    if (display_mode_tier(&mode->mode, wanted) != 4u) {
        LOG_WARN("display: config/machine.yaml asks for %" PRIu32 "x%" PRIu32 " at %" PRIu64
                 " Hz, which %s does not have",
                 wanted->width, wanted->height, wanted->refresh_mhz / 1000u, mode->connector_name);
    }
    return DISPLAY_STATUS_OK;
}

void display_mode_deinit(display_mode_t *mode) {
    memset(mode, 0, sizeof(*mode));
}
