/* See run/src/climate/normals/README.md. */
#ifndef MC_RUN_CLIMATE_NORMALS_INDEX_H
#define MC_RUN_CLIMATE_NORMALS_INDEX_H

#include "arena/index.h"
#include "terrain/density/index.h"
#include "types/index.h"

#include <stdint.h>

/* Every knot table of plan 004 as a curve (master plan rule 5: one home). Built once, read-only after, by any thread. */
typedef struct climate_normals_t {
    terrain_density_spline_t p_belt;     /* 0: hPa by |psi|, master plan 6.4 */
    terrain_density_spline_t t_mean;     /* 296: deg C, the yearly mean at sea level by |phi|, 6.6 */
    terrain_density_spline_t a_sea;      /* 592: deg C, half the yearly swing over the sea by |phi|, 6.6 */
    terrain_density_spline_t a_land;     /* 888: deg C, the same over land, 6.6 */
} climate_normals_t;
static_assert(sizeof(climate_normals_t) == 1184);

/* Takes the curves from arena (Process lifetime: the climate's normals arena) and builds them. Never fails: the arena's
 * budget is fixed, and running out aborts. */
const climate_normals_t *climate_normals_build(arena_t *arena);

/* The same into memory the caller holds, for tests. */
void climate_normals_fill(climate_normals_t *normals);

/* Each curve at a latitude in degrees, either sign: the tables are by |latitude|, and past 90 they stay at 90's value. */
float64_t climate_normals_p_belt(const climate_normals_t *normals, float64_t psi_deg);
float64_t climate_normals_t_mean(const climate_normals_t *normals, float64_t phi_deg);
float64_t climate_normals_a_sea(const climate_normals_t *normals, float64_t phi_deg);
float64_t climate_normals_a_land(const climate_normals_t *normals, float64_t phi_deg);

#endif
