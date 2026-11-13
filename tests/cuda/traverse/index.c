/* ctest cuda_traverse. See README.md. */
#include "harness/index.h"

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/status/index.h"
#include "gpu_bridge/traverse/index.h"
#include "time/index.h"
#include "types/index.h"

#include "cuda/traverse/fixtures/stress_world.h"
#include "cuda/traverse/fixtures/world.h"

#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- One ray at a time, for what can be worked out by hand ------------------------------------------------------------ */

static traverse_ray_t tw_ray(int32_t x, int32_t y, int32_t z, float32_t direction_x, float32_t direction_y,
                             float32_t direction_z) {
    return (traverse_ray_t){.eye_cell = {x, y, z},
                            .eye_frac = {0.5f32, 0.5f32, 0.5f32},
                            .direction = {direction_x, direction_y, direction_z}};
}

static bool tw_cast(test_context_t *t, device_memory_t *memory, const tw_world_t *world, const traverse_ray_t *rays,
                    uint32_t ray_count, traverse_hit_t *hits) {
    device_block_t ray_block = {0}, hit_block = {0};
    const uint64_t ray_bytes = (uint64_t)ray_count * sizeof(traverse_ray_t);
    const uint64_t hit_bytes = (uint64_t)ray_count * sizeof(traverse_hit_t);
    bool ok =
        CHECK_EQ_U64(device_memory_alloc(memory, DEVICE_POOL_FRAMES, ray_bytes, "tw rays", &ray_block), GPU_STATUS_OK);
    ok = ok &&
         CHECK_EQ_U64(device_memory_alloc(memory, DEVICE_POOL_FRAMES, hit_bytes, "tw hits", &hit_block), GPU_STATUS_OK);
    ok = ok && CHECK_EQ_U64(device_memory_write(memory, &ray_block, 0u, rays, ray_bytes), GPU_STATUS_OK);
    gpu_error_t error = {0};
    gpu_status_t status = GPU_STATUS_OK;
    if (ok) {
        status = traverse_rays(&error, &world->view, &ray_block, &hit_block, ray_count);
        status = status == GPU_STATUS_OK ? traverse_wait(&error) : status;
        if (status != GPU_STATUS_OK) {
            gpu_error_log(&error);
        }
        ok = CHECK_EQ_U64(status, GPU_STATUS_OK);
    }
    ok = ok && CHECK_EQ_U64(device_memory_read(memory, &hit_block, 0u, hits, hit_bytes), GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_free(memory, &ray_block), GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_free(memory, &hit_block), GPU_STATUS_OK);
    return ok;
}

static void tw_check_hit(test_context_t *t, const traverse_hit_t *hit, uint32_t face, float32_t expected_t, int32_t dx,
                         int32_t dy, int32_t dz, uint32_t value) {
    CHECK_EQ_U64(traverse_hit_face(hit->info), face);
    CHECK_NEAR(hit->t, expected_t, 1e-3f32);
    CHECK_EQ_I64(hit->offset[0], dx);
    CHECK_EQ_I64(hit->offset[1], dy);
    CHECK_EQ_I64(hit->offset[2], dz);
    CHECK_EQ_U64(hit->value, value);
    CHECK_EQ_U64((uint64_t)hit->info & TRAVERSE_INFO_CAPPED, 0u);
}

/* ---- Worked by hand -------------------------------------------------------------------------------------------------- */

TEST(each_face_is_named_with_its_distance) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 4u, 0, 0);
    tw_set(world, 24, 24, 40, 7u);
    REQUIRE(tw_world_upload(t, &memory, world));
    /* Each eye is at the middle of its cell, 12 cells from the voxel, so the near face is 11.5 away. */
    const traverse_ray_t rays[6] = {
        tw_ray(12, 24, 40, 1.0f32, 0.0f32, 0.0f32), tw_ray(36, 24, 40, -1.0f32, 0.0f32, 0.0f32),
        tw_ray(24, 12, 40, 0.0f32, 1.0f32, 0.0f32), tw_ray(24, 36, 40, 0.0f32, -1.0f32, 0.0f32),
        tw_ray(24, 24, 28, 0.0f32, 0.0f32, 1.0f32), tw_ray(24, 24, 52, 0.0f32, 0.0f32, -1.0f32),
    };
    traverse_hit_t hits[6];
    REQUIRE(tw_cast(t, &memory, world, rays, 6u, hits));
    tw_check_hit(t, &hits[0], TRAVERSE_FACE_NEG_X, 11.5f32, 12, 0, 0, 7u);
    tw_check_hit(t, &hits[1], TRAVERSE_FACE_POS_X, 11.5f32, -12, 0, 0, 7u);
    tw_check_hit(t, &hits[2], TRAVERSE_FACE_NEG_Y, 11.5f32, 0, 12, 0, 7u);
    tw_check_hit(t, &hits[3], TRAVERSE_FACE_POS_Y, 11.5f32, 0, -12, 0, 7u);
    tw_check_hit(t, &hits[4], TRAVERSE_FACE_NEG_Z, 11.5f32, 0, 0, 12, 7u);
    tw_check_hit(t, &hits[5], TRAVERSE_FACE_POS_Z, 11.5f32, 0, 0, -12, 7u);
    tw_close(t, &memory);
}

/* A column's mask takes two words, and the top of the world is in the second. */
TEST(blocks_in_either_word_of_a_columns_mask_are_hit_up_to_the_top_of_the_world) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 4u, 0, 0);
    /* z = 767 is the last of section 63, 768 the first of section 64, and 1250 the top of the world, in section 94. */
    tw_set(world, 8, 8, 767, 7u);
    tw_set(world, 24, 8, 768, 8u);
    tw_set(world, 40, 8, 1250, 9u);
    tw_set(world, 56, 8, 1250, 10u);
    REQUIRE(tw_world_upload(t, &memory, world));
    const traverse_ray_t rays[6] = {
        tw_ray(8, 8, 755, 0.0f32, 0.0f32, 1.0f32),    tw_ray(24, 8, 780, 0.0f32, 0.0f32, -1.0f32),
        tw_ray(40, 8, 1238, 0.0f32, 0.0f32, 1.0f32),  tw_ray(40, 8, 1262, 0.0f32, 0.0f32, -1.0f32),
        tw_ray(44, 8, 1250, -1.0f32, 0.0f32, 0.0f32), tw_ray(56, 8, 100, 0.0f32, 0.0f32, 1.0f32),
    };
    traverse_hit_t hits[6];
    REQUIRE(tw_cast(t, &memory, world, rays, 6u, hits));
    tw_check_hit(t, &hits[0], TRAVERSE_FACE_NEG_Z, 11.5f32, 0, 0, 12, 7u);
    tw_check_hit(t, &hits[1], TRAVERSE_FACE_POS_Z, 11.5f32, 0, 0, -12, 8u);
    tw_check_hit(t, &hits[2], TRAVERSE_FACE_NEG_Z, 11.5f32, 0, 0, 12, 9u);
    tw_check_hit(t, &hits[3], TRAVERSE_FACE_POS_Z, 11.5f32, 0, 0, -12, 9u);
    tw_check_hit(t, &hits[4], TRAVERSE_FACE_POS_X, 3.5f32, -4, 0, 0, 9u);
    tw_check_hit(t, &hits[5], TRAVERSE_FACE_NEG_Z, 1149.5f32, 0, 0, 1150, 10u);
    tw_close(t, &memory);
}

TEST(a_slanted_ray_arrives_where_the_arithmetic_says) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 4u, 0, 0);
    tw_set(world, 30, 22, 40, 7u);
    REQUIRE(tw_world_upload(t, &memory, world));
    /* From (10.5, 12.5, 40.5) along (2, 1, 0): at x = 30 it is at y = 22.25, so it enters by the -x face, in voxel (30, 22).
     * Blocks along it: 19.5 in x, 9.75 in y: sqrt(19.5^2 + 9.75^2) = 21.8020. */
    const traverse_ray_t ray = tw_ray(10, 12, 40, 2.0f32, 1.0f32, 0.0f32);
    traverse_hit_t hit;
    REQUIRE(tw_cast(t, &memory, world, &ray, 1u, &hit));
    tw_check_hit(t, &hit, TRAVERSE_FACE_NEG_X, 21.8020f32, 20, 10, 0, 7u);
    tw_close(t, &memory);
}

TEST(values_come_from_every_kind_of_node) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 4u, 0, 0);
    /* Column 0, section 0 is one value throughout. */
    for (int32_t z = 0; z < 16; z += 1) {
        for (int32_t y = 0; y < 16; y += 1) {
            for (int32_t x = 0; x < 16; x += 1) {
                tw_set(world, x, y, TRAVERSE_Z_BASE + z, 9u);
            }
        }
    }
    /* Column 1: one brick of one value, at bricks (1, 1, 1) of its section at storage 16..31. */
    for (int32_t z = 0; z < 4; z += 1) {
        for (int32_t y = 0; y < 4; y += 1) {
            for (int32_t x = 0; x < 4; x += 1) {
                tw_set(world, 16 + 4 + x, 4 + y, TRAVERSE_Z_BASE + 16 + 4 + z, 5u);
            }
        }
    }
    /* Column 2: one brick with three voxels at bits 0, 21 and 63, so finding the second and third needs the popcount. */
    tw_set(world, 32, 0, TRAVERSE_Z_BASE, 11u);
    tw_set(world, 33, 1, TRAVERSE_Z_BASE + 1, 12u);
    tw_set(world, 35, 3, TRAVERSE_Z_BASE + 3, 13u);
    REQUIRE(tw_world_upload(t, &memory, world));
    CHECK_EQ_U64(world->uniform_sections, 1u);
    CHECK_EQ_U64(world->section_count, 3u);
    CHECK_EQ_U64(world->brick_count, 2u);
    CHECK_EQ_U64(world->voxel_count, 67u);
    const int32_t above = TRAVERSE_Z_BASE + 200;
    const traverse_ray_t rays[5] = {
        tw_ray(5, 5, above, 0.0f32, 0.0f32, -1.0f32),  tw_ray(22, 6, above, 0.0f32, 0.0f32, -1.0f32),
        tw_ray(32, 0, above, 0.0f32, 0.0f32, -1.0f32), tw_ray(33, 1, above, 0.0f32, 0.0f32, -1.0f32),
        tw_ray(35, 3, above, 0.0f32, 0.0f32, -1.0f32),
    };
    traverse_hit_t hits[5];
    REQUIRE(tw_cast(t, &memory, world, rays, 5u, hits));
    /* From storage 200.5 down to the top of the voxel: 16 for the first, 24 for the brick, 1, 2 and 4 for the three voxels. */
    tw_check_hit(t, &hits[0], TRAVERSE_FACE_POS_Z, 184.5f32, 0, 0, -185, 9u);
    tw_check_hit(t, &hits[1], TRAVERSE_FACE_POS_Z, 176.5f32, 0, 0, -177, 5u);
    tw_check_hit(t, &hits[2], TRAVERSE_FACE_POS_Z, 199.5f32, 0, 0, -200, 11u);
    tw_check_hit(t, &hits[3], TRAVERSE_FACE_POS_Z, 198.5f32, 0, 0, -199, 12u);
    tw_check_hit(t, &hits[4], TRAVERSE_FACE_POS_Z, 196.5f32, 0, 0, -197, 13u);
    tw_close(t, &memory);
}

