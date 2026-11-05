/* See run/src/terrain/fields/README.md. */
#ifndef MC_RUN_TERRAIN_FIELDS_INDEX_H
#define MC_RUN_TERRAIN_FIELDS_INDEX_H

#include "arena/index.h"
#include "terrain/noise/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* The noise fields one world's climate takes from the bank. */
enum : uint32_t {
    TERRAIN_FIELDS_NOISE_FIELDS = 7u,
};

/* One world's climate. Nothing changes it after terrain_fields_init, so any number of workers may read it. */
typedef struct terrain_fields_t {
    const terrain_noise_field_t *borrowed_continentalness; /* 0 */
    const terrain_noise_field_t *borrowed_erosion;         /* 8 */
    const terrain_noise_field_t *borrowed_temperature;     /* 16 */
    const terrain_noise_field_t *borrowed_humidity;        /* 24 */
    const terrain_noise_field_t *borrowed_weirdness;       /* 32: ridges is worked out from this */
    const terrain_noise_field_t *borrowed_warp_x;          /* 40: moves where the other fields are read */
    const terrain_noise_field_t *borrowed_warp_y;          /* 48 */
} terrain_fields_t;
static_assert(sizeof(terrain_fields_t) == 56);

/* Columns whole blocks apart. Filled with x changing fastest: out[ix + count_x * iy] is the column origin + i * step. */
typedef struct terrain_fields_area_t {
    int32_t origin_x; /* 0 */
    int32_t origin_y; /* 4 */
    int32_t step_x;   /* 8 */
    int32_t step_y;   /* 12 */
    uint32_t count_x; /* 16 */
    uint32_t count_y; /* 20 */
} terrain_fields_area_t;
static_assert(sizeof(terrain_fields_area_t) == 24);

/* Where the values go, one array per field. Each holds one float32_t per column and needs only float32_t alignment. A
 * nullptr skips that field, and its noise with it, and changes nothing in the others. Arrays may not overlap. */
typedef struct terrain_fields_arrays_t {
    float32_t *continentalness; /* 0 */
    float32_t *erosion;         /* 8 */
    float32_t *ridges;          /* 16 */
    float32_t *temperature;     /* 24 */
    float32_t *humidity;        /* 32 */
    float32_t *weirdness;       /* 40 */
    float32_t *broad;           /* 48: continentalness from its largest octaves alone, so with no bays or capes in it */
} terrain_fields_arrays_t;
static_assert(sizeof(terrain_fields_arrays_t) == 56);

/* All six at one column. Every value is from -1 to 1: see the README for what each means and how often it comes up. */
typedef struct terrain_fields_point_t {
    float32_t continentalness; /* 0 */
    float32_t erosion;         /* 4 */
    float32_t ridges;          /* 8 */
    float32_t temperature;     /* 12 */
    float32_t humidity;        /* 16 */
    float32_t weirdness;       /* 20 */
    float32_t broad;           /* 24: continentalness from its largest octaves alone */
} terrain_fields_point_t;
static_assert(sizeof(terrain_fields_point_t) == 28);

/* Takes the climate from arena and adds its fields to bank, for bank's seed. For use while terrain_init runs. If the bank
 * refuses a field, which has logged why, sets *out to nullptr and returns false. Fields added before that stay in the bank,
 * so the caller gives up on the whole terrain_t. */
[[nodiscard]] bool terrain_fields_init(terrain_fields_t **out, arena_t *arena, terrain_noise_bank_t *bank);

/* Empties the handle. The memory goes back with the arena. */
void terrain_fields_deinit(terrain_fields_t *fields);

/* How many values an area fills in each array. */
uint64_t terrain_fields_area_columns(const terrain_fields_area_t *area);

/* Fills out with the climate of each column of the area. Allocates nothing, and a value is a pure function of the climate
 * and the column: splitting an area into parts, in any order, on any thread, gives the same bits. */
void terrain_fields_sample_area(const terrain_fields_t *fields, const terrain_fields_area_t *area,
                                const terrain_fields_arrays_t *out);

/* The same at count columns anywhere, in blocks. Must be finite. A whole-number column gives the same bits as an area does
 * there. */
void terrain_fields_sample_points(const terrain_fields_t *fields, const float64_t *xs, const float64_t *ys,
                                  uint64_t count, const terrain_fields_arrays_t *out);

/* The same at one column, for something that asks about a single place, such as finding where to spawn. Columns in bulk
 * cost far less each through terrain_fields_sample_area. */
void terrain_fields_at(const terrain_fields_t *fields, int32_t x, int32_t y, terrain_fields_point_t *out);

#endif
