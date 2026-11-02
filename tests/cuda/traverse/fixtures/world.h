/* A world for ctest cuda_traverse, in a dense array and in the tree, and a camera in it. See ../README.md. Included once, by
 * ../index.c. */
#ifndef MC_TESTS_CUDA_TRAVERSE_FIXTURES_WORLD_H
#define MC_TESTS_CUDA_TRAVERSE_FIXTURES_WORLD_H

#include "harness/index.h"

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/status/index.h"
#include "gpu_bridge/traverse/index.h"
#include "types/index.h"
#include "world/column/index.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The kernels' constants are the world's. */
static_assert((uint32_t)TRAVERSE_COLUMN_BLOCKS == (uint32_t)COLUMN_SIZE);
static_assert((uint32_t)TRAVERSE_COLUMN_SECTIONS == (uint32_t)COLUMN_SECTIONS);
static_assert((int32_t)TRAVERSE_Z_BASE == (int32_t)COLUMN_Z_STORAGE_MIN);
static_assert((int32_t)TRAVERSE_HEIGHT_BLOCKS == (COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN));

enum : uint32_t {
    TW_DENSE_HEIGHT = 512u, /* storage heights above this are air in every world here, and it saves most of the array */
    TW_THREADS = 8u,
    TW_RECORDS = 32u,
    TW_MATERIAL_HASHED = 200u, /* a material whose value differs voxel by voxel, so that bricks hold voxels */
    TW_MATERIAL_STONE = 1u,
    TW_TERRAIN_COLUMNS = 33u,
    TW_MEASURE_COLUMNS = 65u, /* R = 32, as Stage 01.3 */
    TW_PATH_COUNT = 7u,
};
enum : uint64_t { TW_POOL_BYTES = 1ull << 30 }; /* under 4 GiB: the desktop shares this card */

#define TW_PI 3.14159265358979f32

/* ---- A world, in a dense array, and in the tree ---------------------------------------------------------------------- */

typedef struct tw_world_t {
    uint8_t *dense;             /* 0: x, then y, then storage height */
    device_block_t columns;     /* 8 */
    device_block_t sections;    /* 32 */
    device_block_t brick_masks; /* 56 */
    device_block_t brick_refs;  /* 80 */
    device_block_t voxels;      /* 104 */
    traverse_world_t view;      /* 128 */
    uint64_t section_count;     /* 184 */
    uint64_t brick_count;       /* 192 */
    uint64_t voxel_count;       /* 200 */
    uint64_t uniform_sections;  /* 208 */
    uint64_t uniform_bricks;    /* 216 */
    int32_t min_block[2];       /* 224 */
    uint32_t columns_wide;      /* 232 */
    uint32_t width;             /* 236 */
} tw_world_t;
static_assert(sizeof(tw_world_t) == 240);

typedef struct tw_encode_t {
    traverse_column_t *columns;   /* 0 */
    traverse_section_t *sections; /* 8 */
    uint64_t *masks;              /* 16 */
    uint32_t *refs;               /* 24 */
    uint32_t *voxels;             /* 32 */
} tw_encode_t;
static_assert(sizeof(tw_encode_t) == 40);

static uint32_t tw_hash(int32_t x, int32_t y, int32_t z) {
    uint32_t h = (uint32_t)x * 0x8DA6B343u ^ (uint32_t)y * 0xD8163841u ^ (uint32_t)z * 0xCB1AB31Fu;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return h;
}

static inline uint64_t tw_index(const tw_world_t *world, uint32_t x, uint32_t y, uint32_t z) {
    return ((uint64_t)z * world->width + y) * world->width + x;
}

/* The value of a voxel by dense coordinates. */
static uint32_t tw_value(const tw_world_t *world, uint32_t x, uint32_t y, uint32_t z) {
    if (z >= TW_DENSE_HEIGHT) {
        return 0u;
    }
    const uint32_t material = world->dense[tw_index(world, x, y, z)];
    if (material == TW_MATERIAL_HASHED) {
        return material | (tw_hash((int32_t)x, (int32_t)y, (int32_t)z) & 0xFFFFu) << 8;
    }
    return material;
}

static tw_world_t *tw_world_begin(test_context_t *t, uint32_t columns_wide, int32_t min_column_x,
                                  int32_t min_column_y) {
    tw_world_t *const world = test_alloc(t, sizeof(tw_world_t));
    memset(world, 0, sizeof(*world));
    world->columns_wide = columns_wide;
    world->width = columns_wide * TRAVERSE_COLUMN_BLOCKS;
    world->min_block[0] = min_column_x * (int32_t)TRAVERSE_COLUMN_BLOCKS;
    world->min_block[1] = min_column_y * (int32_t)TRAVERSE_COLUMN_BLOCKS;
    world->dense = test_alloc(t, (uint64_t)world->width * world->width * TW_DENSE_HEIGHT);
    world->view = (traverse_world_t){.borrowed_columns = &world->columns,
                                     .borrowed_sections = &world->sections,
                                     .borrowed_brick_masks = &world->brick_masks,
                                     .borrowed_brick_refs = &world->brick_refs,
                                     .borrowed_voxels = &world->voxels,
                                     .window_min_column = {min_column_x, min_column_y},
                                     .columns_wide = columns_wide};
    return world;
}