TEST(the_window_may_lie_below_zero_and_its_table_wraps) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    /* Columns -3 to 0, in a table of 4: -3 mod 4 is 1, so column -3 is in slot 1 and column 0 has wrapped to slot 0. */
    tw_world_t *const world = tw_world_begin(t, 4u, -3, -3);
    tw_set(world, -41, -41, 40, 3u); /* column -3, -3 */
    tw_set(world, 5, 5, 40, 4u);     /* column 0, 0 */
    REQUIRE(tw_world_upload(t, &memory, world));
    const traverse_ray_t rays[3] = {
        tw_ray(-41, -41, 60, 0.0f32, 0.0f32, -1.0f32),
        tw_ray(5, 5, 60, 0.0f32, 0.0f32, -1.0f32),
        /* Straight along a row, from one end of the window to the other, through the seam of the table. */
        tw_ray(-48, 5, 40, 1.0f32, 0.0f32, 0.0f32),
    };
    traverse_hit_t hits[3];
    REQUIRE(tw_cast(t, &memory, world, rays, 3u, hits));
    tw_check_hit(t, &hits[0], TRAVERSE_FACE_POS_Z, 19.5f32, 0, 0, -20, 3u);
    tw_check_hit(t, &hits[1], TRAVERSE_FACE_POS_Z, 19.5f32, 0, 0, -20, 4u);
    tw_check_hit(t, &hits[2], TRAVERSE_FACE_NEG_X, 52.5f32, 53, 0, 0, 4u);
    tw_close(t, &memory);
}

/* A slot still holding the column that wrapped out of this place is not looked at, and shows nothing, until its new one arrives. */
TEST(a_column_that_wrapped_out_of_its_slot_is_not_seen) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 4u, 0, 0);
    tw_set(world, 5, 5, 40, 4u); /* column 0, 0, in slot 0 */
    REQUIRE(tw_world_upload(t, &memory, world));
    const traverse_ray_t ray = tw_ray(5, 5, 60, 0.0f32, 0.0f32, -1.0f32);
    traverse_hit_t hit;
    REQUIRE(tw_cast(t, &memory, world, &ray, 1u, &hit));
    tw_check_hit(t, &hit, TRAVERSE_FACE_POS_Z, 19.5f32, 0, 0, -20, 4u);
    const int32_t elsewhere = 4;
    const int32_t here = 0;
    REQUIRE(device_memory_write(&memory, &world->table, offsetof(world_buffers_column_t, chunk_x), &elsewhere,
                                sizeof(elsewhere)) == GPU_STATUS_OK);
    REQUIRE(tw_cast(t, &memory, world, &ray, 1u, &hit));
    CHECK_EQ_U64(traverse_hit_face(hit.info), TRAVERSE_FACE_MISS);
    REQUIRE(device_memory_write(&memory, &world->table, offsetof(world_buffers_column_t, chunk_x), &here,
                                sizeof(here)) == GPU_STATUS_OK);
    REQUIRE(device_memory_write(&memory, &world->table, offsetof(world_buffers_column_t, chunk_y), &elsewhere,
                                sizeof(elsewhere)) == GPU_STATUS_OK);
    REQUIRE(tw_cast(t, &memory, world, &ray, 1u, &hit));
    CHECK_EQ_U64(traverse_hit_face(hit.info), TRAVERSE_FACE_MISS);
    tw_close(t, &memory);
}

TEST(a_ray_from_outside_the_window_enters_it) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 4u, 0, 0);
    tw_set(world, 0, 20, 40, 8u);
    tw_set(world, 20, 20, TRAVERSE_Z_BASE + 511, 6u);
    REQUIRE(tw_world_upload(t, &memory, world));
    const traverse_ray_t rays[2] = {
        tw_ray(-30, 20, 40, 1.0f32, 0.0f32, 0.0f32),  /* from the west: x = 0 is 29.5 away */
        tw_ray(20, 20, 600, 0.0f32, 0.0f32, -1.0f32), /* from above the top of the world */
    };
    traverse_hit_t hits[2];
    REQUIRE(tw_cast(t, &memory, world, rays, 2u, hits));
    tw_check_hit(t, &hits[0], TRAVERSE_FACE_NEG_X, 29.5f32, 30, 0, 0, 8u);
    /* Storage top face at 512, which is world z 256; the eye is at 600.5. */
    tw_check_hit(t, &hits[1], TRAVERSE_FACE_POS_Z, 344.5f32, 0, 0, TRAVERSE_Z_BASE + 511 - 600, 6u);
    tw_close(t, &memory);
}

TEST(an_eye_inside_a_voxel_says_so) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 4u, 0, 0);
    tw_set(world, 20, 20, 40, 8u);
    REQUIRE(tw_world_upload(t, &memory, world));
    const traverse_ray_t ray = tw_ray(20, 20, 40, 1.0f32, 2.0f32, 3.0f32);
    traverse_hit_t hit;
    REQUIRE(tw_cast(t, &memory, world, &ray, 1u, &hit));
    tw_check_hit(t, &hit, TRAVERSE_FACE_INSIDE, 0.0f32, 0, 0, 0, 8u);
    tw_close(t, &memory);
}

TEST(a_ray_that_finds_nothing_says_so) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 4u, 0, 0);
    tw_set(world, 20, 20, 40, 8u);
    REQUIRE(tw_world_upload(t, &memory, world));
    traverse_ray_t rays[4] = {
        tw_ray(10, 20, 40, -1.0f32, 0.0f32, 0.0f32),  /* away from it, and out of the window */
        tw_ray(-30, 20, 40, -1.0f32, 0.0f32, 0.0f32), /* never enters the window */
        tw_ray(10, 21, 40, 1.0f32, 0.0f32, 0.0f32),   /* one row beside it */
        tw_ray(10, 20, 40, 1.0f32, 0.0f32, 0.0f32),   /* would hit, but starts past it */
    };
    rays[3].t_start = 11.0f32;
    traverse_hit_t hits[4];
    REQUIRE(tw_cast(t, &memory, world, rays, 4u, hits));
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        CHECK_EQ_U64(traverse_hit_face(hits[i].info), TRAVERSE_FACE_MISS);
        CHECK_EQ_U64((uint64_t)hits[i].info & TRAVERSE_INFO_CAPPED, 0u);
    }
    /* Voxel 20 is entered at 9.5 and left at 10.5, so a start at 11 is past it. */
    CHECK_EQ_U64(traverse_hit_face(hits[3].info), TRAVERSE_FACE_MISS);
    tw_close(t, &memory);
}

TEST(a_ray_stops_after_its_near_side_when_started_within_a_voxel) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 4u, 0, 0);
    tw_set(world, 20, 20, 40, 8u);
    REQUIRE(tw_world_upload(t, &memory, world));
    traverse_ray_t ray = tw_ray(10, 20, 40, 1.0f32, 0.0f32, 0.0f32);
    ray.t_start = 10.0f32; /* entered at 9.5, left at 10.5 */
    traverse_hit_t hit;
    REQUIRE(tw_cast(t, &memory, world, &ray, 1u, &hit));
    tw_check_hit(t, &hit, TRAVERSE_FACE_INSIDE, 10.0f32, 10, 0, 0, 8u);
    tw_close(t, &memory);
}

/* The reason the eye is a cell and a fraction: a float32_t position would have nothing left of a block at 16 million. */
TEST(the_answer_is_the_same_sixteen_million_blocks_out) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    const int32_t far_column = 1000000;
    const int32_t far_block = far_column * 16;
    tw_world_t *const near = tw_world_begin(t, 4u, 0, 0);
    tw_world_t *const far = tw_world_begin(t, 4u, far_column, far_column);
    tw_set(near, 30, 22, 40, 7u);
    tw_set(far, far_block + 30, far_block + 22, 40, 7u);
    REQUIRE(tw_world_upload(t, &memory, near));
    REQUIRE(tw_world_upload(t, &memory, far));
    const traverse_ray_t near_ray = tw_ray(10, 12, 40, 2.0f32, 1.03f32, 0.01f32);
    const traverse_ray_t far_ray = tw_ray(far_block + 10, far_block + 12, 40, 2.0f32, 1.03f32, 0.01f32);
    traverse_hit_t near_hit, far_hit;
    REQUIRE(tw_cast(t, &memory, near, &near_ray, 1u, &near_hit));
    REQUIRE(tw_cast(t, &memory, far, &far_ray, 1u, &far_hit));
    CHECK_EQ_U64(traverse_hit_face(near_hit.info), TRAVERSE_FACE_NEG_X);
    CHECK_MEM_EQ(&near_hit, &far_hit, sizeof(traverse_hit_t));
    tw_close(t, &memory);
}

TEST(what_makes_no_sense_is_refused_and_starts_nothing) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 2u, 0, 0);
    REQUIRE(tw_world_upload(t, &memory, world));
    device_block_t small = {0}, enough = {0};
    REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_FRAMES, 64u, "tw small", &small) == GPU_STATUS_OK);
    REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_FRAMES, 1u << 20, "tw enough", &enough) == GPU_STATUS_OK);
    const traverse_camera_t camera = {.forward = {1.0f32, 0.0f32, 0.0f32}, .width = 64u, .height = 64u};
    const traverse_targets_t small_targets = {&small, &small};
    const traverse_targets_t good_targets = {&enough, &enough};
    gpu_error_t error = {0};
    CHECK_EQ_U64(traverse_coarse(&error, &world->view, &camera, &small_targets), GPU_STATUS_INVALID_ARGUMENT);
    CHECK_CONTAINS(error.borrowed_what, "coarse target");
    CHECK_EQ_U64(traverse_primary(&error, &world->view, &camera, &small_targets, false), GPU_STATUS_INVALID_ARGUMENT);
    CHECK_CONTAINS(error.borrowed_what, "hit target");
    /* Only the coarse target is too small, and only coarse rays look at it. */
    const traverse_targets_t mixed_targets = {&small, &enough};
    CHECK_EQ_U64(traverse_primary(&error, &world->view, &camera, &mixed_targets, false), GPU_STATUS_OK);
    const traverse_camera_t big_camera = {.forward = {1.0f32, 0.0f32, 0.0f32}, .width = 1024u, .height = 1024u};
    CHECK_EQ_U64(traverse_primary(&error, &world->view, &big_camera, &mixed_targets, true),
                 GPU_STATUS_INVALID_ARGUMENT);
    const traverse_camera_t empty_camera = {.forward = {1.0f32, 0.0f32, 0.0f32}, .width = 0u, .height = 64u};
    CHECK_EQ_U64(traverse_coarse(&error, &world->view, &empty_camera, &good_targets), GPU_STATUS_INVALID_ARGUMENT);
    traverse_world_t bad_world = world->view;
    bad_world.window.side = 0u;
    bad_world.window.slot_count = 0u;
    CHECK_EQ_U64(traverse_coarse(&error, &bad_world, &camera, &good_targets), GPU_STATUS_INVALID_ARGUMENT);
    bad_world.window.side = 3u; /* the table holds 2 x 2 */
    bad_world.window.slot_count = 9u;
    CHECK_EQ_U64(traverse_primary(&error, &bad_world, &camera, &good_targets, false), GPU_STATUS_INVALID_ARGUMENT);
    CHECK_CONTAINS(error.borrowed_what, "column table");
    CHECK_EQ_U64(traverse_rays(&error, &world->view, &small, &small, 100u), GPU_STATUS_INVALID_ARGUMENT);
    CHECK_EQ_U64(traverse_rays(&error, &world->view, &small, &small, 0u), GPU_STATUS_OK);
    CHECK_EQ_U64(traverse_wait(&error), GPU_STATUS_OK);
    tw_close(t, &memory);
}

/* ---- The CPU reference: exact voxel-by-voxel Amanatides and Woo in float64_t, on the dense array --------------------------- */

typedef struct tw_reference_hit_t {
    float64_t t;      /* 0 */
    int32_t cell[3];  /* 8: world x, y and storage height */
    uint32_t value;   /* 20 */
    uint32_t face;    /* 24 */
    uint8_t _pad0[4]; /* 28 */
} tw_reference_hit_t;
static_assert(sizeof(tw_reference_hit_t) == 32);

