/* See README.md. The coarse pass, and the kernels that run traverse_trace for a frame and for a list of rays. */
#include "traverse/index.hu"

#include <math.h>

enum : uint32_t {
    /* Stopping early only costs the time the pass could have saved, so these are limits on work and not on correctness. */
    TRAVERSE_COARSE_MAX_STEPS = 512u,
    TRAVERSE_COARSE_MAX_COLUMNS = 16u,
};

/* The stretches of ray the coarse march tries to clear at once, longest first. See traverse_coarse_tile. */
#define TRAVERSE_COARSE_LONG_STRETCH 16.0F
#define TRAVERSE_COARSE_SHORT_STRETCH 4.0F
#define TRAVERSE_COARSE_LONG_PER_BLOCK 0.25F

/* What a position in float32_t may be off by, in blocks, after t blocks: well over the rounding of frac + d * t. */
#define TRAVERSE_COARSE_PAD 0.0625F
#define TRAVERSE_COARSE_PAD_PER_BLOCK 4e-6F
/* What comes off the safe distance at the end, so the start cell is not decided by rounding either. */
#define TRAVERSE_COARSE_BACK_OFF 0.25F

static __device__ __forceinline__ void traverse_camera_direction(const traverse_camera_t *camera, float32_t pixel_x,
                                                                 float32_t pixel_y, float32_t out[3]) {
    const float32_t u = 2.0F * pixel_x / (float32_t)camera->width - 1.0F;
    const float32_t v = 1.0F - 2.0F * pixel_y / (float32_t)camera->height;
    const float32_t x = camera->forward[0] + u * camera->right[0] + v * camera->up[0];
    const float32_t y = camera->forward[1] + u * camera->right[1] + v * camera->up[1];
    const float32_t z = camera->forward[2] + u * camera->right[2] + v * camera->up[2];
    const float32_t scale = 1.0F / sqrtf(x * x + y * y + z * z);
    out[0] = x * scale;
    out[1] = y * scale;
    out[2] = z * scale;
}

/* The bits of a 4 x 4 x 4 mask inside an inclusive range: a section's bricks and a brick's voxels are numbered alike. */
static __device__ __forceinline__ uint64_t traverse_brick_region(int32_t x0, int32_t x1, int32_t y0, int32_t y1,
                                                                 int32_t z0, int32_t z1) {
    const uint32_t row = ((1u << (x1 - x0 + 1)) - 1u) << x0;
    const uint32_t layer = (row * 0x1111u) & (((1u << ((y1 - y0 + 1) * 4)) - 1u) << (y0 * 4));
    const int32_t layers = z1 - z0 + 1;
    const uint64_t all_layers = layers == 4 ? ~0ull : (1ull << (layers * 16)) - 1ull;
    return ((uint64_t)layer * 0x0001000100010001ull) & (all_layers << (z0 * 16));
}

/* Is every voxel of this inclusive box of cells empty? With look_inside the answer is exact, and without it the answer is for
 * whole sections, so it may say no when the box is empty, as it may for a box too wide to look at. Never says yes when it is not. What lies outside the window is empty, since no ray looks there. */