/* By world coordinates, as a test thinks of them. */
static void tw_set(tw_world_t *world, int32_t x, int32_t y, int32_t z, uint32_t material) {
    const uint32_t dx = (uint32_t)(x - world->min_block[0]), dy = (uint32_t)(y - world->min_block[1]);
    const uint32_t dz = (uint32_t)(z - TRAVERSE_Z_BASE);
    if (dx >= world->width || dy >= world->width || dz >= TW_DENSE_HEIGHT) {
        fprintf(stderr, "tw_set outside the world: %d %d %d\n", x, y, z);
        abort();
    }
    world->dense[tw_index(world, dx, dy, dz)] = (uint8_t)material;
}

static uint32_t tw_floor_mod(int32_t value, uint32_t modulus) {
    const int64_t remainder = (int64_t)value % (int64_t)modulus;
    return (uint32_t)(remainder < 0 ? remainder + (int64_t)modulus : remainder);
}

/* Writes the tree, or with out nullptr only counts it. Written from the dense array alone, as Stage 01.3's encoder was, and it
 * is not the game's: this test says what the format means without taking the encoder's word for it. */
static void tw_encode(tw_world_t *world, const tw_encode_t *out) {
    uint64_t sections = 0u, bricks = 0u, voxels = 0u, uniform_sections = 0u, uniform_bricks = 0u;
    const uint32_t wide = world->columns_wide;
    for (uint32_t column_y = 0u; column_y < wide; column_y += 1u) {
        for (uint32_t column_x = 0u; column_x < wide; column_x += 1u) {
            traverse_column_t column = {.first_section = (uint32_t)sections};
            for (uint32_t section_z = 0u; section_z < TW_DENSE_HEIGHT / 16u; section_z += 1u) {
                const uint32_t x0 = column_x * 16u, y0 = column_y * 16u, z0 = section_z * 16u;
                const uint32_t first = tw_value(world, x0, y0, z0);
                uint32_t occupied = 0u, top = 0u;
                bool uniform = true;
                for (uint32_t i = 0u; i < 4096u; i += 1u) {
                    const uint32_t value = tw_value(world, x0 + (i & 15u), y0 + ((i >> 4) & 15u), z0 + (i >> 8));
                    occupied += value != 0u;
                    uniform = uniform && value == first;
                    top = value != 0u && z0 + (i >> 8) + 1u > top ? z0 + (i >> 8) + 1u : top;
                }
                if (occupied == 0u) {
                    continue;
                }
                column.section_mask |= 1ull << section_z;
                column.top = (uint16_t)top;
                traverse_section_t section = {0};
                if (uniform) {
                    section.brick_mask = ~0ull;
                    section.first_brick = TRAVERSE_SECTION_UNIFORM;
                    section.uniform_value = first;
                    uniform_sections += 1u;
                } else {
                    section.first_brick = (uint32_t)bricks;
                    for (uint32_t b = 0u; b < 64u; b += 1u) {
                        const uint32_t bx = x0 + (b & 3u) * 4u, by = y0 + ((b >> 2) & 3u) * 4u, bz = z0 + (b >> 4) * 4u;
                        uint64_t mask = 0u;
                        uint32_t count = 0u, same_as = 0u;
                        bool same = true;
                        for (uint32_t v = 0u; v < 64u; v += 1u) {
                            const uint32_t value = tw_value(world, bx + (v & 3u), by + ((v >> 2) & 3u), bz + (v >> 4));
                            if (value != 0u) {
                                same_as = count == 0u ? value : same_as;
                                same = same && value == same_as;
                                mask |= 1ull << v;
                                if (out != nullptr) {
                                    out->voxels[voxels + count] = value; /* wasted if this brick turns out uniform */
                                }
                                count += 1u;
                            }
                        }
                        if (mask == 0u) {
                            continue;
                        }
                        section.brick_mask |= 1ull << b;
                        if (out != nullptr) {
                            out->masks[bricks] = mask;
                            out->refs[bricks] = same ? (TRAVERSE_BRICK_UNIFORM | same_as) : (uint32_t)voxels;
                        }
                        if (same) {
                            uniform_bricks += 1u;
                        } else {
                            voxels += count;
                        }
                        bricks += 1u;
                    }
                }
                if (out != nullptr) {
                    out->sections[sections] = section;
                }
                sections += 1u;
            }
            if (out != nullptr) {
                const int32_t world_x = world->min_block[0] / 16 + (int32_t)column_x;
                const int32_t world_y = world->min_block[1] / 16 + (int32_t)column_y;
                const uint32_t slot = tw_floor_mod(world_y, wide) * wide + tw_floor_mod(world_x, wide);
                out->columns[slot] = column;
            }
        }
    }
    world->section_count = sections;
    world->brick_count = bricks;
    world->voxel_count = voxels;
    world->uniform_sections = uniform_sections;
    world->uniform_bricks = uniform_bricks;
}