static uint32_t tw_value_at(const tw_world_t *world, const int32_t cell[3]) {
    const uint32_t x = (uint32_t)(cell[0] - world->min_block[0]), y = (uint32_t)(cell[1] - world->min_block[1]);
    if (x >= world->width || y >= world->width || (uint32_t)cell[2] >= TW_DENSE_HEIGHT) {
        return 0u;
    }
    return tw_value(world, x, y, (uint32_t)cell[2]);
}

/* origin is in world x, y and storage height, and direction is unit length. */
static void tw_reference_trace(const tw_world_t *world, const float64_t origin[3], const float64_t direction[3],
                               float64_t t_start, tw_reference_hit_t *out) {
    const float64_t box_low[3] = {(float64_t)world->min_block[0], (float64_t)world->min_block[1], 0.0f64};
    const float64_t box_high[3] = {box_low[0] + (float64_t)world->width, box_low[1] + (float64_t)world->width,
                                   (float64_t)TRAVERSE_HEIGHT_BLOCKS};
    float64_t t_near = t_start, t_far = 1e300f64;
    float64_t inverse[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        inverse[i] = 1.0f64 / direction[i];
        const float64_t a = (box_low[i] - origin[i]) * inverse[i], b = (box_high[i] - origin[i]) * inverse[i];
        t_near = fmax(t_near, fmin(a, b));
        t_far = fmin(t_far, fmax(a, b));
    }
    *out = (tw_reference_hit_t){.t = (float64_t)TRAVERSE_COARSE_SKY, .face = TRAVERSE_FACE_MISS};
    if (!(t_near < t_far)) {
        return;
    }
    int32_t cell[3], step[3];
    float64_t next[3], delta[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float64_t found = floor(origin[i] + direction[i] * t_near);
        cell[i] = (int32_t)fmin(fmax(found, box_low[i]), box_high[i] - 1.0f64);
        step[i] = direction[i] > 0.0f64 ? 1 : -1;
        next[i] = ((float64_t)(cell[i] + (step[i] > 0 ? 1 : 0)) - origin[i]) * inverse[i];
        delta[i] = fabs(inverse[i]);
    }
    for (;;) {
        if ((uint32_t)(cell[0] - world->min_block[0]) >= world->width ||
            (uint32_t)(cell[1] - world->min_block[1]) >= world->width || (uint32_t)cell[2] >= TRAVERSE_HEIGHT_BLOCKS) {
            return;
        }
        const uint32_t value = tw_value_at(world, cell);
        if (value != 0u) {
            float64_t t = -1e300f64;
            uint32_t axis = 0u;
            for (uint32_t i = 0u; i < 3u; i += 1u) {
                const float64_t t_in = ((float64_t)(cell[i] + (step[i] > 0 ? 0 : 1)) - origin[i]) * inverse[i];
                if (t_in > t) {
                    t = t_in;
                    axis = i;
                }
            }
            out->value = value;
            memcpy(out->cell, cell, sizeof(cell));
            if (t <= t_start) {
                out->t = t_start;
                out->face = TRAVERSE_FACE_INSIDE;
            } else {
                out->t = t;
                out->face = axis * 2u + (step[axis] > 0 ? 0u : 1u);
            }
            return;
        }
        const uint32_t axis = next[0] <= next[1] && next[0] <= next[2] ? 0u : (next[1] <= next[2] ? 1u : 2u);
        cell[axis] += step[axis];
        next[axis] += delta[axis];
    }
}

/* How far a float32_t kernel ray can stray from the float64_t ray, in blocks, after a path of t blocks. Derived in Stage 01.3
 * issue 11, not tuned, with u = 2^-24: 22u t for directions the CPU and card each computed, 3u t for the parameter, 1u t for the
 * position, and 1u of a coordinate. Coordinates here are from the eye, so that last one is also below 1u t. Rounded up:
 * t * 2^-19 + 2^-20. */
static float64_t tw_rounding_pad(float64_t path) {
    return path / 524288.0f64 + 1.0f64 / 1048576.0f64;
}

/* A slab test against a voxel grown by pad on each side; pad below zero shrinks it. */
static bool tw_ray_meets_voxel(const float64_t origin[3], const float64_t direction[3], const int32_t voxel[3],
                               float64_t pad, float64_t *t_in) {
    float64_t low_t = 0.0f64, high_t = 1e300f64;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float64_t a = ((float64_t)voxel[i] - pad - origin[i]) / direction[i];
        const float64_t b = ((float64_t)voxel[i] + 1.0f64 + pad - origin[i]) / direction[i];
        low_t = fmax(low_t, fmin(a, b));
        high_t = fmin(high_t, fmax(a, b));
    }
    if (t_in != nullptr) {
        *t_in = low_t;
    }
    return low_t <= high_t;
}

static float64_t tw_voxel_path(const float64_t origin[3], const int32_t voxel[3]) {
    const float64_t a = (float64_t)voxel[0] + 0.5f64 - origin[0];
    const float64_t b = (float64_t)voxel[1] + 0.5f64 - origin[1];
    const float64_t c = (float64_t)voxel[2] + 0.5f64 - origin[2];
    return sqrt(a * a + b * b + c * c) + 1.0f64;
}

static bool tw_voxel_is_grazed(const float64_t origin[3], const float64_t direction[3], const int32_t voxel[3]) {
    return !tw_ray_meets_voxel(origin, direction, voxel, -tw_rounding_pad(tw_voxel_path(origin, voxel)), nullptr);
}

/* Is the kernel's different answer one rounding can explain? got and voxel are the kernel's. */
static bool tw_answer_is_rounding(const tw_world_t *world, const float64_t origin[3], const float64_t direction[3],
                                  bool got, const int32_t voxel[3], uint32_t value,
                                  const tw_reference_hit_t *reference) {
    const bool reference_got = reference->face != TRAVERSE_FACE_MISS;
    if (!got) {
        return reference_got && tw_voxel_is_grazed(origin, direction, reference->cell);
    }
    /* The kernel names a voxel: it must be there, with that value, and the ray must pass within rounding of it. */
    float64_t t_kernel = 0.0f64;
    if (tw_value_at(world, voxel) != value ||
        !tw_ray_meets_voxel(origin, direction, voxel, tw_rounding_pad(tw_voxel_path(origin, voxel)), &t_kernel)) {
        return false;
    }
    if (!reference_got) {
        return true;
    }
    float64_t t_reference = 0.0f64;
    (void)tw_ray_meets_voxel(origin, direction, reference->cell, 0.0f64, &t_reference);
    /* In front of the reference's voxel: the reference narrowly missed it. Behind: the kernel narrowly missed that one. */
    return t_kernel <= t_reference || tw_voxel_is_grazed(origin, direction, reference->cell);
}

/* Same voxel, another face: where the ray crosses the kernel's face plane must be within rounding of the voxel. */
static bool tw_face_is_rounding(const float64_t origin[3], const float64_t direction[3], uint32_t face,
                                const int32_t voxel[3], float64_t t_start) {
    float64_t t = t_start;
    if (face < TRAVERSE_FACE_INSIDE) {
        const uint32_t axis = face / 2u;
        t = ((float64_t)voxel[axis] + (float64_t)(face & 1u) - origin[axis]) / direction[axis];
    }
    const float64_t pad = tw_rounding_pad(tw_voxel_path(origin, voxel));
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float64_t q = origin[i] + direction[i] * t;
        if (q < (float64_t)voxel[i] - pad || q > (float64_t)voxel[i] + 1.0f64 + pad) {
            return false;
        }
    }
    return true;
}

typedef struct tw_tally_t {
    uint64_t rays;           /* 0 */
    uint64_t got_differs;    /* 8 */
    uint64_t voxel_differs;  /* 16 */
    uint64_t face_differs;   /* 24 */
    uint64_t t_differs;      /* 32: same voxel and face, and t further off than rounding: never explained */
    uint64_t value_differs;  /* 40: same voxel, other value: never explained */
    uint64_t unexplained;    /* 48 */
    uint64_t capped;         /* 56 */
    uint64_t coarse_too_far; /* 64: a ray hits something before its tile's coarse distance */
    uint64_t steps;          /* 72 */
    uint64_t misses;         /* 80 */
} tw_tally_t;
static_assert(sizeof(tw_tally_t) == 88);

static void tw_tally_add(tw_tally_t *sum, const tw_tally_t *part) {
    sum->rays += part->rays;
    sum->got_differs += part->got_differs;
    sum->voxel_differs += part->voxel_differs;
    sum->face_differs += part->face_differs;
    sum->t_differs += part->t_differs;
    sum->value_differs += part->value_differs;
    sum->unexplained += part->unexplained;
    sum->capped += part->capped;
    sum->coarse_too_far += part->coarse_too_far;
    sum->steps += part->steps;
    sum->misses += part->misses;
}

/* Compares one ray's kernel answer with the reference's. eye is the kernel's eye cell, as world x, y and storage height. */
static void tw_compare(tw_tally_t *tally, const tw_world_t *world, const float64_t origin[3],
                       const float64_t direction[3], float64_t t_start, const int32_t eye[3], const traverse_hit_t *hit,
                       const tw_reference_hit_t *reference) {
    const uint32_t face = traverse_hit_face(hit->info);
    const bool got = face != TRAVERSE_FACE_MISS, reference_got = reference->face != TRAVERSE_FACE_MISS;
    tally->rays += 1u;
    tally->steps += traverse_hit_steps(hit->info);
    tally->misses += got ? 0u : 1u;
    tally->capped += ((uint32_t)hit->info & TRAVERSE_INFO_CAPPED) != 0u ? 1u : 0u;
    const int32_t voxel[3] = {eye[0] + hit->offset[0], eye[1] + hit->offset[1], eye[2] + hit->offset[2]};
    const bool same_voxel = got && reference_got && memcmp(voxel, reference->cell, sizeof(voxel)) == 0;
    if (got != reference_got || (got && !same_voxel)) {
        tally->got_differs += got != reference_got ? 1u : 0u;
        tally->voxel_differs += got == reference_got ? 1u : 0u;
        if (!tw_answer_is_rounding(world, origin, direction, got, voxel, hit->value, reference)) {
            tally->unexplained += 1u;
        }
        return;
    }
    if (!got) {
        return;
    }
    if (hit->value != reference->value) {
        tally->value_differs += 1u;
        tally->unexplained += 1u;
    }
    if (face != reference->face) {
        tally->face_differs += 1u;
        if (!tw_face_is_rounding(origin, direction, face, voxel, t_start)) {
            tally->unexplained += 1u;
        }
        return;
    }
    if (face != TRAVERSE_FACE_INSIDE) {
        /* What matters of t is the position it gives on the face's axis. */
        const uint32_t axis = face / 2u;
        const float64_t gap = fabs((float64_t)hit->t - reference->t) * fabs(direction[axis]);
        if (gap > tw_rounding_pad(reference->t + 2.0f64)) {
            tally->t_differs += 1u;
            tally->unexplained += 1u;
        }
    }
}

/* ---- Frames against the reference ------------------------------------------------------------------------------------- */

typedef struct tw_frame_job_t {
    const tw_world_t *world;         /* 0 */
    const traverse_camera_t *camera; /* 8 */
    const traverse_hit_t *hits;      /* 16 */
    const float32_t *coarse;         /* 24: nullptr: nothing to check it against */
    tw_tally_t tally;                /* 32 */
    float64_t eye_error;             /* 120: moves only the reference's eye: a kernel wrong by that much */
    uint32_t start;                  /* 128 */
    uint32_t stride;                 /* 132 */
    uint32_t every;                  /* 136: which pixels: every this many, each way */
    uint8_t _pad0[4];                /* 140 */
} tw_frame_job_t;
static_assert(sizeof(tw_frame_job_t) == 144);

