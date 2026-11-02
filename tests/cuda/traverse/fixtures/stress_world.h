/* Stage 01.3's terrain and lattice worlds, and its seven camera paths, for ctest cuda_traverse. See ../README.md. Included
 * once, by ../index.c. */
#ifndef MC_TESTS_CUDA_TRAVERSE_FIXTURES_STRESS_WORLD_H
#define MC_TESTS_CUDA_TRAVERSE_FIXTURES_STRESS_WORLD_H

#include "cuda/traverse/fixtures/world.h"

#include <pthread.h>

/* ---- The stress worlds of Stage 01.3, with its seven camera paths --------------------------------------------------------- */

static float32_t tw_fade(float32_t x) {
    return x * x * (3.0f32 - 2.0f32 * x);
}

static float32_t tw_mix(float32_t a, float32_t b, float32_t x) {
    return a + (b - a) * x;
}

static float32_t tw_noise_value(int32_t x, int32_t y, int32_t z) {
    return (float32_t)(tw_hash(x, y, z) & 0xFFFFFFu) / 8388607.5f32 - 1.0f32;
}

static float32_t tw_noise3(float32_t x, float32_t y, float32_t z) {
    const float32_t fx = floorf(x), fy = floorf(y), fz = floorf(z);
    const int32_t ix = (int32_t)fx, iy = (int32_t)fy, iz = (int32_t)fz;
    const float32_t u = tw_fade(x - fx), v = tw_fade(y - fy), w = tw_fade(z - fz);
    const float32_t low = tw_mix(tw_mix(tw_noise_value(ix, iy, iz), tw_noise_value(ix + 1, iy, iz), u),
                                 tw_mix(tw_noise_value(ix, iy + 1, iz), tw_noise_value(ix + 1, iy + 1, iz), u), v);
    const float32_t high =
        tw_mix(tw_mix(tw_noise_value(ix, iy, iz + 1), tw_noise_value(ix + 1, iy, iz + 1), u),
               tw_mix(tw_noise_value(ix, iy + 1, iz + 1), tw_noise_value(ix + 1, iy + 1, iz + 1), u), v);
    return tw_mix(low, high, w);
}

static float32_t tw_noise2(float32_t x, float32_t y, int32_t seed) {
    const float32_t fx = floorf(x), fy = floorf(y);
    const int32_t ix = (int32_t)fx, iy = (int32_t)fy;
    const float32_t u = tw_fade(x - fx), v = tw_fade(y - fy);
    return tw_mix(tw_mix(tw_noise_value(ix, iy, seed), tw_noise_value(ix + 1, iy, seed), u),
                  tw_mix(tw_noise_value(ix, iy + 1, seed), tw_noise_value(ix + 1, iy + 1, seed), u), v);
}

static float32_t tw_forest(float32_t x, float32_t y) {
    return tw_noise2(x / 180.0f32, y / 180.0f32, 77);
}

static int32_t tw_ground(float32_t x, float32_t y) {
    float32_t h = 12.0f32;
    h += 40.0f32 * tw_noise2(x / 500.0f32, y / 500.0f32, 1);
    h += 12.0f32 * tw_noise2(x / 140.0f32, y / 140.0f32, 2);
    h += 4.0f32 * tw_noise2(x / 40.0f32, y / 40.0f32, 3);
    h += 1.0f32 * tw_noise2(x / 11.0f32, y / 11.0f32, 4);
    float32_t m = tw_noise2(x / 330.0f32, y / 330.0f32, 5) / 0.6f32;
    m = m < 0.0f32 ? 0.0f32 : (m > 1.0f32 ? 1.0f32 : m);
    m = tw_fade(m);
    const float32_t r = 1.0f32 - fabsf(tw_noise2(x / 170.0f32, y / 170.0f32, 6));
    h += m * r * r * 150.0f32;
    return (int32_t)h;
}

enum : uint32_t {
    TW_M_STONE = 1u,
    TW_M_DIRT = 2u,
    TW_M_GRASS = 3u,
    TW_M_SAND = 4u,
    TW_M_WATER = 5u,
    TW_M_WOOD = 6u,
    TW_M_LEAVES = 7u,
    TW_M_ORE = 8u,
    TW_M_SNOW = 9u,
};
enum : int32_t { TW_SEA = 256 };

