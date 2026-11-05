/* See run/src/world/bench/README.md. */
#ifndef MC_RUN_WORLD_BENCH_INDEX_H
#define MC_RUN_WORLD_BENCH_INDEX_H

#include "tables/blocks/index.h"

#include <stdint.h>

/* Whether this block is half of a bench two voxels long. If so, the way along x to its other half, 1 or -1, and what that half is. */
bool bench_half_other(uint16_t block, int32_t *out_step_x, uint16_t *out_other);

#endif
