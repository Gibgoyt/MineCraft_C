/* See README.md. */
#include "world/bench/index.h"

/* One station of more than one voxel: its parts in order, each where it lies from the first, in the placer's frame: right is to their
 * right hand and ahead is the way they looked, so the way the block faces. */
typedef struct bench_shape_t {
    uint16_t parts[BENCH_PARTS_MAX]; /* 0: block ids, the first where it was aimed */
    int8_t right[BENCH_PARTS_MAX];   /* 8 */
    int8_t ahead[BENCH_PARTS_MAX];   /* 12 */
    uint8_t count;                   /* 16 */
    uint8_t _pad0[1];                /* 17 */
} bench_shape_t;
static_assert(sizeof(bench_shape_t) == 18);

static const bench_shape_t BENCH_SHAPES[] = {
    {.parts = {BLOCK_ID_WOODWORKING_BENCH_WEST, BLOCK_ID_WOODWORKING_BENCH_EAST}, .right = {0, 1}, .count = 2u},
    {.parts = {BLOCK_ID_STONEWORKING_BENCH_LEFT, BLOCK_ID_STONEWORKING_BENCH_RIGHT}, .right = {0, 1}, .count = 2u},
    {.parts = {BLOCK_ID_TANNING_RACK_LEFT, BLOCK_ID_TANNING_RACK_RIGHT}, .right = {0, 1}, .count = 2u},
    {.parts = {BLOCK_ID_LEATHER_WORKBENCH_LEFT, BLOCK_ID_LEATHER_WORKBENCH_RIGHT}, .right = {0, 1}, .count = 2u},
    /* Plan 005 phase 6: 2 by 2 by 1, its front two across the placer's view and the back two beyond them. */
    {.parts = {BLOCK_ID_SMITH_TABLE_FRONT_LEFT, BLOCK_ID_SMITH_TABLE_FRONT_RIGHT, BLOCK_ID_SMITH_TABLE_BACK_LEFT,
               BLOCK_ID_SMITH_TABLE_BACK_RIGHT},
     .right = {0, 1, 0, 1},
     .ahead = {0, 0, 1, 1},
     .count = 4u},
};
enum : uint32_t { BENCH_SHAPE_COUNT = sizeof(BENCH_SHAPES) / sizeof(BENCH_SHAPES[0]) };

/* The shape a block id is a part of, and which part, or false. */
static bool bench_find(uint32_t id, const bench_shape_t **out_shape, uint32_t *out_part) {
    for (uint32_t s = 0u; s < BENCH_SHAPE_COUNT; s++) {
        for (uint32_t p = 0u; p < BENCH_SHAPES[s].count; p++) {
            if (BENCH_SHAPES[s].parts[p] == id) {
                *out_shape = &BENCH_SHAPES[s];
                *out_part = p;
                return true;
            }
        }
    }
    return false;
}

bool bench_is_half(uint32_t id) {
    const bench_shape_t *shape = nullptr;
    uint32_t part = 0u;
    return bench_find(id, &shape, &part);
}

uint32_t bench_parts(uint16_t voxel, int32_t out_steps[BENCH_PARTS_MAX][2], uint16_t out_voxels[BENCH_PARTS_MAX]) {
    const bench_shape_t *shape = nullptr;
    uint32_t here = 0u;
    if (!bench_find(block_state_id(voxel), &shape, &here)) {
        return 0u;
    }
    const uint32_t facing = block_state_facing(voxel);
    /* The placer's right hand: a quarter turn clockwise from the way they looked. */
    const uint32_t right = block_facing_turn(facing, 1u);
    for (uint32_t p = 0u; p < shape->count; p++) {
        const int32_t r = shape->right[p] - shape->right[here], a = shape->ahead[p] - shape->ahead[here];
        out_steps[p][0] = r * block_facing_step_x(right) + a * block_facing_step_x(facing);
        out_steps[p][1] = r * block_facing_step_y(right) + a * block_facing_step_y(facing);
        out_voxels[p] = block_state_with_facing(shape->parts[p], facing);
    }
    return shape->count;
}

bool bench_half_other(uint16_t voxel, int32_t *out_step_x, int32_t *out_step_y, uint16_t *out_other) {
    int32_t steps[BENCH_PARTS_MAX][2];
    uint16_t voxels[BENCH_PARTS_MAX];
    if (bench_parts(voxel, steps, voxels) != 2u) {
        return false;
    }
    const uint32_t other = steps[0][0] == 0 && steps[0][1] == 0 ? 1u : 0u;
    *out_step_x = steps[other][0];
    *out_step_y = steps[other][1];
    *out_other = voxels[other];
    return true;
}