typedef struct tw_fill_job_t {
    tw_world_t *world; /* 0 */
    int16_t *heights;  /* 8 */
    uint32_t start;    /* 16 */
    uint8_t lattice;   /* 20 */
    uint8_t _pad0[3];  /* 21 */
} tw_fill_job_t;
static_assert(sizeof(tw_fill_job_t) == 24);

static void *tw_fill_rows(void *argument) {
    tw_fill_job_t *const job = argument;
    tw_world_t *const world = job->world;
    const uint32_t width = world->width;
    for (uint32_t y = job->start; y < width; y += TW_THREADS) {
        for (uint32_t x = 0u; x < width; x += 1u) {
            if (job->lattice != 0u) {
                /* Isolated voxels every 9 blocks, so no empty section or brick helps. */
                job->heights[(uint64_t)y * width + x] = 80;
                if (x % 9u == 0u && y % 9u == 0u) {
                    for (uint32_t z = 0u; z < 336u; z += 9u) {
                        world->dense[tw_index(world, x, y, z)] = (uint8_t)TW_M_STONE;
                    }
                }
                continue;
            }
            const int32_t h = tw_ground((float32_t)x, (float32_t)y);
            job->heights[(uint64_t)y * width + x] = (int16_t)h;
            const int32_t top = h + TW_SEA;
            for (int32_t z = 0; z <= top && z < (int32_t)TW_DENSE_HEIGHT; z += 1) {
                uint32_t m = TW_M_STONE;
                const int32_t depth = top - z;
                if (depth == 0) {
                    m = h <= 2 ? TW_M_SAND : (h > 150 ? TW_M_SNOW : TW_M_GRASS);
                } else if (depth < 4) {
                    m = h <= 2 ? TW_M_SAND : TW_M_DIRT;
                }
                const float32_t fx = (float32_t)x, fy = (float32_t)y, fz = (float32_t)z;
                const float32_t n1 = tw_noise3(fx / 34.0f32, fy / 34.0f32, fz / 22.0f32);
                const float32_t n2 = tw_noise3(fx / 34.0f32 + 91.0f32, fy / 34.0f32 + 17.0f32, fz / 22.0f32 + 53.0f32);
                bool carve = fabsf(n1) < 0.06f32 && fabsf(n2) < 0.06f32;
                if (!carve && depth > 6) {
                    carve = tw_noise3(fx / 60.0f32 + 7.0f32, fy / 60.0f32, fz / 30.0f32) > 0.55f32;
                }
                if (carve && z > 2 && (h > 2 || depth > 6)) {
                    m = 0u;
                }
                if (m == TW_M_STONE && (tw_hash((int32_t)x >> 3, (int32_t)y >> 3, z >> 3) % 40u) == 0u &&
                    ((x | y | (uint32_t)z) & 6u) == 0u) {
                    m = TW_M_ORE;
                }
                world->dense[tw_index(world, x, y, (uint32_t)z)] = (uint8_t)m;
            }
            for (int32_t z = top + 1; z <= TW_SEA; z += 1) {
                world->dense[tw_index(world, x, y, (uint32_t)z)] = (uint8_t)TW_M_WATER;
            }
        }
    }
    return nullptr;
}

static uint8_t tw_material_dense(const tw_world_t *world, uint32_t x, uint32_t y, int32_t z) {
    return world->dense[tw_index(world, x, y, (uint32_t)z)];
}