static float32_t tw_guard(float32_t value) {
    if (value > -1e-6f32 && value < 1e-6f32) {
        return value < 0.0f32 ? -1e-6f32 : 1e-6f32;
    }
    return value;
}

static void *tw_frame_rows(void *argument) {
    tw_frame_job_t *const job = argument;
    const traverse_camera_t *const c = job->camera;
    const int32_t eye[3] = {c->eye_cell[0], c->eye_cell[1], c->eye_cell[2] - TRAVERSE_Z_BASE};
    const float64_t origin[3] = {(float64_t)eye[0] + (float64_t)c->eye_frac[0] + job->eye_error,
                                 (float64_t)eye[1] + (float64_t)c->eye_frac[1] + job->eye_error,
                                 (float64_t)eye[2] + (float64_t)c->eye_frac[2] + job->eye_error};
    for (uint32_t py = job->start; py < c->height; py += job->stride * job->every) {
        for (uint32_t px = 0u; px < c->width; px += job->every) {
            const float32_t u = 2.0f32 * ((float32_t)px + 0.5f32) / (float32_t)c->width - 1.0f32;
            const float32_t v = 1.0f32 - 2.0f32 * ((float32_t)py + 0.5f32) / (float32_t)c->height;
            float32_t d[3];
            for (uint32_t i = 0u; i < 3u; i += 1u) {
                d[i] = c->forward[i] + u * c->right[i] + v * c->up[i];
            }
            const float32_t scale = 1.0f32 / sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            const float64_t direction[3] = {(float64_t)tw_guard(d[0] * scale), (float64_t)tw_guard(d[1] * scale),
                                            (float64_t)tw_guard(d[2] * scale)};
            tw_reference_hit_t reference;
            tw_reference_trace(job->world, origin, direction, 0.0f64, &reference);
            tw_compare(&job->tally, job->world, origin, direction, 0.0f64, eye,
                       &job->hits[(uint64_t)py * c->width + px], &reference);
            if (job->coarse != nullptr && reference.face != TRAVERSE_FACE_MISS) {
                const uint32_t tiles_x = traverse_tiles_across(c->width);
                const uint32_t tile = py / TRAVERSE_TILE_PIXELS * tiles_x + px / TRAVERSE_TILE_PIXELS;
                if (reference.t < (float64_t)job->coarse[tile]) {
                    job->tally.coarse_too_far += 1u;
                }
            }
        }
    }
    return nullptr;
}

static tw_tally_t tw_frame_compare(const tw_world_t *world, const traverse_camera_t *camera, const traverse_hit_t *hits,
                                   const float32_t *coarse, uint32_t every, float64_t eye_error) {
    tw_frame_job_t jobs[TW_THREADS];
    pthread_t threads[TW_THREADS];
    tw_tally_t sum = {0};
    for (uint32_t i = 0u; i < TW_THREADS; i += 1u) {
        jobs[i] = (tw_frame_job_t){.world = world,
                                   .camera = camera,
                                   .hits = hits,
                                   .coarse = coarse,
                                   .eye_error = eye_error,
                                   .start = i * every,
                                   .stride = TW_THREADS,
                                   .every = every};
        if (pthread_create(&threads[i], nullptr, tw_frame_rows, &jobs[i]) != 0) {
            perror("pthread_create");
            abort();
        }
    }
    for (uint32_t i = 0u; i < TW_THREADS; i += 1u) {
        pthread_join(threads[i], nullptr);
        tw_tally_add(&sum, &jobs[i].tally);
    }
    return sum;
}

typedef struct tw_targets_t {
    device_block_t coarse;      /* 0 */
    device_block_t hits;        /* 24 */
    traverse_targets_t targets; /* 48 */
    traverse_hit_t *host_hits;  /* 64 */
    float32_t *host_coarse;     /* 72 */
} tw_targets_t;
static_assert(sizeof(tw_targets_t) == 80);

static bool tw_targets_make(test_context_t *t, device_memory_t *memory, uint32_t width, uint32_t height,
                            tw_targets_t *out) {
    memset(out, 0, sizeof(*out));
    const uint64_t coarse_bytes = traverse_coarse_bytes(width, height), hit_bytes = traverse_hits_bytes(width, height);
    out->host_hits = test_alloc(t, hit_bytes);
    out->host_coarse = test_alloc(t, coarse_bytes);
    out->targets = (traverse_targets_t){&out->coarse, &out->hits};
    return CHECK_EQ_U64(device_memory_alloc(memory, DEVICE_POOL_FRAMES, coarse_bytes, "tw coarse", &out->coarse),
                        GPU_STATUS_OK) &&
           CHECK_EQ_U64(device_memory_alloc(memory, DEVICE_POOL_FRAMES, hit_bytes, "tw hits", &out->hits),
                        GPU_STATUS_OK);
}

static bool tw_targets_free(test_context_t *t, device_memory_t *memory, tw_targets_t *targets) {
    return CHECK_EQ_U64(device_memory_free(memory, &targets->coarse), GPU_STATUS_OK) &&
           CHECK_EQ_U64(device_memory_free(memory, &targets->hits), GPU_STATUS_OK);
}

/* Runs the passes, and reads back what they wrote. With use_coarse off the coarse pass does not run. */
static bool tw_frame_run(test_context_t *t, device_memory_t *memory, const tw_world_t *world,
                         const traverse_camera_t *camera, tw_targets_t *targets, bool use_coarse) {
    gpu_error_t error = {0};
    gpu_status_t status = GPU_STATUS_OK;
    if (use_coarse) {
        status = traverse_coarse(&error, &world->view, camera, &targets->targets);
    }
    if (status == GPU_STATUS_OK) {
        status = traverse_primary(&error, &world->view, camera, &targets->targets, use_coarse);
    }
    if (status == GPU_STATUS_OK) {
        status = traverse_wait(&error);
    }
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&error);
    }
    if (!CHECK_EQ_U64(status, GPU_STATUS_OK)) {
        return false;
    }
    const uint64_t hit_bytes = traverse_hits_bytes(camera->width, camera->height);
    if (!CHECK_EQ_U64(device_memory_read(memory, &targets->hits, 0u, targets->host_hits, hit_bytes), GPU_STATUS_OK)) {
        return false;
    }
    if (use_coarse) {
        const uint64_t coarse_bytes = traverse_coarse_bytes(camera->width, camera->height);
        return CHECK_EQ_U64(device_memory_read(memory, &targets->coarse, 0u, targets->host_coarse, coarse_bytes),
                            GPU_STATUS_OK);
    }
    return true;
}

/* ---- Random worlds, every kind of node at once -------------------------------------------------------------------------- */

static uint32_t tw_random(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float32_t tw_random_unit(uint32_t *state) {
    return (float32_t)(tw_random(state) & 0xFFFFFFu) / 16777216.0f32;
}

static tw_world_t *tw_random_world(test_context_t *t, uint32_t columns_wide, int32_t min_x, int32_t min_y,
                                   uint32_t seed) {
    tw_world_t *const world = tw_world_begin(t, columns_wide, min_x, min_y);
    uint32_t state = seed;
    for (uint32_t cy = 0u; cy < columns_wide; cy += 1u) {
        for (uint32_t cx = 0u; cx < columns_wide; cx += 1u) {
            const uint32_t sections = 8u + tw_random(&state) % 16u;
            for (uint32_t sz = 0u; sz < sections; sz += 1u) {
                const uint32_t kind = tw_random(&state) % 8u;
                for (uint32_t i = 0u; i < 4096u && kind >= 3u; i += 1u) {
                    const uint32_t x = (i & 15u), y = (i >> 4) & 15u, z = i >> 8;
                    uint32_t material = 0u;
                    const uint32_t h =
                        tw_hash((int32_t)(cx * 16u + x), (int32_t)(cy * 16u + y), (int32_t)(sz * 16u + z)) ^ seed;
                    switch (kind) {
                    case 3u: /* solid, so a uniform section */
                        material = TW_MATERIAL_STONE;
                        break;
                    case 4u: /* whole bricks of one value */
                        material = (tw_hash((int32_t)(cx * 4u + x / 4u), (int32_t)(cy * 4u + y / 4u),
                                            (int32_t)(sz * 4u + z / 4u)) ^
                                    seed) % 4u ==
                                           0u
                                       ? 2u
                                       : 0u;
                        break;
                    case 5u: /* sparse, each with its own value */
                        material = h % 32u == 0u ? TW_MATERIAL_HASHED : 0u;
                        break;
                    case 6u: /* a floor */
                        material = z == 3u ? 3u : 0u;
                        break;
                    default: /* a rare voxel */
                        material = h % 512u == 0u ? TW_MATERIAL_HASHED : 0u;
                        break;
                    }
                    if (material != 0u) {
                        world->dense[tw_index(world, cx * 16u + x, cy * 16u + y, sz * 16u + z)] = (uint8_t)material;
                    }
                }
            }
        }
    }
    return world;
}

enum : uint32_t { TW_RANDOM_RAYS = 400000u };

TEST(random_rays_match_the_reference) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_random_world(t, 9u, -5, 3, 0x1234567u);
    REQUIRE(tw_world_upload(t, &memory, world));
    CHECK(world->uniform_sections > 0u);
    CHECK(world->voxel_count > 0u);
    traverse_ray_t *const rays = test_alloc(t, TW_RANDOM_RAYS * sizeof(traverse_ray_t));
    traverse_hit_t *const hits = test_alloc(t, TW_RANDOM_RAYS * sizeof(traverse_hit_t));
    uint32_t state = 0x9E3779B9u;
    const float32_t span = (float32_t)world->width;
    for (uint32_t i = 0u; i < TW_RANDOM_RAYS; i += 1u) {
        traverse_ray_t *const ray = &rays[i];
        const uint32_t kind = i % 8u;
        /* Eyes in, and a little outside, the window, on whole planes for some. */
        const float32_t px = tw_random_unit(&state) * (span + 80.0f32) - 40.0f32;
        const float32_t py = tw_random_unit(&state) * (span + 80.0f32) - 40.0f32;
        const float32_t pz = tw_random_unit(&state) * 560.0f32 - 20.0f32;
        ray->eye_cell[0] = (int32_t)floorf(px) + world->min_block[0];
        ray->eye_cell[1] = (int32_t)floorf(py) + world->min_block[1];
        ray->eye_cell[2] = (int32_t)floorf(pz) + TRAVERSE_Z_BASE;
        ray->eye_frac[0] = kind == 6u ? 0.0f32 : px - floorf(px);
        ray->eye_frac[1] = kind == 6u ? 0.0f32 : py - floorf(py);
        ray->eye_frac[2] = kind == 6u ? 0.0f32 : pz - floorf(pz);
        if (kind == 5u) { /* along an axis, where the guard matters */
            const uint32_t axis = tw_random(&state) % 3u;
            ray->direction[axis] = (tw_random(&state) & 1u) != 0u ? 1.0f32 : -1.0f32;
        } else if (kind == 6u) { /* exact diagonals, from corner to corner */
            ray->direction[0] = (tw_random(&state) & 1u) != 0u ? 1.0f32 : -1.0f32;
            ray->direction[1] = (tw_random(&state) & 1u) != 0u ? 1.0f32 : -1.0f32;
            ray->direction[2] = (tw_random(&state) & 1u) != 0u ? 1.0f32 : -1.0f32;
        } else {
            ray->direction[0] = tw_random_unit(&state) * 2.0f32 - 1.0f32;
            ray->direction[1] = tw_random_unit(&state) * 2.0f32 - 1.0f32;
            ray->direction[2] = kind == 7u ? 0.002f32 * (tw_random_unit(&state) - 0.5f32) /* nearly flat */
                                           : tw_random_unit(&state) * 2.0f32 - 1.0f32;
            if (ray->direction[0] == 0.0f32 && ray->direction[1] == 0.0f32 && ray->direction[2] == 0.0f32) {
                ray->direction[0] = 1.0f32;
            }
        }
        ray->t_start = kind == 4u ? tw_random_unit(&state) * 64.0f32 : 0.0f32;
    }
    REQUIRE(tw_cast(t, &memory, world, rays, TW_RANDOM_RAYS, hits));

    /* Exact diagonals pass through voxel corners, where a tie decides which of two voxels is first, so they are kept apart. */
    tw_tally_t tally = {0}, diagonals = {0};
    for (uint32_t i = 0u; i < TW_RANDOM_RAYS; i += 1u) {
        const traverse_ray_t *const ray = &rays[i];
        const float32_t *const given = ray->direction;
        const float32_t scale = 1.0f32 / sqrtf(given[0] * given[0] + given[1] * given[1] + given[2] * given[2]);
        const float64_t direction[3] = {(float64_t)tw_guard(given[0] * scale), (float64_t)tw_guard(given[1] * scale),
                                        (float64_t)tw_guard(given[2] * scale)};
        const int32_t eye[3] = {ray->eye_cell[0], ray->eye_cell[1], ray->eye_cell[2] - TRAVERSE_Z_BASE};
        const float64_t origin[3] = {(float64_t)eye[0] + (float64_t)ray->eye_frac[0],
                                     (float64_t)eye[1] + (float64_t)ray->eye_frac[1],
                                     (float64_t)eye[2] + (float64_t)ray->eye_frac[2]};
        tw_reference_hit_t reference;
        tw_reference_trace(world, origin, direction, (float64_t)ray->t_start, &reference);
        tw_compare(i % 8u == 6u ? &diagonals : &tally, world, origin, direction, (float64_t)ray->t_start, eye, &hits[i],
                   &reference);
    }
    printf("  exact diagonals: %" PRIu64 " rays; differ: hit %" PRIu64 ", voxel %" PRIu64 ", face %" PRIu64
           "; not explained: %" PRIu64 "\n",
           diagonals.rays, diagonals.got_differs, diagonals.voxel_differs, diagonals.face_differs,
           diagonals.unexplained);
    CHECK_EQ_U64(diagonals.capped, 0u);
    CHECK_EQ_U64(diagonals.unexplained, 0u);
    printf("  %" PRIu64 " rays, %" PRIu64 " misses, %" PRIu64 " steps; differ: hit %" PRIu64 ", voxel %" PRIu64
           ", face %" PRIu64 ", t %" PRIu64 ", value %" PRIu64 "; not explained by rounding: %" PRIu64 "\n",
           tally.rays, tally.misses, tally.steps, tally.got_differs, tally.voxel_differs, tally.face_differs,
           tally.t_differs, tally.value_differs, tally.unexplained);
    CHECK_EQ_U64(tally.rays + diagonals.rays, TW_RANDOM_RAYS);
    CHECK_EQ_U64(tally.capped, 0u);
    CHECK_EQ_U64(tally.unexplained, 0u);
    CHECK(tally.misses > 0u && tally.misses < tally.rays);
    tw_close(t, &memory);
}