static __device__ __forceinline__ bool traverse_box_is_empty(const traverse_view_t *view, const int32_t low[3],
                                                             const int32_t high[3], bool look_inside) {
    const int32_t window_x = view->window_min_block[0], window_y = view->window_min_block[1];
    const int32_t x0 = low[0] > window_x ? low[0] : window_x;
    const int32_t y0 = low[1] > window_y ? low[1] : window_y;
    const int32_t z0 = low[2] > 0 ? low[2] : 0;
    const int32_t x1 = traverse_clamp(high[0], x0 - 1, window_x + (int32_t)view->window_blocks - 1);
    const int32_t y1 = traverse_clamp(high[1], y0 - 1, window_y + (int32_t)view->window_blocks - 1);
    const int32_t z1 = traverse_clamp(high[2], z0 - 1, (int32_t)TRAVERSE_HEIGHT_BLOCKS - 1);
    if (x0 > x1 || y0 > y1 || z0 > z1) {
        return true;
    }
    const int32_t column_x0 = x0 >> 4, column_x1 = x1 >> 4, column_y0 = y0 >> 4, column_y1 = y1 >> 4;
    if ((uint32_t)((column_x1 - column_x0 + 1) * (column_y1 - column_y0 + 1)) > TRAVERSE_COARSE_MAX_COLUMNS) {
        return false;
    }
    for (int32_t column_y = column_y0; column_y <= column_y1; column_y += 1) {
        for (int32_t column_x = column_x0; column_x <= column_x1; column_x += 1) {
            const traverse_column_t column = view->columns[traverse_slot(view, column_x, column_y)];
            const int32_t top = (int32_t)column.top - 1;
            const int32_t z_end = z1 < top ? z1 : top;
            if (z0 > z_end) {
                continue;
            }
            const int32_t section_0 = z0 >> 4, section_1 = z_end >> 4;
            uint64_t found = column.section_mask & (((1ull << (section_1 - section_0 + 1)) - 1ull) << section_0);
            if (found == 0u) {
                continue;
            }
            if (!look_inside) {
                return false;
            }
            /* What of the box lies in this column, as brick numbers. */
            const int32_t left = column_x << 4, bottom = column_y << 4;
            const int32_t brick_x0 = ((x0 > left ? x0 : left) - left) >> 2;
            const int32_t brick_x1 = ((x1 < left + 15 ? x1 : left + 15) - left) >> 2;
            const int32_t brick_y0 = ((y0 > bottom ? y0 : bottom) - bottom) >> 2;
            const int32_t brick_y1 = ((y1 < bottom + 15 ? y1 : bottom + 15) - bottom) >> 2;
            while (found != 0u) {
                const int32_t section_z = __ffsll((int64_t)found) - 1;
                found &= found - 1u;
                const uint64_t below = column.section_mask & ((1ull << section_z) - 1ull);
                const traverse_section_t section = view->sections[column.first_section + (uint32_t)__popcll(below)];
                if (section.first_brick == TRAVERSE_SECTION_UNIFORM) {
                    return false;
                }
                const int32_t floor_z = section_z << 4;
                const int32_t brick_z0 = ((z0 > floor_z ? z0 : floor_z) - floor_z) >> 2;
                const int32_t brick_z1 = ((z_end < floor_z + 15 ? z_end : floor_z + 15) - floor_z) >> 2;
                const uint64_t region =
                    traverse_brick_region(brick_x0, brick_x1, brick_y0, brick_y1, brick_z0, brick_z1);
                uint64_t bricks = section.brick_mask & region;
                while (bricks != 0u) {
                    const int32_t brick = __ffsll((int64_t)bricks) - 1;
                    bricks &= bricks - 1u;
                    const uint64_t earlier = section.brick_mask & ((1ull << brick) - 1ull);
                    const uint64_t voxels = view->brick_masks[section.first_brick + (uint32_t)__popcll(earlier)];
                    const int32_t cell_x = left + (brick & 3) * 4, cell_y = bottom + ((brick >> 2) & 3) * 4;
                    const int32_t cell_z = floor_z + (brick >> 4) * 4;
                    const uint64_t cells = traverse_brick_region(
                        (x0 > cell_x ? x0 : cell_x) - cell_x, (x1 < cell_x + 3 ? x1 : cell_x + 3) - cell_x,
                        (y0 > cell_y ? y0 : cell_y) - cell_y, (y1 < cell_y + 3 ? y1 : cell_y + 3) - cell_y,
                        (z0 > cell_z ? z0 : cell_z) - cell_z, (z_end < cell_z + 3 ? z_end : cell_z + 3) - cell_z);
                    if ((voxels & cells) != 0u) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

/* Can any ray of the tile hit something between t and t + stretch? See traverse_coarse_tile for why this box answers that. */
static __device__ __forceinline__ bool traverse_coarse_stretch_is_empty(const traverse_view_t *view,
                                                                        const int32_t eye[3], const float32_t frac[3],
                                                                        const float32_t axis[3], float32_t chord,
                                                                        float32_t t, float32_t stretch,
                                                                        bool look_inside) {
    const float32_t t_end = t + stretch;
    const float32_t grow = chord * t_end + TRAVERSE_COARSE_PAD + t_end * TRAVERSE_COARSE_PAD_PER_BLOCK;
    int32_t low[3], high[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float32_t from = frac[i] + axis[i] * t, to = frac[i] + axis[i] * t_end;
        low[i] = (int32_t)floorf(fminf(from, to) - grow) + eye[i];
        high[i] = (int32_t)floorf(fmaxf(from, to) + grow) + eye[i];
    }
    return traverse_box_is_empty(view, low, high, look_inside);
}

/* The safe distance for one tile.
 *
 * The tile's four corner rays lie in a cone around its centre ray. If c is the largest distance from the centre's unit
 * direction to a corner's, then at the same t any ray R of the tile is within c * t of the centre ray's point A(t), since a cone
 * is convex and every ray of the tile is inside it. So for t0 <= t <= t1, R(t) lies in the bounding box of the segment A(t0) to
 * A(t1), grown by c * t1 on each side. If that box is empty, no ray of the tile hits anything in that stretch. That is the whole
 * proof of the pass, and its test compares against an exact trace of every pixel.
 *
 * It tries a long stretch by sections, and a short one voxel by voxel, and stops when neither is clear. The box hugs the
 * segment, and is not a cube around a point: a cube around the eye of someone standing on the ground always holds the ground,
 * so every tile would say 0, as this pass's first version did (the stage file's issue 2). */
static __device__ __forceinline__ float32_t traverse_coarse_tile(const traverse_view_t *view,
                                                                 const traverse_camera_t *camera, uint32_t tile_x,
                                                                 uint32_t tile_y) {
    const float32_t tile = (float32_t)TRAVERSE_TILE_PIXELS;
    const float32_t left = (float32_t)tile_x * tile, bottom = (float32_t)tile_y * tile;
    float32_t axis[3];
    traverse_camera_direction(camera, left + 0.5F * tile, bottom + 0.5F * tile, axis);
    float32_t chord_squared = 0.0F;
    uint32_t sign_positive = 7u, sign_negative = 7u; /* one bit per axis: every corner ray goes that way */
    for (uint32_t corner = 0u; corner < 4u; corner += 1u) {
        float32_t direction[3];
        traverse_camera_direction(camera, left + (float32_t)(corner & 1u) * tile,
                                  bottom + (float32_t)(corner >> 1) * tile, direction);
        float32_t squared = 0.0F;
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            const float32_t gap = direction[i] - axis[i];
            squared += gap * gap;
            sign_positive &= direction[i] > 0.0F ? 7u : ~(1u << i);
            sign_negative &= direction[i] < 0.0F ? 7u : ~(1u << i);
        }
        chord_squared = fmaxf(chord_squared, squared);
    }
    const float32_t chord = sqrtf(chord_squared) + 1e-5F;
    const float32_t frac[3] = {camera->eye_frac[0], camera->eye_frac[1], camera->eye_frac[2]};
    const int32_t eye[3] = {camera->eye_cell[0], camera->eye_cell[1], camera->eye_cell[2] - TRAVERSE_Z_BASE};
    const int32_t window_low[3] = {view->window_min_block[0], view->window_min_block[1], 0};
    const int32_t window_high[3] = {view->window_min_block[0] + (int32_t)view->window_blocks,
                                    view->window_min_block[1] + (int32_t)view->window_blocks,
                                    (int32_t)TRAVERSE_HEIGHT_BLOCKS};
    float32_t t = 0.0F;
    for (uint32_t step = 0u; step < TRAVERSE_COARSE_MAX_STEPS; step += 1u) {
        /* Every ray of the tile is past this side of the window and only moves further out: nothing more can be hit. Which can
         * only be said on an axis all four corners agree on, and a tile of a real camera is narrow enough to have one. */
        const float32_t reach = chord * t + TRAVERSE_COARSE_PAD + t * TRAVERSE_COARSE_PAD_PER_BLOCK;
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            const float32_t point = frac[i] + axis[i] * t;
            const bool past_high =
                ((sign_positive >> i) & 1u) != 0u && point - reach >= (float32_t)(window_high[i] - eye[i]);
            const bool past_low =
                ((sign_negative >> i) & 1u) != 0u && point + reach <= (float32_t)(window_low[i] - eye[i]);
            if (past_high || past_low) {
                return TRAVERSE_COARSE_SKY;
            }
        }
        const float32_t scaled = t * TRAVERSE_COARSE_LONG_PER_BLOCK;
        const float32_t longest = scaled > TRAVERSE_COARSE_LONG_STRETCH ? scaled : TRAVERSE_COARSE_LONG_STRETCH;
        if (traverse_coarse_stretch_is_empty(view, eye, frac, axis, chord, t, longest, false)) {
            t += longest;
        } else if (longest > TRAVERSE_COARSE_LONG_STRETCH &&
                   traverse_coarse_stretch_is_empty(view, eye, frac, axis, chord, t, TRAVERSE_COARSE_LONG_STRETCH,
                                                    false)) {
            t += TRAVERSE_COARSE_LONG_STRETCH;
        } else if (traverse_coarse_stretch_is_empty(view, eye, frac, axis, chord, t, TRAVERSE_COARSE_SHORT_STRETCH,
                                                    true)) {
            t += TRAVERSE_COARSE_SHORT_STRETCH;
        } else {
            break;
        }
    }
    return t > TRAVERSE_COARSE_BACK_OFF ? t - TRAVERSE_COARSE_BACK_OFF : 0.0F;
}

__global__ void traverse_coarse_kernel(const traverse_view_t view, const traverse_camera_t camera, float32_t *coarse) {
    /* traverse_tiles_across is host code, being in a header C also reads. */
    const uint32_t tiles_x = (camera.width + TRAVERSE_TILE_PIXELS - 1u) / TRAVERSE_TILE_PIXELS;
    const uint32_t tiles_y = (camera.height + TRAVERSE_TILE_PIXELS - 1u) / TRAVERSE_TILE_PIXELS;
    const uint32_t tile_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t tile_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (tile_x < tiles_x && tile_y < tiles_y) {
        coarse[tile_y * tiles_x + tile_x] = traverse_coarse_tile(&view, &camera, tile_x, tile_y);
    }
}

/* One block is one tile, which keeps the rays of a block coherent: Stage 01.3. coarse is nullptr to start every ray at 0. */
__global__ void traverse_primary_kernel(const traverse_view_t view, const traverse_camera_t camera,
                                        const float32_t *coarse, traverse_hit_t *hits) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera.width || pixel_y >= camera.height) {
        return;
    }
    float32_t direction[3];
    traverse_camera_direction(&camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
    traverse_trace_ray_t ray;
    ray.eye_cell[0] = camera.eye_cell[0];
    ray.eye_cell[1] = camera.eye_cell[1];
    ray.eye_cell[2] = camera.eye_cell[2] - TRAVERSE_Z_BASE;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        ray.eye_frac[i] = camera.eye_frac[i];
        ray.direction[i] = traverse_guard(direction[i]);
    }
    ray.t_start = coarse != nullptr ? coarse[blockIdx.y * gridDim.x + blockIdx.x] : 0.0F;
    traverse_trace(&view, &ray, &hits[pixel_y * camera.width + pixel_x]);
}

__global__ void traverse_rays_kernel(const traverse_view_t view, const traverse_ray_t *rays, traverse_hit_t *hits,
                                     uint32_t ray_count) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= ray_count) {
        return;
    }
    const traverse_ray_t given = rays[index];
    const float32_t scale =
        1.0F / sqrtf(given.direction[0] * given.direction[0] + given.direction[1] * given.direction[1] +
                     given.direction[2] * given.direction[2]);
    traverse_trace_ray_t ray;
    ray.eye_cell[0] = given.eye_cell[0];
    ray.eye_cell[1] = given.eye_cell[1];
    ray.eye_cell[2] = given.eye_cell[2] - TRAVERSE_Z_BASE;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        ray.eye_frac[i] = given.eye_frac[i];
        ray.direction[i] = traverse_guard(given.direction[i] * scale);
    }
    ray.t_start = given.t_start;
    traverse_trace(&view, &ray, &hits[index]);
}

