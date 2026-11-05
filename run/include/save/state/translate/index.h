/* See run/src/save/state/translate/README.md. */
#ifndef MC_RUN_SAVE_STATE_TRANSLATE_INDEX_H
#define MC_RUN_SAVE_STATE_TRANSLATE_INDEX_H

#include "save/names/index.h"
#include "save/world/index.h"

#include <stdint.h>

enum : uint64_t {
    /* What a translation works in, of its caller's: one map for each table. */
    SAVE_STATE_TRANSLATE_MAPS_BYTES = (uint64_t)SAVE_NAMES_KIND_COUNT * SAVE_NAMES_MAP_BYTES,
};

/* A whole state file, with sound lengths and hashes, saved with other tables than this build's, rewritten where it lies to mean the same things in this build's: every
 * number, and a new list of names at its end. What this build has no name for goes, and the log says what. Nothing in here is believed yet: every number it does not change
 * is left for save_state_check, which is what must look at the result. file has room for capacity, and maps for SAVE_STATE_TRANSLATE_MAPS_BYTES. Gives the file's new length
 * and how many sections it has, and leaves its header as it was, for the caller. Anything but SAVE_WORLD_OK has logged why, and what is in file is then of no use. */
[[nodiscard]] save_world_status_t save_state_translate(uint8_t *file, uint64_t *in_out_bytes, uint64_t capacity,
                                                       uint16_t *maps, const char *where, uint32_t *out_section_count);

#endif
