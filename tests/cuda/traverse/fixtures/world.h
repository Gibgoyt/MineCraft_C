/* A world for ctest cuda_traverse, in a dense array and in the tree, and a camera in it. See ../README.md. Included once, by
 * ../index.c. */
#ifndef MC_TESTS_CUDA_TRAVERSE_FIXTURES_WORLD_H
#define MC_TESTS_CUDA_TRAVERSE_FIXTURES_WORLD_H

#include "harness/index.h"

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/status/index.h"
#include "gpu_bridge/traverse/index.h"
#include "heap/index.h"
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
    uint8_t *dense;            /* 0: x, then y, then storage height */
    device_block_t table;      /* 8 */
    device_block_t pool;       /* 32 */
    traverse_world_t view;     /* 56 */
    uint64_t pool_bytes;       /* 96 */
    uint64_t section_count;    /* 104 */
    uint64_t brick_count;      /* 112 */
    uint64_t voxel_count;      /* 120: in sections that keep their voxels */
    uint64_t uniform_sections; /* 128 */
    int32_t min_block[2];      /* 136 */
    uint32_t columns_wide;     /* 144 */
    uint32_t width;            /* 148 */
} tw_world_t;
static_assert(sizeof(tw_world_t) == 152);

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
        /* A material and a light of its own, each of which has to arrive: see traverse_hit_t.value. */
        return material | (tw_hash((int32_t)x, (int32_t)y, (int32_t)z) & 0xFFFFu) << 8;
    }
    return material;
}