static gpu_status_t traverse_refuse(gpu_error_t *error, const char *what) {
    *error = gpu_error_t{what, nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
    return GPU_STATUS_INVALID_ARGUMENT;
}

static uint32_t traverse_floor_mod(int32_t value, uint32_t modulus) {
    const int64_t remainder = (int64_t)value % (int64_t)modulus;
    return (uint32_t)(remainder < 0 ? remainder + (int64_t)modulus : remainder);
}

/* A column count this wide fits an int16_t offset from any eye inside the window, in traverse_hit_t. */
enum : uint32_t { TRAVERSE_COLUMNS_WIDE_MAX = 2047u };

/* A ray's steps: each moves at least one coordinate forward by at least one, and no coordinate ever moves back, so a ray takes
 * at most width + width + height steps. The cap is that and some room, so it can only trip on a bug, and a test says it never
 * does. */
static gpu_status_t traverse_view_from(gpu_error_t *error, const traverse_world_t *world, traverse_view_t *view) {
    if (world->columns_wide == 0u || world->columns_wide > TRAVERSE_COLUMNS_WIDE_MAX) {
        return traverse_refuse(error, "traverse: columns_wide is 0 or too wide");
    }
    const uint64_t slots = (uint64_t)world->columns_wide * world->columns_wide;
    if (world->borrowed_columns->bytes < slots * sizeof(traverse_column_t)) {
        return traverse_refuse(error, "traverse: the column table is smaller than columns_wide says");
    }
    const uint64_t blocks = (uint64_t)world->columns_wide * TRAVERSE_COLUMN_BLOCKS;
    view->columns = (const traverse_column_t *)device_memory_pointer(world->borrowed_columns);
    view->sections = (const traverse_section_t *)device_memory_pointer(world->borrowed_sections);
    view->brick_masks = (const uint64_t *)device_memory_pointer(world->borrowed_brick_masks);
    view->brick_refs = (const uint32_t *)device_memory_pointer(world->borrowed_brick_refs);
    view->voxels = (const uint32_t *)device_memory_pointer(world->borrowed_voxels);
    for (uint32_t i = 0u; i < 2u; i += 1u) {
        view->window_min_column[i] = world->window_min_column[i];
        view->window_min_block[i] = world->window_min_column[i] * (int32_t)TRAVERSE_COLUMN_BLOCKS;
        view->origin_slot[i] = traverse_floor_mod(world->window_min_column[i], world->columns_wide);
    }
    view->columns_wide = world->columns_wide;
    view->window_blocks = (uint32_t)blocks;
    view->max_steps = (uint32_t)(blocks * 2u) + 2u * TRAVERSE_HEIGHT_BLOCKS + 16u;
    view->_pad0[0] = 0u;
    view->_pad0[1] = 0u;
    view->_pad0[2] = 0u;
    view->_pad0[3] = 0u;
    return GPU_STATUS_OK;
}

static gpu_status_t traverse_camera_check(gpu_error_t *error, const traverse_camera_t *camera) {
    if (camera->width == 0u || camera->height == 0u) {
        return traverse_refuse(error, "traverse: the camera has no pixels");
    }
    return GPU_STATUS_OK;
}

gpu_status_t traverse_coarse(gpu_error_t *error, const traverse_world_t *world, const traverse_camera_t *camera,
                             const traverse_targets_t *targets) {
    traverse_view_t view;
    gpu_status_t status = traverse_view_from(error, world, &view);
    if (status == GPU_STATUS_OK) {
        status = traverse_camera_check(error, camera);
    }
    if (status != GPU_STATUS_OK) {
        return status;
    }
    if (targets->borrowed_coarse->bytes < traverse_coarse_bytes(camera->width, camera->height)) {
        return traverse_refuse(error, "traverse: the coarse target is too small for the camera");
    }
    const uint32_t per_block = TRAVERSE_TILE_PIXELS;
    const dim3 grid(traverse_tiles_across(traverse_tiles_across(camera->width)),
                    traverse_tiles_across(traverse_tiles_across(camera->height)));
    traverse_coarse_kernel<<<grid, dim3(per_block, per_block)>>>(
        view, *camera, (float32_t *)device_memory_pointer(targets->borrowed_coarse));
    return device_memory_check(error, cudaGetLastError(), "traverse: starting the coarse pass");
}

gpu_status_t traverse_primary(gpu_error_t *error, const traverse_world_t *world, const traverse_camera_t *camera,
                              const traverse_targets_t *targets, bool use_coarse) {
    traverse_view_t view;
    gpu_status_t status = traverse_view_from(error, world, &view);
    if (status == GPU_STATUS_OK) {
        status = traverse_camera_check(error, camera);
    }
    if (status != GPU_STATUS_OK) {
        return status;
    }
    if (targets->borrowed_hits->bytes < traverse_hits_bytes(camera->width, camera->height)) {
        return traverse_refuse(error, "traverse: the hit target is too small for the camera");
    }
    if (use_coarse && targets->borrowed_coarse->bytes < traverse_coarse_bytes(camera->width, camera->height)) {
        return traverse_refuse(error, "traverse: the coarse target is too small for the camera");
    }
    const float32_t *const coarse =
        use_coarse ? (const float32_t *)device_memory_pointer(targets->borrowed_coarse) : nullptr;
    const dim3 grid(traverse_tiles_across(camera->width), traverse_tiles_across(camera->height));
    traverse_primary_kernel<<<grid, dim3(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS)>>>(
        view, *camera, coarse, (traverse_hit_t *)device_memory_pointer(targets->borrowed_hits));
    return device_memory_check(error, cudaGetLastError(), "traverse: starting primary rays");
}

gpu_status_t traverse_rays(gpu_error_t *error, const traverse_world_t *world, const device_block_t *rays,
                           const device_block_t *hits, uint32_t ray_count) {
    traverse_view_t view;
    const gpu_status_t status = traverse_view_from(error, world, &view);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    if (ray_count == 0u) {
        return GPU_STATUS_OK;
    }
    if (rays->bytes < (uint64_t)ray_count * sizeof(traverse_ray_t) ||
        hits->bytes < (uint64_t)ray_count * sizeof(traverse_hit_t)) {
        return traverse_refuse(error, "traverse: a ray or hit block is too small for the ray count");
    }
    const uint32_t per_block = 64u;
    traverse_rays_kernel<<<(ray_count + per_block - 1u) / per_block, per_block>>>(
        view, (const traverse_ray_t *)device_memory_pointer(rays), (traverse_hit_t *)device_memory_pointer(hits),
        ray_count);
    return device_memory_check(error, cudaGetLastError(), "traverse: starting rays");
}

gpu_status_t traverse_wait(gpu_error_t *error) {
    return device_memory_check(error, cudaDeviceSynchronize(), "traverse: waiting for kernels");
}