static bool tw_block_up(test_context_t *t, device_memory_t *memory, device_block_t *block, const void *source,
                        uint64_t bytes, const char *name) {
    const uint64_t size = bytes > 64u ? bytes : 64u; /* an empty pool still has an address */
    if (!CHECK_EQ_U64(device_memory_alloc(memory, DEVICE_POOL_VOXELS, size, name, block), GPU_STATUS_OK)) {
        gpu_error_log(&memory->last_error);
        return false;
    }
    if (bytes != 0u) {
        return CHECK_EQ_U64(device_memory_write(memory, block, 0u, source, bytes), GPU_STATUS_OK);
    }
    return true;
}

static bool tw_world_upload(test_context_t *t, device_memory_t *memory, tw_world_t *world) {
    tw_encode(world, nullptr);
    /* The first pass told us the sizes. The second may write up to 64 voxels past them, before a brick turns out uniform. */
    const uint64_t slots = (uint64_t)world->columns_wide * world->columns_wide;
    const tw_encode_t out = {
        .columns = test_alloc(t, slots * sizeof(traverse_column_t)),
        .sections = test_alloc(t, world->section_count * sizeof(traverse_section_t)),
        .masks = test_alloc(t, world->brick_count * sizeof(uint64_t)),
        .refs = test_alloc(t, world->brick_count * sizeof(uint32_t)),
        .voxels = test_alloc(t, (world->voxel_count + 64u) * sizeof(uint32_t)),
    };
    memset(out.columns, 0, slots * sizeof(traverse_column_t));
    tw_encode(world, &out);
    return tw_block_up(t, memory, &world->columns, out.columns, slots * sizeof(traverse_column_t), "tw columns") &&
           tw_block_up(t, memory, &world->sections, out.sections, world->section_count * sizeof(traverse_section_t),
                       "tw sections") &&
           tw_block_up(t, memory, &world->brick_masks, out.masks, world->brick_count * sizeof(uint64_t), "tw masks") &&
           tw_block_up(t, memory, &world->brick_refs, out.refs, world->brick_count * sizeof(uint32_t), "tw refs") &&
           tw_block_up(t, memory, &world->voxels, out.voxels, world->voxel_count * sizeof(uint32_t), "tw voxels");
}

static bool tw_open(test_context_t *t, device_memory_t *memory) {
    device_memory_record_t *const records = test_alloc(t, TW_RECORDS * sizeof(device_memory_record_t));
    const device_memory_limits_t limits = {.pool_bytes = {TW_POOL_BYTES, TW_POOL_BYTES, TW_POOL_BYTES}};
    memset(memory, 0, sizeof(*memory));
    const gpu_status_t status = device_memory_init(memory, limits, records, TW_RECORDS);
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&memory->last_error);
    }
    return CHECK_EQ_U64(status, GPU_STATUS_OK);
}

static void tw_close(test_context_t *t, device_memory_t *memory) {
    CHECK_EQ_U64(device_memory_deinit(memory), GPU_STATUS_OK);
}

/* ---- Cameras --------------------------------------------------------------------------------------------------------- */

/* The eye as dense coordinates, yaw and pitch. half_fov_tangent is the tangent of half the vertical field of view. */
static traverse_camera_t tw_camera(const tw_world_t *world, const float32_t eye[3], float32_t yaw, float32_t pitch,
                                   float32_t half_fov_tangent, uint32_t width, uint32_t height) {
    const float32_t cos_pitch = cosf(pitch), sin_pitch = sinf(pitch), cos_yaw = cosf(yaw), sin_yaw = sinf(yaw);
    const float32_t forward[3] = {cos_pitch * cos_yaw, cos_pitch * sin_yaw, sin_pitch};
    /* x east, y north, z up, right-handed: right = forward x up. */
    const float32_t right[3] = {sin_yaw, -cos_yaw, 0.0f32};
    const float32_t up[3] = {right[1] * forward[2] - right[2] * forward[1],
                             right[2] * forward[0] - right[0] * forward[2],
                             right[0] * forward[1] - right[1] * forward[0]};
    const float32_t across = half_fov_tangent * (float32_t)width / (float32_t)height;
    traverse_camera_t camera = {.width = width, .height = height};
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float32_t whole = floorf(eye[i]);
        camera.eye_frac[i] = eye[i] - whole;
        camera.forward[i] = forward[i];
        camera.right[i] = right[i] * across;
        camera.up[i] = up[i] * half_fov_tangent;
        camera.eye_cell[i] = (int32_t)whole;
    }
    camera.eye_cell[0] += world->min_block[0];
    camera.eye_cell[1] += world->min_block[1];
    camera.eye_cell[2] += TRAVERSE_Z_BASE;
    return camera;
}

#endif
