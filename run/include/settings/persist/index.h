/* See run/src/settings/persist/README.md. */
#ifndef MC_RUN_SETTINGS_PERSIST_INDEX_H
#define MC_RUN_SETTINGS_PERSIST_INDEX_H

#include "gpu_bridge/shade/index.h"
#include "machine/index.h"
#include "render/lod/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    SETTINGS_VERSION = 1u,
    /* The plan's range. */
    SETTINGS_RENDER_DISTANCE_MIN = 2u,
    SETTINGS_RENDER_DISTANCE_MAX = MC_GAME_RENDER_DISTANCE_MAX,
    SETTINGS_FULL_DETAIL_MIN = 2u,
    /* What leaves room on the card for a far field: a table of 101 by 101 gets 3.9 GiB of slabs, and gpu.budget.voxels_mib is 6 GiB. */
    SETTINGS_FULL_DETAIL_MAX = 48u,
    SETTINGS_FOV_MIN = 30u,
    SETTINGS_FOV_DEFAULT = 70u,
    SETTINGS_FOV_MAX = 110u,
    SETTINGS_SENSITIVITY_MIN = 10u,
    SETTINGS_SENSITIVITY_DEFAULT = 100u,
    SETTINGS_SENSITIVITY_MAX = 400u,
    SETTINGS_RENDER_SCALE_MIN = 25u,
    SETTINGS_RENDER_SCALE_DEFAULT = 100u,
    SETTINGS_RENDER_SCALE_MAX = 100u,
    SETTINGS_WORKERS_MIN = 1u,
    SETTINGS_WORKERS_MAX = MC_THREADS_WORKERS,
    SETTINGS_SOUND_VOLUME_MIN = 0u,
    SETTINGS_SOUND_VOLUME_DEFAULT = 70u,
    SETTINGS_SOUND_VOLUME_MAX = 100u,
    SETTINGS_EFFECT_COUNT = 18u,
    /* The first this many have a box on the Effects page, which has room for no more: two columns of eight and one on Back's row.
     * The rest are kept in the file all the same (plan 004 phase 08's weather; its box waits for the screen to have room). */
    SETTINGS_EFFECT_ON_SCREEN = 17u,
    SETTINGS_LINE_BYTES = 120u, /* a line past this is not one this program wrote, and is not read */
    /* What settings_persist_write can ever need, with room for a person's comments in what is read. */
    SETTINGS_TEXT_BYTES = 8192u,
};

/* All of them, in the units a person sees. */
typedef struct settings_values_t {
    uint32_t render_distance;      /* 0: chunks, however far off the world goes on */
    uint32_t full_detail;          /* 4: chunks drawn block by block, and coarse grids past that */
    uint32_t fov_degrees;          /* 8: vertical */
    uint32_t sensitivity_percent;  /* 12: of the mouse's usual turn */
    uint32_t render_scale_percent; /* 16: of width and height */
    uint32_t worker_count;         /* 20: of MC_THREADS_WORKERS, from the next start */
    uint32_t effects;              /* 24: SHADE_FEATURE_* that are on */
    uint8_t vsync;                 /* 28: flips wait for a vblank, and no picture is drawn past it */
    uint8_t show_coordinates;      /* 29: x, y and z in the top left corner, while playing */
    uint8_t
        sound_volume_percent;    /* 30: what the game's sounds are turned down to, on top of the desktop's own volume */
    uint8_t show_date;           /* 31: the date line in the top right corner, while playing */
    uint8_t climate_hud;         /* 32: SETTINGS_CLIMATE_HUD_* that are on: plan 004 phase 10's lines under the date */
    uint8_t pause_on_focus_loss; /* 33: a window losing the focus pauses the game, as in Minecraft (plan 005 phase 7) */
    uint8_t _pad0[2];            /* 34 */
} settings_values_t;
static_assert(sizeof(settings_values_t) == 36);

/* Plan 004 phase 10: the lines under the date line, each with its box on the HUD page and its line in the file. */
enum : uint8_t {
    SETTINGS_CLIMATE_HUD_LINE = 1u << 0,     /* the temperature and wind line: show_climate */
    SETTINGS_CLIMATE_HUD_WARNINGS = 1u << 1, /* storm warnings: storm_warnings */
    SETTINGS_CLIMATE_HUD_ALL = SETTINGS_CLIMATE_HUD_LINE | SETTINGS_CLIMATE_HUD_WARNINGS,
};

/* One graphics effect: what its line in the file is called, and what the screen calls it. */
typedef struct settings_effect_t {
    const char *key;   /* 0 */
    const char *label; /* 8 */
    uint32_t bit;      /* 16: SHADE_FEATURE_* */
    uint8_t _pad0[4];  /* 20 */
} settings_effect_t;
static_assert(sizeof(settings_effect_t) == 24);

/* What a parse found, for whoever wants to say so. */
typedef struct settings_persist_report_t {
    uint32_t applied; /* 0: lines that set something */
    uint32_t
        problems; /* 4: lines that could not, or set something other than what they said; each is logged with its number */
} settings_persist_report_t;
static_assert(sizeof(settings_persist_report_t) == 8);

/* Every effect, in the order of the screen and the file. */
const settings_effect_t *settings_persist_effect(uint32_t index);

/* What a new install runs with. */
void settings_persist_defaults(settings_values_t *out);

/* Each value into its range, nearest end. effects loses anything that is not an effect. */
void settings_persist_clamp(settings_values_t *values);

/* Reads text over what values already holds, so a line that is missing or wrong leaves that setting as it was. Lines are "key: value", with blank
 * lines and # comments allowed, in any order; the last of two lines for one key wins. A number past its range is held to it. A file whose version
 * is not this program's sets nothing at all. Never fails, as no file is worth not starting for: each problem is logged with its line. */
void settings_persist_parse(const uint8_t *text, uint64_t length, settings_values_t *values,
                            settings_persist_report_t *out_report);

/* The file for these values, into out. False if capacity is too small, which SETTINGS_TEXT_BYTES never is. */
bool settings_persist_write(const settings_values_t *values, uint8_t *out, uint64_t capacity, uint64_t *out_bytes);

#endif