static void tw_plant_trees(tw_world_t *world, const int16_t *heights) {
    const uint32_t width = world->width;
    for (uint32_t gy = 4u; gy + 12u < width; gy += 7u) {
        for (uint32_t gx = 4u; gx + 12u < width; gx += 7u) {
            const uint32_t r = tw_hash((int32_t)gx, (int32_t)gy, 999);
            const uint32_t x = gx + (r & 3u), y = gy + ((r >> 2) & 3u);
            const bool forest = tw_forest((float32_t)x, (float32_t)y) > 0.0f32;
            if (!forest && (r >> 8) % 12u != 0u) {
                continue;
            }
            const int32_t h = heights[(uint64_t)y * width + x];
            if (h <= 2 || h > 130) {
                continue;
            }
            const int32_t base = h + TW_SEA;
            if (tw_material_dense(world, x, y, base) != TW_M_GRASS) {
                continue;
            }
            const int32_t tall = 5 + (int32_t)((r >> 16) & 3u);
            for (int32_t dz = -2; dz <= 2; dz += 1) {
                for (int32_t dy = -3; dy <= 3; dy += 1) {
                    for (int32_t dx = -3; dx <= 3; dx += 1) {
                        if (dx * dx + dy * dy + 2 * dz * dz > 10) {
                            continue;
                        }
                        const uint32_t lx = (uint32_t)((int32_t)x + dx), ly = (uint32_t)((int32_t)y + dy);
                        const int32_t lz = base + tall + dz;
                        /* Holes make the canopy porous, the expensive case for a ray. */
                        if (tw_hash((int32_t)lx, (int32_t)ly, lz) % 4u == 0u) {
                            continue;
                        }
                        if (tw_material_dense(world, lx, ly, lz) == 0u) {
                            world->dense[tw_index(world, lx, ly, (uint32_t)lz)] = (uint8_t)TW_M_LEAVES;
                        }
                    }
                }
            }
            for (int32_t z = base + 1; z < base + tall; z += 1) {
                world->dense[tw_index(world, x, y, (uint32_t)z)] = (uint8_t)TW_M_WOOD;
            }
        }
    }
}

/* Where the paths go: found in the world, as Stage 01.3 found them. In dense coordinates. */
typedef struct tw_paths_t {
    float32_t ridge[3];              /* 0 */
    float32_t forest[2];             /* 12 */
    float32_t cave[3];               /* 20 */
    const int16_t *borrowed_heights; /* 32 */
} tw_paths_t;
static_assert(sizeof(tw_paths_t) == 40);

static tw_world_t *tw_stress_world(test_context_t *t, uint32_t columns_wide, bool lattice, tw_paths_t *paths) {
    /* Below zero, off a table wrap: a game's window is anywhere. */
    tw_world_t *const world = tw_world_begin(t, columns_wide, -20, 7);
    const uint32_t width = world->width;
    int16_t *const heights = test_alloc(t, (uint64_t)width * width * sizeof(int16_t));
    tw_fill_job_t jobs[TW_THREADS];
    pthread_t threads[TW_THREADS];
    for (uint32_t i = 0u; i < TW_THREADS; i += 1u) {
        jobs[i] = (tw_fill_job_t){.world = world, .heights = heights, .start = i, .lattice = lattice ? 1u : 0u};
        if (pthread_create(&threads[i], nullptr, tw_fill_rows, &jobs[i]) != 0) {
            perror("pthread_create");
            abort();
        }
    }
    for (uint32_t i = 0u; i < TW_THREADS; i += 1u) {
        pthread_join(threads[i], nullptr);
    }
    if (!lattice) {
        tw_plant_trees(world, heights);
    }
    *paths = (tw_paths_t){.borrowed_heights = heights};
    const uint32_t centre = width / 2u, span = 200u;
    int32_t best = -1000;
    bool got_cave = false;
    float32_t best_forest = -2.0f32;
    for (uint32_t y = centre - span; y < centre + span; y += 4u) {
        for (uint32_t x = centre - span; x < centre + span; x += 4u) {
            const int32_t h = heights[(uint64_t)y * width + x];
            if (h > best) {
                best = h;
                paths->ridge[0] = (float32_t)x + 0.5f32;
                paths->ridge[1] = (float32_t)y + 0.5f32;
            }
            const float32_t forest = tw_forest((float32_t)x, (float32_t)y);
            if (forest > best_forest && h > 3 && h < 120) {
                best_forest = forest;
                paths->forest[0] = (float32_t)x;
                paths->forest[1] = (float32_t)y;
            }
            if (lattice) {
                got_cave = true;
                paths->cave[0] = (float32_t)centre + 4.5f32;
                paths->cave[1] = (float32_t)centre + 4.5f32;
                paths->cave[2] = 200.5f32;
            } else if (!got_cave && h > 10) {
                for (int32_t z = h + TW_SEA - 20; z > 40; z -= 3) {
                    bool room = true;
                    for (int32_t k = -3; k <= 3 && room; k += 1) {
                        room = tw_material_dense(world, x + (uint32_t)(k + 3), y, z + k) == 0u &&
                               tw_material_dense(world, x, y + (uint32_t)(k + 3), z + k) == 0u;
                    }
                    if (room) {
                        paths->cave[0] = (float32_t)x + 3.0f32;
                        paths->cave[1] = (float32_t)y + 3.0f32;
                        paths->cave[2] = (float32_t)z + 0.5f32;
                        got_cave = true;
                        break;
                    }
                }
            }
        }
    }
    int32_t top = -1000;
    for (int32_t dy = -6; dy <= 6; dy += 1) {
        for (int32_t dx = -6; dx <= 6; dx += 1) {
            const uint64_t row = (uint64_t)((int32_t)paths->ridge[1] + dy);
            const uint64_t cell = (uint64_t)((int32_t)paths->ridge[0] + dx);
            top = heights[row * width + cell] > top ? heights[row * width + cell] : top;
        }
    }
    paths->ridge[2] = (float32_t)top + 3.5f32 + (float32_t)TW_SEA;
    if (!got_cave) {
        fprintf(stderr, "no cave in this world\n");
        abort();
    }
    return world;
}

