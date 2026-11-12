/* See README.md beside this. bench_landform's pictures. */
#ifndef MC_BENCH_LANDFORM_DRAW_INDEX_H
#define MC_BENCH_LANDFORM_DRAW_INDEX_H

#include "landform/measure/index.h"

/* <seed>_relief_64km.png, _slope_64km.png and _plateaus_64km.png, from the grid and what lf_measure left in m. */
bool lf_draw_maps(const lf_grid_t *grid, const lf_memory_t *m, const lf_result_t *r);

/* <seed>_profiles.png: true-scale cross-sections through the places r holds. */
bool lf_draw_profiles(const terrain_t *terrain, const lf_grid_t *grid, const lf_memory_t *m, const lf_result_t *r);

/* <seed>_strip.png, from m->strip. */
bool lf_draw_strip(const lf_memory_t *m, uint64_t seed);

/* <seed>_windows.png, from the real blocks of the windows. */
bool lf_draw_windows(const lf_memory_t *m, uint64_t seed, uint32_t windows_side);

#endif