/* ---- The coarse pass ------------------------------------------------------------------------------------------------- */

TEST(the_coarse_pass_knows_a_lone_voxel_and_the_sky) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 16u, 0, 0);
    /* A plate 7 x 7 across, wider than the 1.6 blocks between pixels at that distance. */
    for (int32_t dz = -3; dz <= 3; dz += 1) {
        for (int32_t dy = -3; dy <= 3; dy += 1) {
            tw_set(world, 200, 128 + dy, 40 + dz, 7u);
        }
    }
    REQUIRE(tw_world_upload(t, &memory, world));
    /* The eye is 100 cells west of it, looking along +x. The middle of a 64 x 64 frame looks straight at it. */
    const float32_t eye[3] = {100.5f32, 128.5f32, 40.5f32 + 256.0f32};
    const traverse_camera_t camera = tw_camera(world, eye, 0.0f32, 0.0f32, 0.5f32, 64u, 64u);
    tw_targets_t targets;
    REQUIRE(tw_targets_make(t, &memory, 64u, 64u, &targets));
    REQUIRE(tw_frame_run(t, &memory, world, &camera, &targets, true));
    const uint32_t tiles = traverse_tiles_across(64u);
    uint32_t sky = 0u;
    for (uint32_t i = 0u; i < tiles * tiles; i += 1u) {
        sky += targets.host_coarse[i] >= TRAVERSE_COARSE_SKY ? 1u : 0u;
    }
    /* The plate's near side is 99.5 away. A tile of this frame is a wide cone, about 0.09 across at the corners: the march stops
     * when the short stretch's box, grown by that, reaches it, which is near t + 4 + 0.09 (t + 4) = 99.5, so t = 87, less up to
     * one short stretch and the back-off. */
    uint32_t hit_pixel = 0u;
    for (uint32_t i = 0u; i < 64u * 64u; i += 1u) {
        hit_pixel += traverse_hit_face(targets.host_hits[i].info) != TRAVERSE_FACE_MISS ? 1u : 0u;
    }
    CHECK(hit_pixel > 0u);
    const float32_t middle = targets.host_coarse[(tiles / 2u) * tiles + tiles / 2u];
    printf("  the middle tile starts at %.3f, and %u of %u tiles know there is nothing\n", (libc_double_t)middle, sky,
           tiles * tiles);
    CHECK(middle < 99.5f32);
    CHECK(middle > 80.0f32);
    CHECK(sky >= tiles * tiles - 16u);
    tw_targets_free(t, &memory, &targets);
    tw_close(t, &memory);
}

/* The coarse pass reads the same mask its own way: it must not call a box empty that has a block in the second word, nor one across the two. */
TEST(the_coarse_pass_sees_blocks_in_either_word_of_a_columns_mask) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 16u, 0, 0);
    /* A plate as in the test of a lone voxel, high in the second word. */
    for (int32_t dz = -3; dz <= 3; dz += 1) {
        for (int32_t dy = -3; dy <= 3; dy += 1) {
            tw_set(world, 200, 128 + dy, 1100 + dz, 7u);
        }
    }
    REQUIRE(tw_world_upload(t, &memory, world));
    const float32_t eye[3] = {100.5f32, 128.5f32, 1100.5f32 + 256.0f32};
    const traverse_camera_t camera = tw_camera(world, eye, 0.0f32, 0.0f32, 0.5f32, 64u, 64u);
    tw_targets_t targets;
    REQUIRE(tw_targets_make(t, &memory, 64u, 64u, &targets));
    REQUIRE(tw_frame_run(t, &memory, world, &camera, &targets, true));
    const uint32_t tiles = traverse_tiles_across(64u);
    uint32_t hit_pixel = 0u;
    for (uint32_t i = 0u; i < 64u * 64u; i += 1u) {
        hit_pixel += traverse_hit_face(targets.host_hits[i].info) != TRAVERSE_FACE_MISS ? 1u : 0u;
    }
    CHECK(hit_pixel > 0u);
    const float32_t middle = targets.host_coarse[(tiles / 2u) * tiles + tiles / 2u];
    CHECK(middle < 99.5f32);
    CHECK(middle > 80.0f32);
    tw_targets_free(t, &memory, &targets);
    /* Now one across the boundary, z = 760 to 775, looked at from the side. */
    tw_world_t *const across = tw_world_begin(t, 16u, 0, 0);
    for (int32_t z = 760; z <= 775; z += 1) {
        for (int32_t dy = -3; dy <= 3; dy += 1) {
            tw_set(across, 200, 128 + dy, z, 7u);
        }
    }
    REQUIRE(tw_world_upload(t, &memory, across));
    const float32_t eye_across[3] = {100.5f32, 128.5f32, 767.5f32 + 256.0f32};
    const traverse_camera_t camera_across = tw_camera(across, eye_across, 0.0f32, 0.0f32, 0.5f32, 64u, 64u);
    tw_targets_t targets_across;
    REQUIRE(tw_targets_make(t, &memory, 64u, 64u, &targets_across));
    REQUIRE(tw_frame_run(t, &memory, across, &camera_across, &targets_across, true));
    const float32_t middle_across = targets_across.host_coarse[(tiles / 2u) * tiles + tiles / 2u];
    CHECK(middle_across < 99.5f32);
    CHECK(middle_across > 80.0f32);
    tw_targets_free(t, &memory, &targets_across);
    tw_close(t, &memory);
}

TEST(a_coarse_pass_starting_beside_solid_ground_says_zero) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 4u, 0, 0);
    /* A solid world, with one cell of air for the eye. */
    memset(world->dense, TW_MATERIAL_STONE, (uint64_t)world->width * world->width * TW_DENSE_HEIGHT);
    tw_set(world, 30, 30, 0, 0u);
    REQUIRE(tw_world_upload(t, &memory, world));
    const float32_t eye[3] = {30.5f32, 30.5f32, 256.5f32};
    const traverse_camera_t camera = tw_camera(world, eye, 0.3f32, 0.1f32, 0.7f32, 64u, 64u);
    tw_targets_t targets;
    REQUIRE(tw_targets_make(t, &memory, 64u, 64u, &targets));
    REQUIRE(tw_frame_run(t, &memory, world, &camera, &targets, true));
    for (uint32_t i = 0u; i < 8u * 8u; i += 1u) {
        CHECK_EQ_U64(targets.host_coarse[i] == 0.0f32, 1u);
    }
    tw_targets_free(t, &memory, &targets);
    tw_close(t, &memory);
}

/* Random cameras in random worlds, in sizes that do not divide by 8 and fields of view from narrow to very wide, every pixel. */
TEST(the_coarse_pass_never_says_too_far_and_rays_from_it_match) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_random_world(t, 12u, 2, -7, 0xABCDEFu);
    REQUIRE(tw_world_upload(t, &memory, world));
    static const uint32_t sizes[][2] = {{1u, 1u}, {7u, 5u}, {16u, 9u}, {64u, 36u}, {97u, 53u}, {320u, 180u}};
    const uint32_t size_count = sizeof(sizes) / sizeof(sizes[0]);
    tw_targets_t targets;
    REQUIRE(tw_targets_make(t, &memory, 320u, 180u, &targets));
    uint32_t state = 0x5EEDu;
    tw_tally_t sum = {0};
    uint64_t skipped = 0u, tiles = 0u;
    for (uint32_t i = 0u; i < 120u; i += 1u) {
        const uint32_t *const size = sizes[i % size_count];
        const float32_t span = (float32_t)world->width;
        const float32_t eye[3] = {tw_random_unit(&state) * (span + 40.0f32) - 20.0f32,
                                  tw_random_unit(&state) * (span + 40.0f32) - 20.0f32,
                                  tw_random_unit(&state) * 640.0f32 - 20.0f32};
        const float32_t yaw = tw_random_unit(&state) * 2.0f32 * TW_PI;
        const float32_t pitch = (tw_random_unit(&state) - 0.5f32) * 3.0f32;
        const float32_t fov = 0.05f32 + tw_random_unit(&state) * tw_random_unit(&state) * 3.0f32;
        const traverse_camera_t camera = tw_camera(world, eye, yaw, pitch, fov, size[0], size[1]);
        REQUIRE(tw_frame_run(t, &memory, world, &camera, &targets, true));
        const tw_tally_t part = tw_frame_compare(world, &camera, targets.host_hits, targets.host_coarse, 1u, 0.0f64);
        tw_tally_add(&sum, &part);
        const uint32_t tile_count = traverse_tiles_across(size[0]) * traverse_tiles_across(size[1]);
        for (uint32_t k = 0u; k < tile_count; k += 1u) {
            skipped += targets.host_coarse[k] > 0.0f32 ? 1u : 0u;
        }
        tiles += tile_count;
    }
    printf("  %" PRIu64 " rays; %" PRIu64 " of %" PRIu64 " tiles start past 0; differ: hit %" PRIu64 ", voxel %" PRIu64
           ", face %" PRIu64 ", t %" PRIu64 "; not explained: %" PRIu64 "; coarse too far: %" PRIu64 "\n",
           sum.rays, skipped, tiles, sum.got_differs, sum.voxel_differs, sum.face_differs, sum.t_differs,
           sum.unexplained, sum.coarse_too_far);
    CHECK_EQ_U64(sum.coarse_too_far, 0u);
    CHECK_EQ_U64(sum.unexplained, 0u);
    CHECK_EQ_U64(sum.capped, 0u);
    CHECK(skipped > tiles / 8u); /* a pass that always says 0 would pass everything above, and do nothing */
    tw_targets_free(t, &memory, &targets);
    tw_close(t, &memory);
}

