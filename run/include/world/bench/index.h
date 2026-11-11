/* See run/src/world/bench/README.md. */
#ifndef MC_RUN_WORLD_BENCH_INDEX_H
#define MC_RUN_WORLD_BENCH_INDEX_H

#include "tables/blocks/index.h"

#include <stdint.h>

enum : uint32_t {
    /* The most cells one station takes: the smith table is 2 by 2 (plan 005 phase 6). */
    BENCH_PARTS_MAX = 4u,
};

/* Whether this block id is a part of a station more than one voxel big: a woodworking or stoneworking bench, a tanning rack or a
 * leather workbench, two long, or the smith table, 2 by 2. The name is from when the only one was a bench of two halves. */
bool bench_is_half(uint32_t id);

/* Whether this voxel is a part of such a station. If so, how many parts it has, and for each, in order from the first: the step on the
 * grid from this voxel to that part, and that part's voxel as it must be there: its block, with the same facing. This voxel is one of
 * them, with the step 0. The first part is where the placer aimed, the second to their right as they faced the way it faces, and for
 * the smith table the other two beyond those, away from the placer. */
uint32_t bench_parts(uint16_t voxel, int32_t out_steps[BENCH_PARTS_MAX][2], uint16_t out_voxels[BENCH_PARTS_MAX]);

/* For a station of two parts only, as a bench: whether this voxel is half of one, and if so the step to its other half and that half as
 * it must be. The first half, woodworking_bench_west, is where it was aimed, and the second is to its right as seen by whoever placed it,
 * facing the way it faces: east of it facing north, south facing east, west facing south and north facing west. */
bool bench_half_other(uint16_t voxel, int32_t *out_step_x, int32_t *out_step_y, uint16_t *out_other);

#endif
