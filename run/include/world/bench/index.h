/* See run/src/world/bench/README.md. */
#ifndef MC_RUN_WORLD_BENCH_INDEX_H
#define MC_RUN_WORLD_BENCH_INDEX_H

#include "tables/blocks/index.h"

#include <stdint.h>

/* Whether this block id is either half of a bench two voxels long. */
bool bench_is_half(uint32_t id);

/* Whether this voxel is half of a bench two voxels long. If so, the step on the grid to its other half, and that half as it must
 * be there: the other block, with the same facing. The first half, woodworking_bench_west, is where it was aimed, and the second
 * is to its right as seen by whoever placed it, facing the way it faces: east of it facing north, south facing east, west facing
 * south and north facing west. */
bool bench_half_other(uint16_t voxel, int32_t *out_step_x, int32_t *out_step_y, uint16_t *out_other);

#endif