static uint32_t tw_floor_mod(int32_t value, uint32_t modulus) {
    const int64_t remainder = (int64_t)value % (int64_t)modulus;
    return (uint32_t)(remainder < 0 ? remainder + (int64_t)modulus : remainder);
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
    world->view = (traverse_world_t){.borrowed_table = &world->table,
                                     .borrowed_pool = &world->pool,
                                     .window = {.min_x = min_column_x,
                                                .min_y = min_column_y,
                                                .min_slot_x = tw_floor_mod(min_column_x, columns_wide),
                                                .min_slot_y = tw_floor_mod(min_column_y, columns_wide),
                                                .side = columns_wide,
                                                .slot_count = columns_wide * columns_wide}};
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

/* One section, in the form gpu_bridge/world_buffers/index.h defines, at out, or with out nullptr only counted. Returns its
 * bytes. Written from the dense array alone, as Stage 01.3's encoder was, and it is not the game's: this test says what the
 * format means without taking the encoder's word for it. x0, y0 and z0 are dense coordinates of its low corner. */
static uint32_t tw_encode_section(tw_world_t *world, uint32_t x0, uint32_t y0, uint32_t z0, uint8_t *out) {
    const uint32_t first_value = tw_value(world, x0, y0, z0);
    uint64_t words[64];
    uint64_t brick_mask = 0u;
    uint32_t bricks = 0u, voxels = 0u;
    bool uniform = true;
    for (uint32_t b = 0u; b < 64u; b += 1u) {
        const uint32_t bx = x0 + (b & 3u) * 4u, by = y0 + ((b >> 2) & 3u) * 4u, bz = z0 + (b >> 4) * 4u;
        uint64_t mask = 0u;
        for (uint32_t v = 0u; v < 64u; v += 1u) {
            const uint32_t value = tw_value(world, bx + (v & 3u), by + ((v >> 2) & 3u), bz + (v >> 4));
            uniform = uniform && value == first_value;
            if (value != 0u) {
                mask |= 1ull << v;
                voxels += 1u;
            }
        }
        if (mask != 0u) {
            brick_mask |= 1ull << b;
            words[bricks] = mask;
            bricks += 1u;
        }
    }
    if (voxels == 0u) {
        return 0u;
    }
    if (uniform) {
        world->uniform_sections += 1u;
        if (out != nullptr) {
            const world_buffers_section_t header = {.brick_mask = ~0ull,
                                                    .voxel_count = 4096u,
                                                    .material_offset = 16u,
                                                    .light_offset = 18u,
                                                    .brick_count = 0u};
            const uint16_t material = (uint16_t)(first_value & TRAVERSE_VALUE_MATERIAL_MASK);
            memcpy(out, &header, sizeof(header));
            memcpy(out + 16, &material, 2u);
            out[18] = (uint8_t)(first_value >> TRAVERSE_VALUE_LIGHT_SHIFT);
        }
        return WORLD_BUFFERS_SECTION_BYTES_UNIFORM;
    }
    world->brick_count += bricks;
    world->voxel_count += voxels;
    const uint32_t first_offset = 16u + bricks * 8u;
    const uint32_t material_offset = first_offset + bricks * 2u;
    const uint32_t light_offset = material_offset + voxels * 2u;
    const uint32_t bytes = (light_offset + voxels + 7u) & ~7u;
    if (out == nullptr) {
        return bytes;
    }
    const world_buffers_section_t header = {.brick_mask = brick_mask,
                                            .voxel_count = (uint16_t)voxels,
                                            .material_offset = (uint16_t)material_offset,
                                            .light_offset = (uint16_t)light_offset,
                                            .brick_count = (uint8_t)bricks};
    memcpy(out, &header, sizeof(header));
    memcpy(out + 16, words, bricks * 8u);
    uint32_t rank = 0u, place = 0u;
    for (uint32_t b = 0u; b < 64u; b += 1u) {
        if (((brick_mask >> b) & 1u) == 0u) {
            continue;
        }
        const uint16_t first = (uint16_t)place;
        memcpy(out + first_offset + rank * 2u, &first, 2u);
        const uint32_t bx = x0 + (b & 3u) * 4u, by = y0 + ((b >> 2) & 3u) * 4u, bz = z0 + (b >> 4) * 4u;
        for (uint32_t v = 0u; v < 64u; v += 1u) {
            const uint32_t value = tw_value(world, bx + (v & 3u), by + ((v >> 2) & 3u), bz + (v >> 4));
            if (value != 0u) {
                const uint16_t material = (uint16_t)(value & TRAVERSE_VALUE_MATERIAL_MASK);
                memcpy(out + material_offset + place * 2u, &material, 2u);
                out[light_offset + place] = (uint8_t)(value >> TRAVERSE_VALUE_LIGHT_SHIFT);
                place += 1u;
            }
        }
        rank += 1u;
    }
    return bytes;
}

/* The table and the pool, or with nullptrs only counted. Sections go where a slab would, on a whole granule. */
static void tw_encode(tw_world_t *world, world_buffers_column_t *table, uint8_t *pool) {
    world->section_count = 0u;
    world->brick_count = 0u;
    world->voxel_count = 0u;
    world->uniform_sections = 0u;
    uint64_t granule = 0u;
    const uint32_t wide = world->columns_wide;
    for (uint32_t column_y = 0u; column_y < wide; column_y += 1u) {
        for (uint32_t column_x = 0u; column_x < wide; column_x += 1u) {
            const int32_t world_x = world->min_block[0] / 16 + (int32_t)column_x;
            const int32_t world_y = world->min_block[1] / 16 + (int32_t)column_y;
            world_buffers_column_t column = {.chunk_x = world_x,
                                             .chunk_y = world_y,
                                             .generation = 1u,
                                             .top_z = (int16_t)(TRAVERSE_Z_BASE - 1),
                                             .ready = 1u};
            for (uint32_t section_z = 0u; section_z < TW_DENSE_HEIGHT / 16u; section_z += 1u) {
                const uint32_t x0 = column_x * 16u, y0 = column_y * 16u, z0 = section_z * 16u;
                uint8_t *const out = pool != nullptr ? pool + granule * WORLD_BUFFERS_GRANULE_BYTES : nullptr;
                const uint32_t bytes = tw_encode_section(world, x0, y0, z0, out);
                if (bytes == 0u) {
                    continue;
                }
                column.section_mask |= 1ull << section_z;
                column.sections[section_z] = (uint32_t)granule + 1u;
                granule += (bytes + WORLD_BUFFERS_GRANULE_BYTES - 1u) / WORLD_BUFFERS_GRANULE_BYTES;
                world->section_count += 1u;
                for (uint32_t i = 0u; i < 4096u; i += 1u) {
                    if (tw_value(world, x0 + (i & 15u), y0 + ((i >> 4) & 15u), z0 + (i >> 8)) != 0u) {
                        const int32_t height = (int32_t)(z0 + (i >> 8)) + TRAVERSE_Z_BASE;
                        column.top_z = height > column.top_z ? (int16_t)height : column.top_z;
                    }
                }
            }
            if (table != nullptr) {
                table[tw_floor_mod(world_y, wide) * wide + tw_floor_mod(world_x, wide)] = column;
            }
        }
    }
    world->pool_bytes = granule * WORLD_BUFFERS_GRANULE_BYTES;
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
    tw_encode(world, nullptr, nullptr);
    const uint64_t slots = (uint64_t)world->columns_wide * world->columns_wide;
    world_buffers_column_t *const table = test_alloc(t, slots * sizeof(world_buffers_column_t));
    memset(table, 0, slots * sizeof(world_buffers_column_t));
    /* From the heap and not the test arena, and given back: at R = 32 this alone is 618 MiB, which with the dense world would
     * not fit in the arena's 1 GiB. */
    heap_region_t region;
    if (!CHECK(heap_reserve(world->pool_bytes + 64u, &region))) {
        return false;
    }
    uint8_t *const pool = region.owned_base;
    memset(pool, 0, world->pool_bytes + 64u);
    tw_encode(world, table, pool);
    const bool sent =
        tw_block_up(t, memory, &world->table, table, slots * sizeof(world_buffers_column_t), "tw table") &&
        tw_block_up(t, memory, &world->pool, pool, world->pool_bytes, "tw pool");
    heap_release(&region);
    return sent;
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
