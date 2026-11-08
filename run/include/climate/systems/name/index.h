/* See run/src/climate/systems/name/README.md. */
#ifndef MC_RUN_CLIMATE_SYSTEMS_NAME_INDEX_H
#define MC_RUN_CLIMATE_SYSTEMS_NAME_INDEX_H

#include "climate/systems/index.h"
#include "types/index.h"

#include <stdint.h>

enum : uint32_t {
    CLIMATE_SYSTEM_SYLLABLES = 24u,
    CLIMATE_SYSTEM_NAME_LETTERS = 8u, /* so "Tropical storm " and the name fit phase 07's char name[24] */
    CLIMATE_SYSTEM_NAME_BYTES = 9u,
    CLIMATE_SYSTEM_TITLE_BYTES = 24u,
    CLIMATE_SYSTEM_NAME_SYLLABLES_MAX = 3u, /* phase 06: two or three syllables, by the seed's low bit */
    CLIMATE_SYSTEM_NAME_SYLLABLE_BITS = 5u, /* each syllable from 5 bits of the seed */
};

/* The bare name from a seed: 2 or 3 syllables, at most 8 letters, first letter upper case, into out (at least 9 bytes). */
void climate_system_name(uint32_t name_seed, char out[CLIMATE_SYSTEM_NAME_BYTES]);
/* "Hurricane Tavela" or "Tropical storm Tavela", by v_max at t; a low or high is "Low Tavela" or "High Tavela". out holds
 * 24 bytes. */
void climate_system_title(const climate_system_t *system, float64_t t, char out[CLIMATE_SYSTEM_TITLE_BYTES]);

#endif