/* If the kernel really reads the coarse values, and the comparison really looks, a distance made too long has to show. */
TEST(a_forged_coarse_distance_is_caught_by_the_comparison) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 8u, 0, 0);
    for (int32_t y = 0; y < 128; y += 1) {
        for (int32_t x = 0; x < 128; x += 1) {
            tw_set(world, x, y, 0, TW_MATERIAL_STONE);
        }
    }
    REQUIRE(tw_world_upload(t, &memory, world));
    const float32_t eye[3] = {64.5f32, 64.5f32, 256.0f32 + 40.0f32};
    const traverse_camera_t camera = tw_camera(world, eye, 0.2f32, -1.0f32, 0.5f32, 64u, 64u);
    tw_targets_t targets;
    REQUIRE(tw_targets_make(t, &memory, 64u, 64u, &targets));

    /* The honest run: nothing to say. */
    REQUIRE(tw_frame_run(t, &memory, world, &camera, &targets, true));
    tw_tally_t honest = tw_frame_compare(world, &camera, targets.host_hits, targets.host_coarse, 1u, 0.0f64);
    CHECK_EQ_U64(honest.coarse_too_far, 0u);
    CHECK_EQ_U64(honest.unexplained, 0u);

    /* Every tile says to start past the ground. */
    const uint32_t tile_count = 8u * 8u;
    for (uint32_t i = 0u; i < tile_count; i += 1u) {
        targets.host_coarse[i] = 300.0f32;
    }
    REQUIRE(device_memory_write(&memory, &targets.coarse, 0u, targets.host_coarse, tile_count * sizeof(float32_t)) ==
            GPU_STATUS_OK);
    gpu_error_t error = {0};
    REQUIRE(traverse_primary(&error, &world->view, &camera, &targets.targets, true) == GPU_STATUS_OK);
    REQUIRE(traverse_wait(&error) == GPU_STATUS_OK);
    REQUIRE(device_memory_read(&memory, &targets.hits, 0u, targets.host_hits, traverse_hits_bytes(64u, 64u)) ==
            GPU_STATUS_OK);
    tw_tally_t forged = tw_frame_compare(world, &camera, targets.host_hits, targets.host_coarse, 1u, 0.0f64);
    CHECK(forged.coarse_too_far > 1000u);
    CHECK(forged.unexplained > 1000u);
    tw_targets_free(t, &memory, &targets);
    tw_close(t, &memory);
}

/* The world on the card is what is traced, and not something left over. */
TEST(changing_a_voxel_on_the_card_changes_the_answer) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 4u, 0, 0);
    tw_set(world, 20, 20, 40, 8u);
    tw_set(world, 30, 20, 40, 9u);
    REQUIRE(tw_world_upload(t, &memory, world));
    const traverse_ray_t ray = tw_ray(10, 20, 40, 1.0f32, 0.0f32, 0.0f32);
    traverse_hit_t hit;
    REQUIRE(tw_cast(t, &memory, world, &ray, 1u, &hit));
    tw_check_hit(t, &hit, TRAVERSE_FACE_NEG_X, 9.5f32, 10, 0, 0, 8u);
    /* Takes voxel (20, 20, 40) out of its brick's mask, directly. This world has one section, first in the pool, whose words
     * follow its header. Its first brick holds that voxel alone, since 20 and 30 fall in different bricks. */
    const uint64_t empty = 0u;
    REQUIRE(device_memory_write(&memory, &world->pool, sizeof(world_buffers_section_t), &empty, sizeof(empty)) ==
            GPU_STATUS_OK);
    REQUIRE(tw_cast(t, &memory, world, &ray, 1u, &hit));
    tw_check_hit(t, &hit, TRAVERSE_FACE_NEG_X, 19.5f32, 20, 0, 0, 9u);
    tw_close(t, &memory);
}

/* ---- Stage 01.3's worlds and paths, against the reference ------------------------------------------------------------- */

static const float32_t tw_poses[] = {0.13f32, 0.5f32, 0.87f32};

/* A whole frame, with and without the coarse pass, against the reference, on all seven paths. */
static void tw_check_stress_world(test_context_t *t, bool lattice, uint32_t width, uint32_t height, uint32_t every) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_paths_t paths;
    tw_world_t *const world = tw_stress_world(t, TW_TERRAIN_COLUMNS, lattice, &paths);
    REQUIRE(tw_world_upload(t, &memory, world));
    tw_targets_t targets;
    REQUIRE(tw_targets_make(t, &memory, width, height, &targets));
    tw_tally_t plain_sum = {0}, coarse_sum = {0};
    uint64_t fly_plain_steps = 0u, fly_coarse_steps = 0u;
    for (uint32_t path = 0u; path < TW_PATH_COUNT; path += 1u) {
        uint64_t path_plain = 0u, path_coarse = 0u, path_skip = 0u;
        for (uint32_t pose = 0u; pose < sizeof(tw_poses) / sizeof(tw_poses[0]); pose += 1u) {
            const traverse_camera_t camera = tw_path_camera(world, &paths, path, tw_poses[pose], width, height);
            REQUIRE(tw_frame_run(t, &memory, world, &camera, &targets, false));
            const tw_tally_t plain = tw_frame_compare(world, &camera, targets.host_hits, nullptr, every, 0.0f64);
            tw_tally_add(&plain_sum, &plain);
            path_plain += plain.steps;
            REQUIRE(tw_frame_run(t, &memory, world, &camera, &targets, true));
            const tw_tally_t coarse =
                tw_frame_compare(world, &camera, targets.host_hits, targets.host_coarse, every, 0.0f64);
            tw_tally_add(&coarse_sum, &coarse);
            path_coarse += coarse.steps;
            const uint32_t tile_count = traverse_tiles_across(width) * traverse_tiles_across(height);
            for (uint32_t k = 0u; k < tile_count; k += 1u) {
                path_skip += targets.host_coarse[k] > 0.0f32 ? 1u : 0u;
            }
            /* The check has teeth, on this world and this frame: a kernel a hundredth of a block off cannot pass. */
            if (path == TW_PATH_FLY && pose == 1u) {
                const tw_tally_t wrong =
                    tw_frame_compare(world, &camera, targets.host_hits, targets.host_coarse, every, 0.01f64);
                printf("  a reference 0.01 blocks off: %" PRIu64 " of %" PRIu64 " rays not explained\n",
                       wrong.unexplained, wrong.rays);
                CHECK(wrong.unexplained > wrong.rays / 1000u);
            }
        }
        printf("  %-14s steps seen without coarse %10" PRIu64 ", with %10" PRIu64 "; tiles starting past 0: %" PRIu64
               "\n",
               tw_path_names[path], path_plain, path_coarse, path_skip);
        if (path == TW_PATH_FLY) {
            fly_plain_steps = path_plain;
            fly_coarse_steps = path_coarse;
        }
    }
    printf("  without coarse: %" PRIu64 " rays; differ: hit %" PRIu64 ", voxel %" PRIu64 ", face %" PRIu64
           ", t %" PRIu64 ", value %" PRIu64 "; not explained %" PRIu64 "\n",
           plain_sum.rays, plain_sum.got_differs, plain_sum.voxel_differs, plain_sum.face_differs, plain_sum.t_differs,
           plain_sum.value_differs, plain_sum.unexplained);
    printf("  with coarse:    %" PRIu64 " rays; differ: hit %" PRIu64 ", voxel %" PRIu64 ", face %" PRIu64
           ", t %" PRIu64 ", value %" PRIu64 "; not explained %" PRIu64 "; coarse too far %" PRIu64 "\n",
           coarse_sum.rays, coarse_sum.got_differs, coarse_sum.voxel_differs, coarse_sum.face_differs,
           coarse_sum.t_differs, coarse_sum.value_differs, coarse_sum.unexplained, coarse_sum.coarse_too_far);
    CHECK_EQ_U64(plain_sum.capped + coarse_sum.capped, 0u);
    CHECK_EQ_U64(plain_sum.unexplained, 0u);
    CHECK_EQ_U64(coarse_sum.unexplained, 0u);
    CHECK_EQ_U64(coarse_sum.coarse_too_far, 0u);
    if (!lattice) {
        /* A flight over terrain is where a coarse pass earns its keep. Steps are deterministic, so this does not flake. */
        CHECK(fly_coarse_steps < fly_plain_steps);
    }
    tw_targets_free(t, &memory, &targets);
    tw_close(t, &memory);
}

TEST(terrain_frames_match_the_reference_on_seven_paths) {
    tw_check_stress_world(t, false, 1280u, 720u, 4u);
}

TEST(lattice_frames_match_the_reference_on_seven_paths) {
    tw_check_stress_world(t, true, 1280u, 720u, 4u);
}

/* Stage 01.3's fallback is a lower internal size. Rendering at one must be right, not only cheap. */
TEST(the_fallback_render_sizes_match_the_reference) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_paths_t paths;
    tw_world_t *const world = tw_stress_world(t, TW_TERRAIN_COLUMNS, false, &paths);
    REQUIRE(tw_world_upload(t, &memory, world));
    static const uint32_t sizes[][2] = {{1920u, 1080u}, {1440u, 810u}, {1280u, 720u}, {960u, 540u}};
    tw_targets_t targets;
    REQUIRE(tw_targets_make(t, &memory, 1920u, 1080u, &targets));
    for (uint32_t i = 0u; i < 4u; i += 1u) {
        const traverse_camera_t camera = tw_path_camera(world, &paths, TW_PATH_FLY, 0.5f32, sizes[i][0], sizes[i][1]);
        REQUIRE(tw_frame_run(t, &memory, world, &camera, &targets, true));
        const tw_tally_t tally = tw_frame_compare(world, &camera, targets.host_hits, targets.host_coarse, 4u, 0.0f64);
        CHECK_EQ_U64(tally.unexplained, 0u);
        CHECK_EQ_U64(tally.coarse_too_far, 0u);
    }
    tw_targets_free(t, &memory, &targets);
    tw_close(t, &memory);
}

/* ---- Plan 009 phase 01: counting steps changes nothing, and the counts add up ---------------------------------------------- */

/* A counts block for pixels planes (0 for none), zeroed, with its pixels set. */
static bool tw_counts_make(test_context_t *t, device_memory_t *memory, uint32_t pixels, device_block_t *out) {
    const uint64_t bytes = traverse_counts_bytes(pixels);
    return CHECK_EQ_U64(device_memory_alloc(memory, DEVICE_POOL_FRAMES, bytes, "tw counts", out), GPU_STATUS_OK) &&
           CHECK_EQ_U64(device_memory_zero(memory, out, 0u, bytes), GPU_STATUS_OK) &&
           CHECK_EQ_U64(device_memory_write(memory, out, offsetof(traverse_counts_t, pixels), &pixels, sizeof(pixels)),
                        GPU_STATUS_OK);
}

