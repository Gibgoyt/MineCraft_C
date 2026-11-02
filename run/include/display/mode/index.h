/* See run/src/display/mode/README.md. */
#ifndef MC_RUN_DISPLAY_MODE_INDEX_H
#define MC_RUN_DISPLAY_MODE_INDEX_H

#include "arena/index.h"
#include "display/device/index.h"

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <stdint.h>

enum : uint32_t {
    DISPLAY_MODE_MAX_CONNECTORS = 16u,
    DISPLAY_MODE_MAX_CRTCS = 16u,
    DISPLAY_MODE_MAX_ENCODERS = 32u,
    DISPLAY_MODE_MAX_CONNECTOR_ENCODERS = 16u,
    DISPLAY_MODE_MAX_MODES = 256u,
    DISPLAY_MODE_NAME_BYTES = 32u,
};

/* What config/machine.yaml asks for. A mode within 1 Hz below refresh_mhz meets it: "120 Hz" is often 119.88. */
typedef struct display_mode_wanted_t {
    const char *connector_name; /* nullptr or "auto" lets the best connected connector win */
    uint64_t refresh_mhz;
    uint32_t width;
    uint32_t height;
} display_mode_wanted_t;
static_assert(sizeof(display_mode_wanted_t) == 24);

/* One connected connector with at least one usable mode, and the best of them. */
typedef struct display_connector_t {
    uint64_t best_mhz;
    struct drm_mode_modeinfo best_mode;
    uint32_t connector_id;
    uint32_t encoder_id; /* the encoder driving it now, or 0 */
    uint32_t type;
    uint32_t type_id;
    uint32_t encoder_count;
    uint32_t encoders[DISPLAY_MODE_MAX_CONNECTOR_ENCODERS];
} display_connector_t;
static_assert(sizeof(display_connector_t) == 160);

/* The chosen output, and what was on its CRTC before us. */
typedef struct display_mode_t {
    uint64_t refresh_mhz;
    display_connector_t *borrowed_connectors; /* from the arena given to _init */
    struct drm_mode_crtc saved_crtc;
    struct drm_mode_modeinfo mode;
    uint32_t connector_count;
    uint32_t connector_id;
    uint32_t crtc_id;
    uint32_t saved_connector_count;
    uint32_t saved_connectors[DISPLAY_MODE_MAX_CONNECTORS];
    char connector_name[DISPLAY_MODE_NAME_BYTES];
    uint8_t _pad0[4];
} display_mode_t;
static_assert(sizeof(display_mode_t) == 304);

/* Pure, so that tests can feed them fixtures. */

/* The real refresh in millihertz, from the timings and not the rounded vrefresh. 0 for nonsense timings. */
uint64_t display_mode_refresh_mhz(const struct drm_mode_modeinfo *mode);

/* Whether a mode can be used at all: progressive, not stereo, sensible timings. */
bool display_mode_is_usable(const struct drm_mode_modeinfo *mode);

/* How good a usable mode is, higher being better: 4 the wanted size and refresh, 3 the refresh at another size, 2 the
 * size at a lower refresh, 1 neither. */
uint32_t display_mode_tier(const struct drm_mode_modeinfo *mode, const display_mode_wanted_t *wanted);

/* Whether candidate beats current: by tier, then by refresh (the nearest to the wanted one where the tier meets it,
 * otherwise the fastest), then the mode the monitor prefers, then the larger area. */
bool display_mode_is_better(const struct drm_mode_modeinfo *candidate, const struct drm_mode_modeinfo *current,
                            const display_mode_wanted_t *wanted);

/* The best usable mode in a list, or nullptr if none is usable. */
const struct drm_mode_modeinfo *display_mode_pick(const struct drm_mode_modeinfo *modes, uint32_t count,
                                                  const display_mode_wanted_t *wanted);

/* Writes a connector's name, such as HDMI-A-1: the same names as /sys/class/drm. Always ends in a NUL. */
void display_mode_connector_name(uint32_t type, uint32_t type_id, char *out, uint64_t capacity);

/* The connector wanted->connector_name names or, for auto, the one whose best mode is best, the earliest on a tie.
 * nullptr if there is none. */
const display_connector_t *display_mode_pick_connector(const display_connector_t *connectors, uint32_t count,
                                                       const display_mode_wanted_t *wanted);

/* Asks the card what is connected, picks a connector, a mode and a CRTC, and remembers what the CRTC holds. Changes
 * nothing on the card. Its memory comes from the arena and lasts as long as the arena's owner keeps it. */
display_status_t display_mode_init(display_mode_t *mode, const display_device_t *device, arena_t *arena,
                                   const display_mode_wanted_t *wanted);
void display_mode_deinit(display_mode_t *mode);

#endif
