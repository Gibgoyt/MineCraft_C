/* See README.md. Hurricane names made from syllables by a system's name seed (master plan 7.2), so each world has its own:
 * "Hurricane Tavela", "Tropical storm Orin". */
#include "climate/systems/name/index.h"

#include <string.h>

/* The first six give the master plan's two examples: 4161 is ta-ve-la, 262 is o-rin. */
static const char *const NAME_SYLLABLES[CLIMATE_SYSTEM_SYLLABLES] = {
    "ta", "ve", "la", "o",   "rin", "ka", "mi", "so", "len", "du", "ra", "ni",
    "ko", "sa", "el", "mar", "ti",  "no", "ba", "lu", "fe",  "ar", "zo", "wen",
};

void climate_system_name(uint32_t name_seed, char out[9]) {
    const uint32_t syllables = (name_seed & 1u) != 0u ? 3u : 2u;
    uint32_t length = 0u;
    for (uint32_t k = 0u; k < syllables; k++) {
        const char *const syllable = NAME_SYLLABLES[((name_seed >> (1u + 5u * k)) & 31u) % CLIMATE_SYSTEM_SYLLABLES];
        const uint32_t bytes = (uint32_t)strlen(syllable);
        if (length + bytes > CLIMATE_SYSTEM_NAME_LETTERS) {
            break; /* only the third can overflow: two syllables are at most 6 letters */
        }
        memcpy(out + length, syllable, bytes);
        length += bytes;
    }
    out[length] = '\0';
    if (out[0] >= 'a' && out[0] <= 'z') {
        out[0] = (char)(out[0] - 'a' + 'A');
    }
}

void climate_system_title(const climate_system_t *system, float64_t t, char out[24]) {
    char name[CLIMATE_SYSTEM_NAME_BYTES];
    climate_system_name(system->name_seed, name);
    const char *prefix = "Low ";
    if (system->kind == CLIMATE_SYSTEM_HURRICANE) {
        climate_system_state_t state;
        climate_system_state_at(system, t, &state);
        prefix = state.wind_max >= CLIMATE_HURRICANE_STORM_VMAX ? "Hurricane " : "Tropical storm ";
    } else if (system->kind == CLIMATE_SYSTEM_HIGH) {
        prefix = "High ";
    }
    const uint64_t prefix_bytes = strlen(prefix), name_bytes = strlen(name);
    memcpy(out, prefix, prefix_bytes);
    memcpy(out + prefix_bytes, name, name_bytes + 1u); /* 15 + 8 + 1 = 24 at the most */
}