/* Primary rays with counts, waited for, and their hits and the counts read back. */
static bool tw_frame_counted(test_context_t *t, device_memory_t *memory, const tw_world_t *world,
                             const traverse_camera_t *camera, tw_targets_t *targets, const device_block_t *counts,
                             traverse_counts_t *host_counts) {
    gpu_error_t error = {0};
    gpu_status_t status = traverse_coarse(&error, &world->view, camera, &targets->targets);
    if (status == GPU_STATUS_OK) {
        status = traverse_primary_counted(&error, &world->view, camera, &targets->targets, true, counts);
    }
    if (status == GPU_STATUS_OK) {
        status = traverse_wait(&error);
    }
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&error);
    }
    return CHECK_EQ_U64(status, GPU_STATUS_OK) &&
           CHECK_EQ_U64(device_memory_read(memory, &targets->hits, 0u, targets->host_hits,
                                           traverse_hits_bytes(camera->width, camera->height)),
                        GPU_STATUS_OK) &&
           CHECK_EQ_U64(device_memory_read(memory, counts, 0u, host_counts, sizeof(*host_counts)), GPU_STATUS_OK);
}

/* Every hit of the reference scenes, its voxel, face, t to the bit and value, the same with counts and without: each of the seven
 * paths of both stress worlds. */
TEST(counting_steps_changes_no_hit) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    const uint32_t width = 640u, height = 360u;
    const uint64_t hit_bytes = traverse_hits_bytes(width, height);
    traverse_hit_t *const plain = test_alloc(t, hit_bytes);
    traverse_counts_t *const host_counts = test_alloc(t, sizeof(traverse_counts_t));
    tw_targets_t targets;
    REQUIRE(tw_targets_make(t, &memory, width, height, &targets));
    device_block_t counts = {0};
    REQUIRE(tw_counts_make(t, &memory, width * height, &counts));
    uint64_t frames_same = 0u;
    for (uint32_t lattice = 0u; lattice < 2u; lattice += 1u) {
        tw_paths_t paths;
        tw_world_t *const world = tw_stress_world(t, TW_TERRAIN_COLUMNS, lattice != 0u, &paths);
        REQUIRE(tw_world_upload(t, &memory, world));
        for (uint32_t path = 0u; path < TW_PATH_COUNT; path += 1u) {
            const traverse_camera_t camera = tw_path_camera(world, &paths, path, 0.5f32, width, height);
            REQUIRE(tw_frame_run(t, &memory, world, &camera, &targets, true));
            memcpy(plain, targets.host_hits, hit_bytes);
            REQUIRE(tw_frame_counted(t, &memory, world, &camera, &targets, &counts, host_counts));
            frames_same += CHECK(memcmp(plain, targets.host_hits, hit_bytes) == 0) ? 1u : 0u;
        }
    }
    CHECK_EQ_U64(frames_same, 2u * TW_PATH_COUNT);
    CHECK_EQ_U64(device_memory_free(&memory, &counts), GPU_STATUS_OK);
    tw_targets_free(t, &memory, &targets);
    tw_close(t, &memory);
}

/* Over a 256 x 256 camera, primary steps summed over the bands are the hits' own steps summed, every pixel's plane holds its hit's
 * steps, every ray is one call, and the whole-ray histogram holds every call. No ray reaches the 4095 that info saturates at. */
TEST(primary_band_sums_equal_each_hits_steps) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    const uint32_t side = 256u, pixels = side * side;
    tw_paths_t paths;
    tw_world_t *const world = tw_stress_world(t, TW_TERRAIN_COLUMNS, false, &paths);
    REQUIRE(tw_world_upload(t, &memory, world));
    tw_targets_t targets;
    REQUIRE(tw_targets_make(t, &memory, side, side, &targets));
    device_block_t counts = {0};
    REQUIRE(tw_counts_make(t, &memory, pixels, &counts));
    const uint64_t bytes = traverse_counts_bytes(pixels);
    uint8_t *const host = test_alloc(t, bytes);
    const traverse_camera_t camera = tw_path_camera(world, &paths, TW_PATH_FLY, 0.5f32, side, side);
    REQUIRE(tw_frame_counted(t, &memory, world, &camera, &targets, &counts, (traverse_counts_t *)(void *)host));
    REQUIRE(CHECK_EQ_U64(device_memory_read(&memory, &counts, 0u, host, bytes), GPU_STATUS_OK));
    const traverse_counts_t *const c = (const traverse_counts_t *)(const void *)host;
    const uint16_t *const plane = (const uint16_t *)(const void *)(host + sizeof(traverse_counts_t));
    uint64_t hit_steps = 0u, most = 0u, plane_wrong = 0u;
    for (uint32_t i = 0u; i < pixels; i += 1u) {
        const uint32_t steps = traverse_hit_steps(targets.host_hits[i].info);
        hit_steps += steps;
        most = steps > most ? steps : most;
        plane_wrong += plane[i] != steps ? 1u : 0u;
    }
    uint64_t band_steps = 0u, whole = 0u;
    for (uint32_t b = 0u; b < TRAVERSE_COUNT_BANDS; b += 1u) {
        band_steps += c->steps[TRAVERSE_COUNT_PRIMARY][b];
    }
    for (uint32_t k = 0u; k < TRAVERSE_COUNT_BUCKETS; k += 1u) {
        whole += c->histogram[TRAVERSE_COUNT_PRIMARY][TRAVERSE_COUNT_BANDS][k];
    }
    printf("  %" PRIu64 " primary steps over %u rays, the most %" PRIu64 "\n", hit_steps, pixels, most);
    CHECK(most < (uint64_t)TRAVERSE_INFO_STEPS_MAX);
    CHECK(hit_steps > (uint64_t)pixels);
    CHECK_EQ_U64(band_steps, hit_steps);
    CHECK_EQ_U64(c->calls[TRAVERSE_COUNT_PRIMARY], pixels);
    CHECK_EQ_U64(whole, pixels);
    CHECK_EQ_U64(plane_wrong, 0u);
    for (uint32_t kind = 1u; kind < TRAVERSE_COUNT_KINDS; kind += 1u) {
        CHECK_EQ_U64(c->calls[kind], 0u);
    }
    CHECK_EQ_U64(device_memory_free(&memory, &counts), GPU_STATUS_OK);
    tw_targets_free(t, &memory, &targets);
    tw_close(t, &memory);
}

/* A straight ray over an empty world to a wall 300 blocks off. It steps a column at a time, so it begins a step in every band it crosses,
 * from 0-16 to 256-512, and in none past 512. */
TEST(a_ray_at_300_blocks_counts_into_the_bands_it_crossed) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 24u, 0, 0);
    for (int32_t z = 30; z < 50; z += 1) {
        for (int32_t y = 30; y < 50; y += 1) {
            tw_set(world, 310, y, z, 7u);
        }
    }
    REQUIRE(tw_world_upload(t, &memory, world));
    device_block_t counts = {0}, ray_block = {0}, hit_block = {0};
    REQUIRE(tw_counts_make(t, &memory, 0u, &counts));
    const traverse_ray_t ray = tw_ray(10, 40, 40, 1.0f32, 0.0f32, 0.0f32);
    REQUIRE(CHECK_EQ_U64(device_memory_alloc(&memory, DEVICE_POOL_FRAMES, sizeof(ray), "tw ray", &ray_block),
                         GPU_STATUS_OK));
    REQUIRE(CHECK_EQ_U64(device_memory_alloc(&memory, DEVICE_POOL_FRAMES, sizeof(traverse_hit_t), "tw hit", &hit_block),
                         GPU_STATUS_OK));
    REQUIRE(CHECK_EQ_U64(device_memory_write(&memory, &ray_block, 0u, &ray, sizeof(ray)), GPU_STATUS_OK));
    gpu_error_t error = {0};
    gpu_status_t status = traverse_rays_counted(&error, &world->view, &ray_block, &hit_block, 1u, &counts);
    status = status == GPU_STATUS_OK ? traverse_wait(&error) : status;
    REQUIRE(CHECK_EQ_U64(status, GPU_STATUS_OK));
    traverse_hit_t hit;
    traverse_counts_t *const c = test_alloc(t, sizeof(traverse_counts_t));
    CHECK_EQ_U64(device_memory_read(&memory, &hit_block, 0u, &hit, sizeof(hit)), GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_read(&memory, &counts, 0u, c, sizeof(*c)), GPU_STATUS_OK);
    tw_check_hit(t, &hit, TRAVERSE_FACE_NEG_X, 299.5f32, 300, 0, 0, 7u);
    uint64_t sum = 0u;
    for (uint32_t b = 0u; b < TRAVERSE_COUNT_BANDS; b += 1u) {
        printf("  band from %4u: %" PRIu64 " steps\n", traverse_count_band_low(b), c->steps[TRAVERSE_COUNT_PRIMARY][b]);
        sum += c->steps[TRAVERSE_COUNT_PRIMARY][b];
        if (b <= 4u) {
            CHECK(c->steps[TRAVERSE_COUNT_PRIMARY][b] > 0u);
            CHECK_EQ_U64(c->rays[TRAVERSE_COUNT_PRIMARY][b], 1u);
        } else {
            CHECK_EQ_U64(c->steps[TRAVERSE_COUNT_PRIMARY][b], 0u);
            CHECK_EQ_U64(c->rays[TRAVERSE_COUNT_PRIMARY][b], 0u);
        }
    }
    CHECK_EQ_U64(sum, traverse_hit_steps(hit.info));
    CHECK_EQ_U64(c->calls[TRAVERSE_COUNT_PRIMARY], 1u);
    /* The host's bucket and its least count agree. */
    for (uint32_t steps = 0u; steps < 70000u; steps += 1u) {
        const uint32_t bucket = traverse_count_bucket(steps);
        CHECK(traverse_count_bucket_low(bucket) <= steps);
        if (bucket + 1u < TRAVERSE_COUNT_BUCKETS) {
            CHECK(traverse_count_bucket_low(bucket + 1u) > steps);
        }
    }
    CHECK_EQ_U64(device_memory_free(&memory, &ray_block), GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_free(&memory, &hit_block), GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_free(&memory, &counts), GPU_STATUS_OK);
    tw_close(t, &memory);
}
/* ---- Plan 009 phase 04: t_end --------------------------------------------------------------------------------------------- */

/* A ray with an end it never reaches finds what one with none finds, to the bit, on every path of both stress worlds: so the check the loop
 * does each step, and on each column it enters, changes nothing until it fires. The world's bands set a primary ray's end at d_1, here past
 * any distance in the window. */
TEST(an_endless_t_end_changes_nothing) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    for (uint32_t lattice = 0u; lattice < 2u; lattice += 1u) {
        tw_paths_t paths;
        tw_world_t *const world = tw_stress_world(t, TW_TERRAIN_COLUMNS, lattice != 0u, &paths);
        REQUIRE(tw_world_upload(t, &memory, world));
        tw_targets_t targets;
        REQUIRE(tw_targets_make(t, &memory, 640u, 360u, &targets));
        const uint64_t hit_bytes = traverse_hits_bytes(640u, 360u);
        traverse_hit_t *const endless = test_alloc(t, hit_bytes);
        uint64_t differ = 0u, rays = 0u;
        for (uint32_t path = 0u; path < TW_PATH_COUNT; path += 1u) {
            const traverse_camera_t camera = tw_path_camera(world, &paths, path, 0.5f32, 640u, 360u);
            for (uint32_t coarse = 0u; coarse < 2u; coarse += 1u) {
                world->view.band_base = 0.0f32;
                REQUIRE(tw_frame_run(t, &memory, world, &camera, &targets, coarse != 0u));
                memcpy(endless, targets.host_hits, hit_bytes);
                /* d_1 is 2 million blocks: an end, and never reached. */
                world->view.band_base = 1.0e6f32;
                REQUIRE(tw_frame_run(t, &memory, world, &camera, &targets, coarse != 0u));
                for (uint64_t k = 0u; k < hit_bytes / sizeof(traverse_hit_t); k += 1u) {
                    differ += memcmp(&endless[k], &targets.host_hits[k], sizeof(traverse_hit_t)) != 0 ? 1u : 0u;
                }
                rays += hit_bytes / sizeof(traverse_hit_t);
            }
        }
        world->view.band_base = 0.0f32;
        printf("  %s: %" PRIu64 " of %" PRIu64 " hits differ with an end never reached\n",
               lattice != 0u ? "lattice" : "terrain", differ, rays);
        CHECK_EQ_U64(differ, 0u);
        tw_targets_free(t, &memory, &targets);
        CHECK_EQ_U64(device_memory_free(&memory, &world->table), GPU_STATUS_OK);
        CHECK_EQ_U64(device_memory_free(&memory, &world->pool), GPU_STATUS_OK);
    }
    tw_close(t, &memory);
}