static float32_t tw_surface(const tw_world_t *world, const tw_paths_t *paths, float32_t x, float32_t y) {
    const int32_t ix = (int32_t)x, iy = (int32_t)y;
    int32_t h = TW_SEA;
    for (int32_t dy = -2; dy <= 2; dy += 1) {
        for (int32_t dx = -2; dx <= 2; dx += 1) {
            const int32_t g =
                paths->borrowed_heights[(uint64_t)(iy + dy) * world->width + (uint64_t)(ix + dx)] + TW_SEA;
            h = g > h ? g : h;
        }
    }
    float32_t z = (float32_t)h + 2.7f32;
    while (tw_material_dense(world, (uint32_t)ix, (uint32_t)iy, (int32_t)z) != 0u) {
        z += 1.0f32;
    }
    return z;
}

static const char *const tw_path_names[TW_PATH_COUNT] = {"walk_horizon", "flyover_high", "ridge_grazing", "spin_360",
                                                         "forest_floor", "canopy_skim",  "cave_interior"};
enum : uint32_t { TW_PATH_FLY = 1u };

/* tan(35 degrees), Stage 01.3's field of view. */
#define TW_HALF_FOV 0.70020754f32

/* The seven paths of Stage 01.3, at a from 0 to 1. */
static traverse_camera_t tw_path_camera(const tw_world_t *world, const tw_paths_t *paths, uint32_t path, float32_t a,
                                        uint32_t width, uint32_t height) {
    const float32_t cx = (float32_t)(world->width / 2u), cy = cx;
    float32_t eye[3] = {paths->cave[0], paths->cave[1], paths->cave[2]};
    float32_t yaw = a * 2.0f32 * TW_PI, pitch = -0.1f32;
    switch (path) {
    case 0u:
        eye[0] = cx - 120.0f32 + 240.0f32 * a;
        eye[1] = cy + 30.0f32 * sinf(a * 6.0f32);
        eye[2] = tw_surface(world, paths, eye[0], eye[1]);
        yaw = 0.15f32 * sinf(a * 9.0f32);
        pitch = -0.02f32;
        break;
    case TW_PATH_FLY:
        eye[0] = cx - 150.0f32 + 300.0f32 * a;
        eye[1] = cy - 100.0f32 + 200.0f32 * a;
        eye[2] = 256.0f32 + 170.0f32;
        yaw = 0.6f32;
        pitch = -0.45f32;
        break;
    case 2u:
        eye[0] = paths->ridge[0];
        eye[1] = paths->ridge[1];
        eye[2] = paths->ridge[2];
        pitch = -0.06f32;
        break;
    case 3u:
        eye[0] = cx;
        eye[1] = cy;
        eye[2] = tw_surface(world, paths, cx, cy);
        pitch = -0.03f32;
        break;
    case 4u:
        eye[0] = paths->forest[0] + 40.0f32 * a;
        eye[1] = paths->forest[1] + 12.0f32 * a;
        eye[2] = tw_surface(world, paths, eye[0], eye[1]);
        yaw = a * 2.0f32;
        pitch = 0.05f32;
        break;
    case 5u:
        eye[0] = paths->forest[0] - 60.0f32 + 120.0f32 * a;
        eye[1] = paths->forest[1] + 0.37f32;
        eye[2] = tw_surface(world, paths, paths->forest[0], paths->forest[1]) + 11.3f32;
        yaw = a * 6.0f32;
        pitch = -0.03f32;
        break;
    default:
        break;
    }
    return tw_camera(world, eye, yaw, pitch, TW_HALF_FOV, width, height);
}

#endif
