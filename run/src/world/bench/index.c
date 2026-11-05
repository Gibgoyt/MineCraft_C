/* See README.md. */
#include "world/bench/index.h"

bool bench_half_other(uint16_t block, int32_t *out_step_x, uint16_t *out_other) {
    if (block == BLOCK_ID_WOODWORKING_BENCH_WEST) {
        *out_step_x = 1;
        *out_other = BLOCK_ID_WOODWORKING_BENCH_EAST;
        return true;
    }
    if (block == BLOCK_ID_WOODWORKING_BENCH_EAST) {
        *out_step_x = -1;
        *out_other = BLOCK_ID_WOODWORKING_BENCH_WEST;
        return true;
    }
    return false;
}