/* An empty world and a wall at 50: an end at 30 stops the ray there and says so (TRAVERSE_FACE_PAST: a miss whose t is the end); an end at 60
 * finds the wall as no end does. A ray with an end entering a column the world does not hold stops where it came in. */
TEST(a_ray_stops_at_its_t_end_and_says_past) {
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_world_t *const world = tw_world_begin(t, 8u, 0, 0);
    for (int32_t z = 30; z < 50; z += 1) {
        for (int32_t y = 30; y < 50; y += 1) {
            tw_set(world, 60, y, z, 7u);
        }
    }
    REQUIRE(tw_world_upload(t, &memory, world));
    traverse_ray_t rays[3] = {tw_ray(10, 40, 40, 1.0f32, 0.0f32, 0.0f32), tw_ray(10, 40, 40, 1.0f32, 0.0f32, 0.0f32),
                              tw_ray(10, 40, 40, 1.0f32, 0.0f32, 0.0f32)};
    rays[0].t_end = 30.0f32;
    rays[1].t_end = 60.0f32;
    traverse_hit_t hits[3];
    REQUIRE(tw_cast(t, &memory, world, rays, 3u, hits));
    CHECK_EQ_U64(traverse_hit_face(hits[0].info), TRAVERSE_FACE_PAST);
    CHECK(traverse_hit_past(&hits[0]));
    CHECK(memcmp(&hits[0].t, &rays[0].t_end, sizeof(float32_t)) == 0);
    tw_check_hit(t, &hits[1], TRAVERSE_FACE_NEG_X, 49.5f32, 50, 0, 0, 7u);
    CHECK(memcmp(&hits[1], &hits[2], sizeof(traverse_hit_t)) == 0);
    CHECK(!traverse_hit_past(&hits[1]));
    /* A miss with no end is no PAST. */
    const traverse_ray_t up = tw_ray(10, 40, 40, 0.0f32, 0.0f32, 1.0f32);
    traverse_hit_t sky;
    REQUIRE(tw_cast(t, &memory, world, &up, 1u, &sky));
    CHECK_EQ_U64(traverse_hit_face(sky.info), TRAVERSE_FACE_MISS);
    CHECK(!traverse_hit_past(&sky));
    /* Column 3, 2 (x 48 to 63, the wall's) taken out of its slot: with an end the ray stops at x = 48, 37.5 along; with none it passes. */
    const int32_t elsewhere = 3 + 8;
    const uint64_t slot = 2u * 8u + 3u;
    REQUIRE(device_memory_write(&memory, &world->table,
                                slot * sizeof(world_buffers_column_t) + offsetof(world_buffers_column_t, chunk_x),
                                &elsewhere, sizeof(elsewhere)) == GPU_STATUS_OK);
    REQUIRE(tw_cast(t, &memory, world, rays, 3u, hits));
    CHECK(traverse_hit_past(&hits[1]));
    CHECK_NEAR(hits[1].t, 37.5f32, 1e-4f32);
    CHECK_EQ_U64(traverse_hit_face(hits[2].info), TRAVERSE_FACE_MISS);
    CHECK(!traverse_hit_past(&hits[2]));
    tw_close(t, &memory);
}

/* ---- Timing, on request. A number on a shared card proves nothing in a test, so it is printed and not compared. ----------- */

static int32_t tw_compare_i64(const void *a, const void *b) {
    const int64_t left = *(const int64_t *)a, right = *(const int64_t *)b;
    return left < right ? -1 : (left > right ? 1 : 0);
}

typedef struct tw_timing_t {
    float64_t mean_ms; /* 0 */
    float64_t p99_ms;  /* 8 */
    float64_t max_ms;  /* 16 */
} tw_timing_t;
static_assert(sizeof(tw_timing_t) == 24);

typedef enum tw_passes_t : uint32_t { TW_PASS_PRIMARY = 0, TW_PASS_COARSE = 1, TW_PASS_BOTH = 2 } tw_passes_t;
static_assert(sizeof(tw_passes_t) == 4);

static tw_timing_t tw_time_path(test_context_t *t, const tw_world_t *world, const tw_paths_t *paths,
                                const tw_targets_t *targets, uint32_t path, uint32_t width, uint32_t height,
                                tw_passes_t passes, uint32_t frames, int64_t *samples) {
    gpu_error_t error = {0};
    const uint32_t warm = frames / 10u;
    for (uint32_t i = 0u; i < warm + frames; i += 1u) {
        const float32_t a = (float32_t)(i >= warm ? i - warm : 0u) / (float32_t)frames;
        const traverse_camera_t camera = tw_path_camera(world, paths, path, a, width, height);
        const int64_t start = time_now_ns();
        gpu_status_t status = GPU_STATUS_OK;
        if (passes != TW_PASS_PRIMARY) {
            status = traverse_coarse(&error, &world->view, &camera, &targets->targets);
        }
        if (status == GPU_STATUS_OK && passes != TW_PASS_COARSE) {
            status = traverse_primary(&error, &world->view, &camera, &targets->targets, passes == TW_PASS_BOTH);
        }
        status = status == GPU_STATUS_OK ? traverse_wait(&error) : status;
        const int64_t end = time_now_ns();
        CHECK_EQ_U64(status, GPU_STATUS_OK);
        if (i >= warm) {
            samples[i - warm] = end - start;
        }
    }
    qsort(samples, frames, sizeof(int64_t), tw_compare_i64);
    int64_t total = 0;
    for (uint32_t i = 0u; i < frames; i += 1u) {
        total += samples[i];
    }
    return (tw_timing_t){.mean_ms = time_ns_to_ms(total) / (float64_t)frames,
                         .p99_ms = time_ns_to_ms(samples[(frames * 99u) / 100u]),
                         .max_ms = time_ns_to_ms(samples[frames - 1u])};
}

/* MC_TRAVERSE_MEASURE=1 TEST_FILTER=measure bin/tests/cuda_traverse: Stage 01.3's worlds at R = 32. */
static void tw_measure_world(test_context_t *t, bool lattice) {
    if (getenv("MC_TRAVERSE_MEASURE") == nullptr) {
        printf("  skipped: set MC_TRAVERSE_MEASURE=1\n");
        return;
    }
    device_memory_t memory;
    REQUIRE(tw_open(t, &memory));
    tw_paths_t paths;
    tw_world_t *const world = tw_stress_world(t, TW_MEASURE_COLUMNS, lattice, &paths);
    REQUIRE(tw_world_upload(t, &memory, world));
    printf("  world: %" PRIu64 " sections, %" PRIu64 " bricks, %" PRIu64 " voxels, %.1f MiB\n", world->section_count,
           world->brick_count, world->voxel_count,
           (libc_double_t)(world->pool_bytes + (uint64_t)TW_MEASURE_COLUMNS * TW_MEASURE_COLUMNS * 16u) /
               (libc_double_t)1048576u);
    enum : uint32_t { TW_MEASURE_FRAMES = 200u };
    int64_t *const samples = test_alloc(t, TW_MEASURE_FRAMES * sizeof(int64_t));
    tw_targets_t targets;
    REQUIRE(tw_targets_make(t, &memory, 1920u, 1080u, &targets));
    printf("  | Path | Primary alone mean | p99 | Coarse alone mean | Both mean | p99 | Max | Steps per ray, without | "
           "with |\n");
    printf("  |---|---|---|---|---|---|---|---|---|\n");
    float64_t worst_plain = 0.0f64, worst_both = 0.0f64;
    for (uint32_t path = 0u; path < TW_PATH_COUNT; path += 1u) {
        const tw_timing_t plain =
            tw_time_path(t, world, &paths, &targets, path, 1920u, 1080u, TW_PASS_PRIMARY, TW_MEASURE_FRAMES, samples);
        const tw_timing_t coarse =
            tw_time_path(t, world, &paths, &targets, path, 1920u, 1080u, TW_PASS_COARSE, TW_MEASURE_FRAMES, samples);
        const tw_timing_t both =
            tw_time_path(t, world, &paths, &targets, path, 1920u, 1080u, TW_PASS_BOTH, TW_MEASURE_FRAMES, samples);
        const traverse_camera_t camera = tw_path_camera(world, &paths, path, 0.5f32, 1920u, 1080u);
        REQUIRE(tw_frame_run(t, &memory, world, &camera, &targets, false));
        uint64_t steps_plain = 0u, steps_coarse = 0u;
        const uint64_t pixels = 1920u * 1080u;
        for (uint64_t i = 0u; i < pixels; i += 1u) {
            steps_plain += traverse_hit_steps(targets.host_hits[i].info);
        }
        REQUIRE(tw_frame_run(t, &memory, world, &camera, &targets, true));
        for (uint64_t i = 0u; i < pixels; i += 1u) {
            steps_coarse += traverse_hit_steps(targets.host_hits[i].info);
        }
        printf("  | %s | %.2f | %.2f | %.3f | %.2f | %.2f | %.2f | %.1f | %.1f |\n", tw_path_names[path],
               (libc_double_t)plain.mean_ms, (libc_double_t)plain.p99_ms, (libc_double_t)coarse.mean_ms,
               (libc_double_t)both.mean_ms, (libc_double_t)both.p99_ms, (libc_double_t)both.max_ms,
               (libc_double_t)steps_plain / (libc_double_t)pixels, (libc_double_t)steps_coarse / (libc_double_t)pixels);
        worst_plain = plain.p99_ms > worst_plain ? plain.p99_ms : worst_plain;
        worst_both = both.p99_ms > worst_both ? both.p99_ms : worst_both;
    }
    printf("  worst p99 at 1920x1080: primary alone %.2f ms, coarse and primary %.2f ms\n", (libc_double_t)worst_plain,
           (libc_double_t)worst_both);
    static const uint32_t sizes[][2] = {{1440u, 810u}, {1280u, 720u}, {960u, 540u}};
    for (uint32_t s = 0u; s < 3u; s += 1u) {
        float64_t mean_low = 1e30f64, mean_high = 0.0f64, worst = 0.0f64;
        for (uint32_t path = 0u; path < TW_PATH_COUNT; path += 1u) {
            const tw_timing_t both = tw_time_path(t, world, &paths, &targets, path, sizes[s][0], sizes[s][1],
                                                  TW_PASS_BOTH, TW_MEASURE_FRAMES / 2u, samples);
            mean_low = both.mean_ms < mean_low ? both.mean_ms : mean_low;
            mean_high = both.mean_ms > mean_high ? both.mean_ms : mean_high;
            worst = both.p99_ms > worst ? both.p99_ms : worst;
        }
        printf("  %ux%u, coarse and primary: mean %.2f - %.2f ms, worst p99 %.2f ms\n", sizes[s][0], sizes[s][1],
               (libc_double_t)mean_low, (libc_double_t)mean_high, (libc_double_t)worst);
    }
    tw_targets_free(t, &memory, &targets);
    tw_close(t, &memory);
}

TEST(measure_terrain) {
    tw_measure_world(t, false);
}

TEST(measure_lattice) {
    tw_measure_world(t, true);
}

TEST_MAIN()
