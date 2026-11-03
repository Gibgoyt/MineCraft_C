/* See run/src/settings/README.md. */
#ifndef MC_RUN_SETTINGS_INDEX_H
#define MC_RUN_SETTINGS_INDEX_H

#include "arena/index.h"
#include "file/index.h"
#include "heap/index.h"
#include "settings/persist/index.h"

#include <stdint.h>

/* What is stored, and where. There is one, under app_t. The main thread's. */
typedef struct settings_t {
    arena_t arena;              /* 0: over owned_region: lives as long as this does */
    heap_region_t owned_region; /* 64 */
    settings_values_t values;   /* 80: what the game runs with: change these, and settings_save keeps them */
    char *borrowed_path;        /* 112: from arena: where they are kept; empty until settings_load */
    uint8_t *borrowed_text;     /* 120: from arena: a file's worth, for reading and writing */
} settings_t;
static_assert(sizeof(settings_t) == 128);
static_assert(alignof(settings_t) == 64);

/* Reserves its memory, and the values are the defaults. On failure logs why, leaves nothing behind and returns false. */
[[nodiscard]] bool settings_init(settings_t *settings);

/* Releases it. Safe on a zeroed settings_t. */
void settings_deinit(settings_t *settings);

/* Reads what is kept at path over the values, and remembers path for settings_save. No file there yet is what a first run looks like, and says
 * nothing. A file that cannot be read, or says something wrong, is logged, and whatever can be used is: no setting is worth not starting for. */
void settings_load(settings_t *settings, const char *path);

/* Keeps the values at the path settings_load was given, whole or not at all: file_write_atomic. FILE_OK, or what went wrong, with what was there
 * before still there. Takes as long as a sync does, so for a person leaving a screen and not for a frame in play. */
[[nodiscard]] file_status_t settings_save(settings_t *settings);

#endif
