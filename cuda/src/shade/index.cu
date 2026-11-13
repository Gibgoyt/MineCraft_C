/* See README.md. One colour per pixel of the hits: from the near surface to the far, the texture of each face a ray meets, at a mip
 * chosen by distance, under that face's brightness, the light stored in its voxel and what its one sun ray saw, for as far as what
 * it meets lets the ray on. Water reflects one ray, and takes colour by how far a ray goes in it. */
#include "shade/index.hu"

#include <math.h>

/* A texel in linear light, with what shows through a part-covered one in its place, for a ray that stops on it. */
static __device__ __forceinline__ void shade_texel_colour(const shade_texture_t *texture, const uint8_t *texel,
                                                          float32_t out[3]) {
    const uint32_t alpha = texel[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float32_t seen = shade_srgb_byte(texel[i]);
        if (alpha == SHADE_ALPHA_OPAQUE) {
            out[i] = seen;
        } else {
            const float32_t behind = shade_srgb_byte(texture->mean_rgba[i]);
            const float32_t cover = (float32_t)alpha / 255.0F;
            out[i] = behind + (seen - behind) * cover;
        }
    }
}

/* How much of the sun reaches this point of a surface, from 0 to 1: the one sun ray. It starts from the surface's own voxel, so it
 * is as exact far from the eye as near it, and goes on through what is see-through as a primary ray does, with a part-covered
 * texel taking its share, and a body of water by how much of it there is. from_t, how far off the surface is seen, chooses the mip
 * of whatever is in the way, so that a far tree's shadow has the same holes as its leaves. */
static __device__ __forceinline__ float32_t shade_sun_seen(const shade_context_t *context, const traverse_hit_t *from,
                                                           const shade_surface_t *surface, traverse_counts_t *counts,
                                                           uint32_t pixel) {
    const bool see_through = (context->features & SHADE_FEATURE_TRANSPARENCY) != 0u;
    const bool tint = (context->features & SHADE_FEATURE_WATER_TINT) != 0u;
    traverse_trace_ray_t ray;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        ray.eye_cell[i] = context->camera->eye_cell[i] + (int32_t)from->offset[i];
        ray.eye_frac[i] = surface->local[i];
        ray.direction[i] = traverse_guard(context->sun->direction[i]);
    }
    ray.eye_cell[2] -= TRAVERSE_Z_BASE;
    const float32_t bias = shade_sun_bias(from->t);
    ray.eye_frac[surface->face >> 1] += (surface->face & 1u) != 0u ? bias : -bias;
    ray.t_start = 0.0F;
    ray.resume = 1u;
    ray.t_end = TRAVERSE_T_END_NONE;
    float32_t seen = 1.0F;
    uint32_t last_material = SHADE_NO_MATERIAL;
    int16_t last_offset[3] = {0, 0, 0};
    for (uint32_t layer = 0u; layer < SHADE_SUN_LAYERS; layer += 1u) {
        traverse_hit_t hit;
        traverse_tally_t tally;
        traverse_trace_counted(
            context->view, &ray, nullptr, &hit,
            traverse_tally_for(&tally, counts, TRAVERSE_COUNT_SUN, pixel, TRAVERSE_COUNT_NO_LEVEL, 1.0F));
        const uint32_t face = (uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK;
        if (face == TRAVERSE_FACE_MISS) {
            return seen;
        }
        if ((context->features & SHADE_FEATURE_ALPHA) == 0u) {
            return 0.0F;
        }
        shade_surface_t met;
        shade_surface_read(context, ray.eye_frac, ray.direction, &hit, from->t, &met);
        if (met.texture == nullptr) {
            return 0.0F;
        }
        ray.t_start = hit.t;
        if (see_through && met.material == context->water_material) {
            shade_water_walk_t walk;
            shade_water_through(context->view, &ray, &hit, met.material,
                                tint ? SHADE_WATER_SEEN_BLOCKS : TRAVERSE_COARSE_SKY, nullptr, &walk);
            if (walk.cut_short != 0u) {
                return 0.0F;
            }
            if (tint) {
                seen *= expf(-SHADE_WATER_ABSORB_SUN * (walk.out - hit.t));
            }
            ray.t_start = walk.last_in;
            last_material = met.material;
            last_offset[0] = walk.last[0];
            last_offset[1] = walk.last[1];
            last_offset[2] = walk.last[2];
            continue;
        }
        if (see_through && shade_own_kind_behind(last_material, last_offset, met.material, hit.offset, face)) {
            last_offset[0] = hit.offset[0];
            last_offset[1] = hit.offset[1];
            last_offset[2] = hit.offset[2];
            continue;
        }
        const uint32_t alpha = met.texel[3];
        if (alpha == SHADE_ALPHA_OPAQUE) {
            return 0.0F;
        }
        seen *= 1.0F - (float32_t)alpha / 255.0F;
        if (alpha != 0u || met.material == context->glass_material) {
            last_material = met.material;
            last_offset[0] = hit.offset[0];
            last_offset[1] = hit.offset[1];
            last_offset[2] = hit.offset[2];
        } else {
            last_material = SHADE_NO_MATERIAL;
        }
    }
    return 0.0F;
}

/* The light on a surface, with its one sun ray if it wants one. visible is what AO left of the light stored in its voxel. */
static __device__ __forceinline__ void shade_surface_lit(const shade_context_t *context, const traverse_hit_t *hit,
                                                         const shade_surface_t *surface, float32_t visible,
                                                         float32_t light[3], traverse_counts_t *counts,
                                                         uint32_t pixel) {
    float32_t sun_seen = 1.0F;
    const shade_sun_t *const sun = context->sun;
    if ((context->features & SHADE_FEATURE_SHADOW) != 0u &&
        shade_sun_towards(sun, surface->face, surface->stored) > 0.0F &&
        sun->direct[0] + sun->direct[1] + sun->direct[2] > 0.0F) {
        sun_seen = shade_sun_seen(context, hit, surface, counts, pixel);
    }
    shade_surface_light_near(sun, surface->face, surface->stored, sun_seen, visible, light);
}

/* One AO ray off a surface: how much of what it looks at is open, from 0 to 1. It looks only SHADE_AO_REACH far, by a cap on steps
 * that always gets that far. It goes on through what is see-through as a sun ray does, and water takes nothing: a lake's bed is not
 * in a corner for having a lake on it. */
static __device__ __forceinline__ float32_t shade_ao_ray(const shade_context_t *context, const traverse_hit_t *from,
                                                         const shade_surface_t *surface, const float32_t direction[3],
                                                         traverse_counts_t *counts, uint32_t pixel) {
    const bool see_through = (context->features & SHADE_FEATURE_TRANSPARENCY) != 0u;
    traverse_view_t view = *context->view;
    view.max_steps = SHADE_AO_STEPS;
    traverse_trace_ray_t ray;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        ray.eye_cell[i] = context->camera->eye_cell[i] + (int32_t)from->offset[i];
        ray.eye_frac[i] = surface->local[i];
        ray.direction[i] = traverse_guard(direction[i]);
    }
    ray.eye_cell[2] -= TRAVERSE_Z_BASE;
    const float32_t bias = shade_sun_bias(from->t);
    ray.eye_frac[surface->face >> 1] += (surface->face & 1u) != 0u ? bias : -bias;
    ray.t_start = 0.0F;
    ray.resume = 1u;
    ray.t_end = TRAVERSE_T_END_NONE;
    float32_t visible = 1.0F;
    uint32_t last_material = SHADE_NO_MATERIAL;
    int16_t last_offset[3] = {0, 0, 0};
    for (uint32_t layer = 0u; layer < SHADE_AO_LAYERS; layer += 1u) {
        traverse_hit_t hit;
        traverse_tally_t tally;
        traverse_trace_counted(
            &view, &ray, nullptr, &hit,
            traverse_tally_for(&tally, counts, TRAVERSE_COUNT_AO, pixel, TRAVERSE_COUNT_NO_LEVEL, 1.0F));
        const uint32_t face = (uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK;
        if (face == TRAVERSE_FACE_MISS || hit.t >= SHADE_AO_REACH) {
            return visible;
        }
        const float32_t near_part = shade_ao_near(hit.t);
        if ((context->features & SHADE_FEATURE_ALPHA) == 0u) {
            return visible * (1.0F - near_part);
        }
        shade_surface_t met;
        shade_surface_read(context, ray.eye_frac, ray.direction, &hit, from->t + hit.t, &met);
        if (met.texture == nullptr) {
            return visible * (1.0F - near_part);
        }
        ray.t_start = hit.t;
        if (see_through && met.material == context->water_material) {
            shade_water_walk_t walk;
            shade_water_through(context->view, &ray, &hit, met.material, SHADE_AO_REACH - hit.t, nullptr, &walk);
            if (walk.cut_short != 0u) {
                return visible;
            }
            ray.t_start = walk.last_in;
            last_material = met.material;
            last_offset[0] = walk.last[0];
            last_offset[1] = walk.last[1];
            last_offset[2] = walk.last[2];
            continue;
        }
        if (see_through && shade_own_kind_behind(last_material, last_offset, met.material, hit.offset, face)) {
            last_offset[0] = hit.offset[0];
            last_offset[1] = hit.offset[1];
            last_offset[2] = hit.offset[2];
            continue;
        }
        const uint32_t alpha = met.texel[3];
        if (alpha == SHADE_ALPHA_OPAQUE) {
            return visible * (1.0F - near_part);
        }
        visible *= 1.0F - near_part * (float32_t)alpha / 255.0F;
        if (alpha != 0u || met.material == context->glass_material) {
            last_material = met.material;
            last_offset[0] = hit.offset[0];
            last_offset[1] = hit.offset[1];
            last_offset[2] = hit.offset[2];
        } else {
            last_material = SHADE_NO_MATERIAL;
        }
    }
    return visible;
}

/* Plan 008 phase 04: what the AO cache and the corner estimate need of a frame. */
typedef struct shade_ao_cache_args_t {
    uint64_t *words; /* 0: SHADE_AO_CACHE_ENTRIES entries, or nullptr for no cache */
    const uint32_t
        *stops; /* 8: traverse_stops_t's words, the blocks opaque on every face, or nullptr: then no corner estimate */
} shade_ao_cache_args_t;
static_assert(sizeof(shade_ao_cache_args_t) == 16);

/* Plan 008 phase 04: how far from a section's border a face's open cell may be and still be darkened by a block across it: SHADE_AO_REACH, and a
 * cell for where on the face. */
enum : int32_t { SHADE_AO_REVISION_NEAR = 4 };
static_assert((float32_t)SHADE_AO_REVISION_NEAR >= SHADE_AO_REACH + 1.0F);

/* The revisions of the sections whose blocks can darken a face whose open cell is front (storage height): its own, and those across any
 * border nearer than SHADE_AO_REVISION_NEAR, combined in a fixed order; a column not loaded counts as 0. And its own column's generation.
 * The bytes of a section, not when it was sent, so the same world gives the same keys. */
static __device__ __forceinline__ uint32_t shade_ao_revision(const traverse_view_t *view, const int32_t front[3],
                                                             uint32_t *out_generation) {
    int32_t low[3], high[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const int32_t inside = front[i] & 15;
        low[i] = inside < SHADE_AO_REVISION_NEAR ? -1 : 0;
        high[i] = inside >= 16 - SHADE_AO_REVISION_NEAR ? 1 : 0;
    }
    uint32_t revision = 0u;
    *out_generation = 0u;
    for (int32_t dy = low[1]; dy <= high[1]; dy += 1) {
        for (int32_t dx = low[0]; dx <= high[0]; dx += 1) {
            const int32_t x = front[0] + dx * 16, y = front[1] + dy * 16;
            const world_buffers_column_t *column = nullptr;
            if (((uint32_t)(x - view->window_min_block[0]) < view->window_blocks) &&
                ((uint32_t)(y - view->window_min_block[1]) < view->window_blocks)) {
                const world_buffers_column_t *const there = &view->borrowed_table[traverse_slot(view, x >> 4, y >> 4)];
                column =
                    there->ready != 0u && there->chunk_x == (x >> 4) && there->chunk_y == (y >> 4) ? there : nullptr;
            }
            if (column != nullptr && dx == 0 && dy == 0) {
                *out_generation = column->generation;
            }
            for (int32_t dz = low[2]; dz <= high[2]; dz += 1) {
                const uint32_t section = (uint32_t)((front[2] >> 4) + dz);
                revision = shade_ao_revision_add(revision, column != nullptr && section < WORLD_BUFFERS_COLUMN_SECTIONS
                                                               ? column->revisions[section]
                                                               : 0u);
            }
        }
    }
    return revision;
}

/* A pixel's surface as the AO cache keys it: its block's cell, its face, the square of the face it is in, and the revisions of the sections
 * around the open cell in front of the face, where the blocks that darken it are. */
static __device__ __forceinline__ uint64_t shade_ao_cache_key_of(const traverse_view_t *view,
                                                                 const traverse_camera_t *camera,
                                                                 const traverse_hit_t *hit,
                                                                 const shade_surface_t *surface) {
    const uint32_t face = surface->face, axis = face >> 1;
    int32_t cell[3], front[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        cell[i] = camera->eye_cell[i] + (int32_t)hit->offset[i];
    }
    cell[2] -= TRAVERSE_Z_BASE;
    front[0] = cell[0];
    front[1] = cell[1];
    front[2] = cell[2];
    front[axis] += (face & 1u) != 0u ? 1 : -1;
    uint32_t generation = 0u;
    const uint32_t revision = front[2] >= 0 ? shade_ao_revision(view, front, &generation) : 0u;
    const float32_t u = surface->local[(axis + 1u) % 3u], v = surface->local[(axis + 2u) % 3u];
    return shade_ao_cache_key(cell, face, shade_ao_cache_place(u, v), generation, revision);
}

/* The type CUDA's 64-bit atomicMax takes, which the standard bans by name: uint64_t is another type to nvcc, of the same bits. */
typedef decltype(0ull) shade_atomic_word_t;
static_assert(sizeof(shade_atomic_word_t) == sizeof(uint64_t));

/* A pixel's AO, as it now is, into the cache: the newest wins, and of one frame the one of most samples. */
static __device__ __forceinline__ void shade_ao_cache_put(const shade_ao_cache_args_t *cache,
                                                          const traverse_view_t *view, const traverse_camera_t *camera,
                                                          const traverse_hit_t *hit, const shade_surface_t *surface,
                                                          uint32_t frame, float32_t visible, uint32_t samples) {
    const uint64_t key = shade_ao_cache_key_of(view, camera, hit, surface);
    const uint64_t word = shade_ao_cache_word(key, surface->face, frame & SHADE_AO_CACHE_AGE_MASK, visible, samples);
    atomicMax((shade_atomic_word_t *)(void *)&cache->words[shade_ao_cache_slot(key)], (shade_atomic_word_t)word);
}

/* Whether a block darkens the corner estimate: opaque on every face, a whole cube, and not water. */
static __device__ __forceinline__ bool shade_ao_solid(const traverse_view_t *view, const uint32_t *stops,
                                                      uint32_t water_material, uint32_t material) {
    if (material == SHADE_NO_MATERIAL || material == water_material || material >= TRAVERSE_STOPS_MATERIALS ||
        ((stops[material >> 5] >> (material & 31u)) & 1u) == 0u) {
        return false;
    }
    return view->borrowed_shapes == nullptr || material >= view->shape_count ||
           view->borrowed_shapes[material] == TRAVERSE_SHAPE_CUBE;
}

/* The corner estimate of a pixel's surface: the eight cells around the open cell in front of its face, in the face's plane. */
static __device__ __forceinline__ float32_t shade_ao_corner_of(const traverse_view_t *view, const uint32_t *stops,
                                                               uint32_t water_material, const traverse_camera_t *camera,
                                                               const traverse_hit_t *hit,
                                                               const shade_surface_t *surface) {
    const uint32_t face = surface->face, axis = face >> 1, along_u = (axis + 1u) % 3u, along_v = (axis + 2u) % 3u;
    int32_t front[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        front[i] = camera->eye_cell[i] + (int32_t)hit->offset[i];
    }
    front[2] -= TRAVERSE_Z_BASE;
    front[axis] += (face & 1u) != 0u ? 1 : -1;
    /* SHADE_AO_CELL_* in bit order: the four sides, then the four diagonals. */
    const int8_t steps[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    shade_cell_cursor_t cursor;
    cursor.section[0] = INT32_MAX;
    cursor.section[1] = INT32_MAX;
    cursor.section[2] = INT32_MAX;
    uint32_t solid = 0u;
    for (uint32_t k = 0u; k < 8u; k += 1u) {
        int32_t cell[3] = {front[0], front[1], front[2]};
        cell[along_u] += steps[k][0];
        cell[along_v] += steps[k][1];
        const uint32_t material = shade_cell_material_kept(view, cell, &cursor, nullptr);
        solid |= shade_ao_solid(view, stops, water_material, material) ? 1u << k : 0u;
    }
    return shade_ao_corner(solid, surface->local[along_u], surface->local[along_v]);
}

/* What AO needs of a frame, and of a pixel. */
typedef struct shade_ao_frame_t {
    const post_temporal_t *temporal; /* 0 */
    const shade_ao_pixel_t *last;    /* 8: what the last frame left */
    uint32_t pixel_x;                /* 16 */
    uint32_t pixel_y;                /* 20 */
    float32_t steady[3]; /* 24: this pixel's direction with no jitter, which is how the last frame is found */
    uint8_t _pad0[4];    /* 36 */
} shade_ao_frame_t;
static_assert(sizeof(shade_ao_frame_t) == 40);

/* How open a pixel's last surface is: what AO's history says, with this frame's ray in it if it is this pixel's turn. A pixel whose
 * own place in the last frame was another surface goes on from the pixels around that place which are on this one, with the weight
 * of one sample. With none of those either, as where something has just come into view, out_fresh says so, and shade_ao_fresh_kernel
 * sends its SHADE_AO_FRESH_RAYS. What is shown is its mean with those of the pixels around it on the same surface, as each one's own
 * walk through its samples shows as grain. out is what the next frame gets. */
static __device__ __forceinline__ float32_t shade_ao_at(const shade_context_t *context, const shade_ao_frame_t *ao,
                                                        const shade_ao_cache_args_t *cache, const traverse_hit_t *hit,
                                                        const shade_surface_t *surface, shade_ao_pixel_t *out,
                                                        bool *out_fresh, traverse_counts_t *counts, uint32_t pixel) {
    const uint32_t face = surface->face;
    const traverse_camera_t *const camera = context->camera;
    const post_temporal_t *const temporal = ao->temporal;
    out->face = (uint16_t)face;
    if (face >= SHADE_FACES) {
        out->visible = 1.0F;
        out->plane = 0.0F;
        out->samples = 0u;
        return 1.0F;
    }
    const uint32_t axis = face >> 1;
    const float32_t plane = surface->local[axis] + (float32_t)hit->offset[axis];
    const int32_t moved = camera->eye_cell[axis] - temporal->previous.eye_cell[axis];
    out->plane = plane;
    float32_t own = 1.0F, around_sum = 0.0F, around_count = 0.0F;
    uint32_t own_samples = 0u;
    float32_t place_x = 0.0F, place_y = 0.0F;
    if (temporal->ao_valid != 0u &&
        post_reproject(&temporal->previous, camera, ao->steady, hit->t, &place_x, &place_y)) {
        const int32_t last_x = (int32_t)place_x, last_y = (int32_t)place_y;
        for (int32_t dy = -1; dy <= 1; dy += 1) {
            for (int32_t dx = -1; dx <= 1; dx += 1) {
                const int32_t x = last_x + dx, y = last_y + dy;
                /* The last frame's AO is at its own size, which a rung change makes another (plan 008 phase 05). */
                if (x < 0 || y < 0 || x >= (int32_t)temporal->previous.width ||
                    y >= (int32_t)temporal->previous.height) {
                    continue;
                }
                const shade_ao_pixel_t *const one = &ao->last[(uint64_t)y * temporal->previous.width + (uint32_t)x];
                if (!shade_ao_same_surface(one, face, plane, moved, hit->t)) {
                    continue;
                }
                if (dx == 0 && dy == 0) {
                    own = one->visible;
                    own_samples = one->samples;
                } else {
                    around_sum += one->visible;
                    around_count += 1.0F;
                }
            }
        }
    }
    if (own_samples == 0u && around_count > 0.0F) {
        own = around_sum / around_count;
        own_samples = 1u;
    }
    if (own_samples == 0u) {
        *out_fresh = true;
        return 0.0F;
    }
    if (shade_ao_turn(temporal->frame, ao->pixel_x, ao->pixel_y)) {
        float32_t u = 0.0F, v = 0.0F, direction[3];
        shade_ao_sample(shade_ao_hash(ao->pixel_x, ao->pixel_y), temporal->frame / SHADE_AO_TURNS, &u, &v);
        shade_ao_direction(face, u, v, direction);
        const float32_t seen = shade_ao_ray(context, hit, surface, direction, counts, pixel);
        const uint32_t samples =
            own_samples >= SHADE_AO_SAMPLES_MAX ? (uint32_t)SHADE_AO_SAMPLES_MAX : own_samples + 1u;
        out->visible = own + (seen - own) / (float32_t)samples;
        out->samples = (uint16_t)samples;
        /* Plan 008 phase 04: what it has now, for a pixel that sees this place again after it left the picture. Nothing in this frame
         * reads the cache before shade_ao_fresh_kernel, so what that finds is the same whatever order these come in. */
        if (cache->words != nullptr && shade_ao_cache_writes(temporal->flags, temporal->frame)) {
            shade_ao_cache_put(cache, context->view, camera, hit, surface, temporal->frame, out->visible, samples);
        }
    } else {
        out->visible = own;
        out->samples = (uint16_t)own_samples;
    }
    return (out->visible + around_sum) / (1.0F + around_count);
}

/* What a surface of water reflects: one ray from it, mirrored in the face it came in by. Simple on purpose. What it meets has the
 * light stored in its voxel and all the sun that faces it, with no ray of its own to ask, and it goes on through holes only: what is
 * part-covered shows its mean colour, water in water included, so that this cannot go on and on. Its fog is by the whole way from
 * the eye. A ray that meets nothing sees the sky, with the sun's disc in it: that is the glitter on a lake. */
static __device__ __forceinline__ void shade_reflection(const shade_context_t *context, const traverse_hit_t *from,
                                                        const shade_surface_t *surface, const float32_t direction[3],
                                                        float32_t first_t, float32_t out[3], traverse_counts_t *counts,
                                                        uint32_t pixel) {
    const bool see_through = (context->features & SHADE_FEATURE_TRANSPARENCY) != 0u;
    traverse_trace_ray_t ray;
    float32_t mirrored[3];
    const uint32_t axis = surface->face >> 1;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        mirrored[i] = i == axis ? -direction[i] : direction[i];
        ray.eye_cell[i] = context->camera->eye_cell[i] + (int32_t)from->offset[i];
        ray.eye_frac[i] = surface->local[i];
        ray.direction[i] = traverse_guard(mirrored[i]);
    }
    ray.eye_cell[2] -= TRAVERSE_Z_BASE;
    const float32_t bias = shade_sun_bias(from->t);
    ray.eye_frac[axis] += (surface->face & 1u) != 0u ? bias : -bias;
    ray.t_start = 0.0F;
    ray.resume = 1u;
    ray.t_end = TRAVERSE_T_END_NONE;
    float32_t sky[3];
    post_sky(context->post, mirrored, sky);
    uint32_t last_material = SHADE_NO_MATERIAL;
    int16_t last_offset[3] = {0, 0, 0};
    for (uint32_t layer = 0u;; layer += 1u) {
        traverse_hit_t hit;
        traverse_tally_t tally;
        traverse_trace_counted(
            context->view, &ray, nullptr, &hit,
            traverse_tally_for(&tally, counts, TRAVERSE_COUNT_REFLECT, pixel, TRAVERSE_COUNT_NO_LEVEL, 1.0F));
        const uint32_t face = (uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK;
        if (face == TRAVERSE_FACE_MISS) {
            post_sky_discs(context->post, mirrored, sky);
            out[0] = sky[0];
            out[1] = sky[1];
            out[2] = sky[2];
            return;
        }
        const float32_t whole_way = from->t + hit.t;
        shade_surface_t met;
        shade_surface_read(context, ray.eye_frac, ray.direction, &hit, whole_way, &met);
        float32_t colour[3] = {SHADE_MISSING_R, SHADE_MISSING_G, SHADE_MISSING_B};
        if (met.texture != nullptr) {
            const bool last_layer = layer + 1u >= SHADE_REFLECTION_LAYERS;
            const bool behind = see_through && !last_layer &&
                                shade_own_kind_behind(last_material, last_offset, met.material, hit.offset, face);
            if (behind || (met.texel[3] == 0u && (context->features & SHADE_FEATURE_ALPHA) != 0u && !last_layer)) {
                last_material = met.material == context->glass_material ? met.material : (uint32_t)SHADE_NO_MATERIAL;
                last_offset[0] = hit.offset[0];
                last_offset[1] = hit.offset[1];
                last_offset[2] = hit.offset[2];
                ray.t_start = hit.t;
                continue;
            }
            float32_t light[3];
            shade_texel_colour(met.texture, met.texel, colour);
            shade_surface_light(context->sun, met.face, met.stored, 1.0F, light);
            colour[0] *= light[0];
            colour[1] *= light[1];
            colour[2] *= light[2];
        }
        const float32_t fog = shade_fog_behind(context->post, direction[2], first_t, whole_way);
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            out[i] = colour[i] + (sky[i] - colour[i]) * fog;
        }
        return;
    }
}

/* Over a surface's colour, the crack on the block being mined: its texture's frame for how far along that is, by the same place on the
 * face and the same choice of mip as the surface's own. */
static __device__ __forceinline__ void shade_crack_over(const shade_context_t *context, const shade_texture_t *crack,
                                                        uint32_t frame, const shade_surface_t *surface, float32_t t,
                                                        float32_t colour[3]) {
    float32_t u = 0.0F, v = 0.0F;
    shade_face_uv(surface->face, surface->local, &u, &v);
    const uint32_t level = shade_mip_level(t, context->tangent, crack->width, crack->level_count);
    const uint32_t width = shade_level_dimension(crack->width, level);
    const uint32_t height = shade_level_dimension(crack->height, level);
    const uint8_t *const texel =
        shade_texel(crack, context->pixels, level, u, v) + (uint64_t)(frame - 1u) * (uint64_t)width * height * 4u;
    const float32_t cover = (float32_t)texel[3] / 255.0F;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float32_t seen = shade_srgb_byte(texel[i]);
        colour[i] += (seen - colour[i]) * cover;
    }
}

/* What a box shows where a pixel's ray meets it, as a block's face would under the same light and sun: a texture over the whole of the
 * face or a body part's share of its skin, by its own axes, and one sun ray from that place through the world. Boxes cast no shadow and take no AO. */
static __device__ __forceinline__ void shade_box_colour(const shade_context_t *context, const entity_rays_box_t *box,
                                                        const shade_texture_t *entity_textures,
                                                        const entity_rays_hit_t *entity, const float32_t direction[3],
                                                        float32_t out[3]) {
    const uint32_t face = entity->face;
    const float32_t t = entity->t;
    const float32_t from_middle[3] = {direction[0] * t - box->center[0], direction[1] * t - box->center[1],
                                      direction[2] * t - box->center[2]};
    float32_t own[3];
    entity_rays_to_box(box, from_middle, own);
    shade_surface_t surface = {};
    surface.face = face;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float32_t part = (own[i] + box->half[i]) / (2.0F * box->half[i]);
        surface.local[i] = part < 0.0F ? 0.0F : (part > 1.0F ? 1.0F : part);
    }
    const shade_texture_t *texture = nullptr;
    if (box->material != ENTITY_RAYS_NO_MATERIAL) {
        const uint32_t index = box->material < context->material_count
                                   ? context->faces[box->material * SHADE_FACES + face]
                                   : (uint32_t)SHADE_NO_TEXTURE;
        texture = index != SHADE_NO_TEXTURE ? &context->textures[index] : nullptr;
    } else if (box->texture != ENTITY_RAYS_NO_TEXTURE) {
        texture = &entity_textures[box->texture];
    }
    float32_t colour[3] = {SHADE_MISSING_R, SHADE_MISSING_G, SHADE_MISSING_B};
    if (texture != nullptr) {
        float32_t u = 0.0F, v = 0.0F;
        /* The whole texture is across the box, so that its texels are as many times smaller on the screen as the box is than a block. */
        float32_t across = fmaxf(box->half[0], fmaxf(box->half[1], box->half[2])) * 2.0F;
        if (box->skin_size[0] != 0u) {
            /* A skin's texels are the same size on every part: its whole width is this many blocks. */
            entity_rays_skin_uv(box, face, surface.local, texture->width, texture->height, &u, &v);
            across = (float32_t)texture->width / ENTITY_RAYS_SKIN_TEXELS_PER_BLOCK;
        } else {
            shade_face_uv(face, surface.local, &u, &v);
        }
        const uint32_t level = shade_mip_level(t / across, context->tangent, texture->width, texture->level_count);
        shade_texel_colour(texture, shade_texel(texture, context->pixels, level, u, v), colour);
    }
    float32_t normal[3];
    entity_rays_normal(box, face, normal);
    const uint32_t stored =
        (context->features & SHADE_FEATURE_LIGHT) != 0u ? box->stored : (uint32_t)SHADE_LIGHT_OPEN_SKY;
    const shade_sun_t *const sun = context->sun;
    const float32_t along =
        normal[0] * sun->direction[0] + normal[1] * sun->direction[1] + normal[2] * sun->direction[2];
    float32_t sun_seen = 1.0F;
    if ((context->features & SHADE_FEATURE_SHADOW) != 0u && (stored >> 4) != 0u && along > 0.0F &&
        sun->direct[0] + sun->direct[1] + sun->direct[2] > 0.0F) {
        /* The place as a voxel and a fraction, as a god ray's, and pushed off along whichever of the world's axes it looks most along. */
        traverse_hit_t place = {};
        place.t = t;
        shade_surface_t from = {};
        uint32_t axis = 0u;
        for (uint32_t a = 0u; a < 3u; a += 1u) {
            const float32_t coordinate = context->camera->eye_frac[a] + direction[a] * t;
            const float32_t cell = floorf(coordinate);
            place.offset[a] = (int16_t)cell;
            from.local[a] = coordinate - cell;
            axis = fabsf(normal[a]) > fabsf(normal[axis]) ? a : axis;
        }
        from.face = axis * 2u + (normal[axis] > 0.0F ? 1u : 0u);
        sun_seen = shade_sun_seen(context, &place, &from, nullptr, TRAVERSE_COUNT_NO_PIXEL);
    }
    float32_t light[3];
    shade_box_light(sun, face, normal, stored, sun_seen, light);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        out[i] = colour[i] * light[i];
    }
}

/* What one layer adds to a pixel: its colour, fogged, by its share of what still gets this far. */
static __device__ __forceinline__ void shade_add(float32_t sum[3], const float32_t through[3],
                                                 const float32_t colour[3], const float32_t sky[3], float32_t fog,
                                                 float32_t weight) {
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        sum[i] += through[i] * weight * (colour[i] + (sky[i] - colour[i]) * fog);
    }
}

/* What shade_kernel leaves for the two kernels below, in AO's next block, which has the room, in place of a shade_ao_pixel_t: what a pixel
 * shows of its last surface per unit of light on it. Its hit stays in the hits, which nothing reads after this, as that surface. A pixel's
 * colour is a straight line in what the sun ray and AO find, so each of them adds its own part afterwards. See the phase 17 stage 3 file for
 * why they are kernels of their own: rays of a warp that come to one at different layers of shade_kernel's loop each run it while the rest wait. */
typedef struct shade_ao_work_t {
    float32_t share[3]; /* 0 */
} shade_ao_work_t;
static_assert(sizeof(shade_ao_work_t) == sizeof(shade_ao_pixel_t));

/* What these two need of a pixel's last surface. */
static __device__ __forceinline__ void shade_last_surface(const traverse_camera_t *camera, const traverse_hit_t *hit,
                                                          const float32_t direction[3], uint32_t features,
                                                          shade_surface_t *out) {
    *out = shade_surface_t{};
    out->face = (uint32_t)hit->info & TRAVERSE_INFO_FACE_MASK;
    out->stored = (features & SHADE_FEATURE_LIGHT) != 0u ? (hit->value >> TRAVERSE_VALUE_LIGHT_SHIFT) & 0xFFu
                                                         : (uint32_t)SHADE_LIGHT_OPEN_SKY;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        out->local[i] = camera->eye_frac[i] + direction[i] * hit->t - (float32_t)hit->offset[i];
    }
}

/* The sun ray of a pixel's last surface, where shade_kernel left one to ask. */
static __device__ __forceinline__ void
shade_sun_kernel_body(const traverse_view_t *view, const traverse_camera_t *camera, const shade_texture_t *textures,
                      const uint16_t *faces, const uint8_t *pixels, uint32_t material_count, uint32_t features,
                      uint32_t water_material, uint32_t glass_material, const shade_sun_t *sun,
                      const shade_ao_work_t *work, const traverse_hit_t *hits, shade_pixel_t *out,
                      traverse_counts_t *counts) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera->width || pixel_y >= camera->height) {
        return;
    }
    const uint64_t index = (uint64_t)pixel_y * camera->width + pixel_x;
    const traverse_hit_t hit = hits[index];
    const uint32_t face = (uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK;
    const uint32_t stored = (features & SHADE_FEATURE_LIGHT) != 0u ? (hit.value >> TRAVERSE_VALUE_LIGHT_SHIFT) & 0xFFu
                                                                   : (uint32_t)SHADE_LIGHT_OPEN_SKY;
    /* Far from all of this, a face turned away, a miss among them, has nothing to ask. */
    const float32_t towards = face == TRAVERSE_FACE_MISS ? 0.0F : shade_sun_towards(sun, face, stored);
    if (!(towards > 0.0F) || !(sun->direct[0] + sun->direct[1] + sun->direct[2] > 0.0F)) {
        return;
    }
    const shade_context_t context = {
        view,
        camera,
        textures,
        faces,
        pixels,
        sun,
        nullptr,
        shade_pixel_tangent(camera),
        material_count,
        features,
        (uint16_t)water_material,
        (uint16_t)glass_material,
    };
    float32_t direction[3];
    traverse_camera_direction(camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
    shade_surface_t surface;
    shade_last_surface(camera, &hit, direction, features, &surface);
    const float32_t seen = shade_sun_seen(&context, &hit, &surface, counts, (uint32_t)index);
    const shade_ao_work_t mine = work[index];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        out[index].rgb[i] += mine.share[i] * sun->direct[i] * towards * seen;
    }
}

__global__ void shade_sun_kernel(const traverse_view_t view, const traverse_camera_t camera,
                                 const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                                 uint32_t material_count, uint32_t features, uint32_t water_material,
                                 uint32_t glass_material, const shade_sun_t sun, const shade_ao_work_t *work,
                                 const traverse_hit_t *hits, shade_pixel_t *out) {
    shade_sun_kernel_body(&view, &camera, textures, faces, pixels, material_count, features, water_material,
                          glass_material, &sun, work, hits, out, nullptr);
}

__global__ void shade_sun_kernel_counted(const traverse_view_t view, const traverse_camera_t camera,
                                         const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                                         uint32_t material_count, uint32_t features, uint32_t water_material,
                                         uint32_t glass_material, const shade_sun_t sun, const shade_ao_work_t *work,
                                         const traverse_hit_t *hits, shade_pixel_t *out, traverse_counts_t *counts) {
    shade_sun_kernel_body(&view, &camera, textures, faces, pixels, material_count, features, water_material,
                          glass_material, &sun, work, hits, out, counts);
}

/* What AO finds for a pixel's last surface, and its history for the next frame. */
static __device__ __forceinline__ void
shade_ao_kernel_body(const traverse_view_t *view, const traverse_camera_t *camera, const shade_texture_t *textures,
                     const uint16_t *faces, const uint8_t *pixels, uint32_t material_count, uint32_t features,
                     uint32_t water_material, uint32_t glass_material, const shade_sun_t *sun,
                     const post_temporal_t *temporal, const shade_ao_cache_args_t *cache,
                     const shade_ao_pixel_t *ao_last, shade_ao_pixel_t *ao_next, uint32_t *fresh, uint32_t *fresh_count,
                     const traverse_hit_t *hits, shade_pixel_t *out, traverse_counts_t *counts) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera->width || pixel_y >= camera->height) {
        return;
    }
    const uint64_t index = (uint64_t)pixel_y * camera->width + pixel_x;
    const traverse_hit_t hit = hits[index];
    shade_ao_pixel_t ao_out = {1.0F, 0.0F, 0u, (uint16_t)TRAVERSE_FACE_MISS};
    if (((uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK) != TRAVERSE_FACE_MISS) {
        const shade_ao_work_t work = ((const shade_ao_work_t *)(const void *)ao_next)[index];
        const shade_context_t context = {
            view,
            camera,
            textures,
            faces,
            pixels,
            sun,
            nullptr,
            shade_pixel_tangent(camera),
            material_count,
            features,
            (uint16_t)water_material,
            (uint16_t)glass_material,
        };
        float32_t direction[3];
        traverse_camera_direction(camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
        shade_surface_t surface;
        shade_last_surface(camera, &hit, direction, features, &surface);
        shade_ao_frame_t ao = {temporal, ao_last, pixel_x, pixel_y, {0.0F, 0.0F, 0.0F}, {0u, 0u, 0u, 0u}};
        float32_t jitter[2];
        post_temporal_jitter(temporal, jitter);
        traverse_camera_direction(camera, (float32_t)pixel_x + 0.5F - jitter[0], (float32_t)pixel_y + 0.5F - jitter[1],
                                  ao.steady);
        bool is_fresh = false;
        const float32_t visible =
            shade_ao_at(&context, &ao, cache, &hit, &surface, &ao_out, &is_fresh, counts, (uint32_t)index);
        if (is_fresh) {
            /* What it left in the next block stays, for shade_ao_fresh_kernel to go on from. */
            fresh[atomicAdd(fresh_count, 1u)] = (uint32_t)index;
            return;
        }
        float32_t light[3];
        shade_surface_light_stored(sun, surface.face, surface.stored, light);
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            out[index].rgb[i] += work.share[i] * light[i] * visible;
        }
    }
    ao_next[index] = ao_out;
}

__global__ void shade_ao_kernel(const traverse_view_t view, const traverse_camera_t camera,
                                const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                                uint32_t material_count, uint32_t features, uint32_t water_material,
                                uint32_t glass_material, const shade_sun_t sun, const post_temporal_t temporal,
                                const shade_ao_cache_args_t cache, const shade_ao_pixel_t *ao_last,
                                shade_ao_pixel_t *ao_next, uint32_t *fresh, uint32_t *fresh_count,
                                const traverse_hit_t *hits, shade_pixel_t *out) {
    shade_ao_kernel_body(&view, &camera, textures, faces, pixels, material_count, features, water_material,
                         glass_material, &sun, &temporal, &cache, ao_last, ao_next, fresh, fresh_count, hits, out,
                         nullptr);
}

__global__ void shade_ao_kernel_counted(const traverse_view_t view, const traverse_camera_t camera,
                                        const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                                        uint32_t material_count, uint32_t features, uint32_t water_material,
                                        uint32_t glass_material, const shade_sun_t sun, const post_temporal_t temporal,
                                        const shade_ao_cache_args_t cache, const shade_ao_pixel_t *ao_last,
                                        shade_ao_pixel_t *ao_next, uint32_t *fresh, uint32_t *fresh_count,
                                        const traverse_hit_t *hits, shade_pixel_t *out, traverse_counts_t *counts) {
    shade_ao_kernel_body(&view, &camera, textures, faces, pixels, material_count, features, water_material,
                         glass_material, &sun, &temporal, &cache, ao_last, ao_next, fresh, fresh_count, hits, out,
                         counts);
}

/* Before shade_kernel, for a pixel whose ray first meets a surface of water: its reflection in rgb and what its sun ray saw in t, of the
 * pixels block, which shade_kernel takes from there and then writes over. The same work it would have done at that one place. As with the two
 * kernels above, here all a warp's rays of each kind run together. A ray that comes to water behind something still asks in shade_kernel. And
 * where the water goes on past what a ray looks through, that is the whole pixel, the same sums as shade_kernel's, so it is finished here. */
/* The rays of the pixels shade_ao_kernel listed: as a list, and not where they are on the picture, so that a warp is 32 pixels that have rays to
 * send, and not 32 of which one has, which would take as long. See the phase 17 stage 3 file. Plan 008 phase 04: while the camera moves, with
 * POST_TEMPORAL_FIRST_GUESS, a pixel on a whole cube sends none. It takes what the AO cache has for its place, as up to
 * SHADE_AO_CACHE_SAMPLES_CAP samples, or else the corner estimate as one; its own rays then refine that, one frame in SHADE_AO_TURNS. Only a
 * face of what is not a cube (a slab, stairs, a plant) still sends rays then, and it does not write the cache: nothing written in this kernel
 * may be read in it, or the picture would hang on the order the card ran it in. */
static __device__ __forceinline__ void
shade_ao_fresh_kernel_body(const traverse_view_t *view, const traverse_camera_t *camera,
                           const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                           uint32_t material_count, uint32_t features, uint32_t water_material, uint32_t glass_material,
                           const shade_sun_t *sun, uint32_t frame, uint32_t flags, const shade_ao_cache_args_t *cache,
                           shade_ao_pixel_t *ao_next, const uint32_t *fresh, const uint32_t *fresh_count,
                           const traverse_hit_t *hits, shade_pixel_t *out, traverse_counts_t *counts) {
    const uint32_t place = blockIdx.x * blockDim.x + threadIdx.x;
    if (place >= *fresh_count) {
        return;
    }
    const uint32_t index = fresh[place];
    const uint32_t pixel_y = index / camera->width, pixel_x = index - pixel_y * camera->width;
    const traverse_hit_t hit = hits[index];
    const shade_ao_work_t work = ((const shade_ao_work_t *)(const void *)ao_next)[index];
    const shade_context_t context = {
        view,
        camera,
        textures,
        faces,
        pixels,
        sun,
        nullptr,
        shade_pixel_tangent(camera),
        material_count,
        features,
        (uint16_t)water_material,
        (uint16_t)glass_material,
    };
    float32_t direction[3];
    traverse_camera_direction(camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
    shade_surface_t surface;
    shade_last_surface(camera, &hit, direction, features, &surface);
    const uint32_t wanted = POST_TEMPORAL_FIRST_GUESS | POST_TEMPORAL_MOVED;
    const bool guess = (flags & wanted) == wanted && cache->stops != nullptr;
    const uint32_t material = hit.value & TRAVERSE_VALUE_MATERIAL_MASK;
    const bool cube = view->borrowed_shapes == nullptr || material >= view->shape_count ||
                      view->borrowed_shapes[material] == TRAVERSE_SHAPE_CUBE;
    float32_t visible = 0.0F;
    uint32_t samples = 0u;
    if (guess && cube) {
        if (cache->words != nullptr) {
            const uint64_t key = shade_ao_cache_key_of(view, camera, &hit, &surface);
            (void)shade_ao_cache_hit(cache->words[shade_ao_cache_slot(key)], key, surface.face,
                                     frame & SHADE_AO_CACHE_AGE_MASK, &visible, &samples);
        }
        if (samples == 0u) {
            visible = shade_ao_corner_of(view, cache->stops, water_material, camera, &hit, &surface);
            samples = 1u;
        }
    } else {
        const uint32_t rays = (flags & POST_TEMPORAL_FRESH_ONE) != 0u ? 1u : (uint32_t)SHADE_AO_FRESH_RAYS;
        const uint32_t hash = shade_ao_hash(pixel_x, pixel_y);
        const uint32_t first_sample = frame / SHADE_AO_TURNS;
        float32_t seen = 0.0F;
        for (uint32_t r = 0u; r < rays; r += 1u) {
            float32_t u = 0.0F, v = 0.0F, ray_direction[3];
            shade_ao_sample(hash, first_sample + r, &u, &v);
            shade_ao_direction(surface.face, u, v, ray_direction);
            seen += shade_ao_ray(&context, &hit, &surface, ray_direction, counts, index);
        }
        visible = seen / (float32_t)rays;
        samples = rays;
        if (!guess && cache->words != nullptr && shade_ao_cache_writes(flags, frame)) {
            shade_ao_cache_put(cache, view, camera, &hit, &surface, frame, visible, samples);
        }
    }
    float32_t light[3];
    shade_surface_light_stored(sun, surface.face, surface.stored, light);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        out[index].rgb[i] += work.share[i] * light[i] * visible;
    }
    const uint32_t axis = surface.face >> 1;
    ao_next[index] = shade_ao_pixel_t{visible, surface.local[axis] + (float32_t)hit.offset[axis], (uint16_t)samples,
                                      (uint16_t)surface.face};
}

__global__ void shade_ao_fresh_kernel(const traverse_view_t view, const traverse_camera_t camera,
                                      const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                                      uint32_t material_count, uint32_t features, uint32_t water_material,
                                      uint32_t glass_material, const shade_sun_t sun, uint32_t frame, uint32_t flags,
                                      const shade_ao_cache_args_t cache, shade_ao_pixel_t *ao_next,
                                      const uint32_t *fresh, const uint32_t *fresh_count, const traverse_hit_t *hits,
                                      shade_pixel_t *out) {
    shade_ao_fresh_kernel_body(&view, &camera, textures, faces, pixels, material_count, features, water_material,
                               glass_material, &sun, frame, flags, &cache, ao_next, fresh, fresh_count, hits, out,
                               nullptr);
}

__global__ void shade_ao_fresh_kernel_counted(const traverse_view_t view, const traverse_camera_t camera,
                                              const shade_texture_t *textures, const uint16_t *faces,
                                              const uint8_t *pixels, uint32_t material_count, uint32_t features,
                                              uint32_t water_material, uint32_t glass_material, const shade_sun_t sun,
                                              uint32_t frame, uint32_t flags, const shade_ao_cache_args_t cache,
                                              shade_ao_pixel_t *ao_next, const uint32_t *fresh,
                                              const uint32_t *fresh_count, const traverse_hit_t *hits,
                                              shade_pixel_t *out, traverse_counts_t *counts) {
    shade_ao_fresh_kernel_body(&view, &camera, textures, faces, pixels, material_count, features, water_material,
                               glass_material, &sun, frame, flags, &cache, ao_next, fresh, fresh_count, hits, out,
                               counts);
}

/* A list starts each frame empty, from where it is in the stream. */
__global__ void shade_ao_fresh_clear_kernel(uint32_t *fresh_count) {
    *fresh_count = 0u;
}

static __device__ __forceinline__ void shade_water_kernel_body(
    const traverse_view_t *view, const traverse_camera_t *camera, const shade_texture_t *textures,
    const uint16_t *faces, const uint8_t *pixels, uint32_t material_count, uint32_t features, uint32_t water_material,
    uint32_t glass_material, const shade_sun_t *sun, const post_settings_t *post, const entity_rays_hit_t *entity_hits,
    const entity_rays_mark_t *mark, traverse_hit_t *hits, shade_pixel_t *out, traverse_counts_t *counts) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera->width || pixel_y >= camera->height) {
        return;
    }
    const uint64_t index = (uint64_t)pixel_y * camera->width + pixel_x;
    const traverse_hit_t hit = hits[index];
    const uint32_t face = (uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK;
    if (face >= SHADE_FACES || (hit.value & TRAVERSE_VALUE_MATERIAL_MASK) != water_material) {
        return;
    }
    const shade_context_t context = {
        view,
        camera,
        textures,
        faces,
        pixels,
        sun,
        post,
        shade_pixel_tangent(camera),
        material_count,
        features,
        (uint16_t)water_material,
        (uint16_t)glass_material,
    };
    float32_t direction[3];
    traverse_camera_direction(camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
    shade_surface_t surface;
    shade_surface_read(&context, camera->eye_frac, direction, &hit, hit.t, &surface);
    if (surface.texture == nullptr) {
        return;
    }
    shade_pixel_t result = {{0.0F, 0.0F, 0.0F}, 1.0F};
    if ((features & SHADE_FEATURE_SHADOW) != 0u && shade_sun_towards(sun, face, surface.stored) > 0.0F &&
        sun->direct[0] + sun->direct[1] + sun->direct[2] > 0.0F) {
        result.t = shade_sun_seen(&context, &hit, &surface, counts, (uint32_t)index);
    }
    if ((features & SHADE_FEATURE_WATER_REFLECTION) != 0u) {
        shade_reflection(&context, &hit, &surface, direction, hit.t, result.rgb, counts, (uint32_t)index);
    }
    const float32_t entity_t = entity_hits != nullptr ? entity_hits[index].t : TRAVERSE_COARSE_SKY;
    const bool cracked = mark->crack_frame != 0u && hit.offset[0] == mark->crack_cell[0] &&
                         hit.offset[1] == mark->crack_cell[1] && hit.offset[2] == mark->crack_cell[2];
    /* A hole in the water's own texture would be gone on through, and is shade_kernel's. */
    const bool hole = surface.texel[3] == 0u && (features & SHADE_FEATURE_ALPHA) != 0u;
    if ((features & SHADE_FEATURE_WATER_TINT) == 0u || entity_t < hit.t || cracked || hole) {
        out[index] = result;
        return;
    }
    traverse_trace_ray_t ray;
    ray.eye_cell[0] = camera->eye_cell[0];
    ray.eye_cell[1] = camera->eye_cell[1];
    ray.eye_cell[2] = camera->eye_cell[2] - TRAVERSE_Z_BASE;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        ray.eye_frac[i] = camera->eye_frac[i];
        ray.direction[i] = traverse_guard(direction[i]);
    }
    ray.resume = 1u;
    ray.t_start = hit.t;
    ray.t_end = TRAVERSE_T_END_NONE;
    shade_cell_cursor_t cursor;
    cursor.section[0] = INT32_MAX;
    cursor.section[1] = 0;
    cursor.section[2] = 0;
    shade_water_walk_t walk;
    shade_water_through(view, &ray, &hit, water_material, SHADE_WATER_SEEN_BLOCKS, &cursor, &walk);
    if (walk.cut_short == 0u || entity_t < walk.out) {
        out[index] = result;
        return;
    }
    /* From here on as in shade_kernel, at a fog of 0, as this is the first surface. */
    float32_t light[3];
    shade_surface_light_near(sun, face, surface.stored, result.t, 1.0F, light);
    float32_t sum[3] = {0.0F, 0.0F, 0.0F};
    float32_t through[3] = {1.0F, 1.0F, 1.0F};
    const float32_t none[3] = {0.0F, 0.0F, 0.0F};
    if ((features & SHADE_FEATURE_WATER_REFLECTION) != 0u) {
        const float32_t share = shade_water_fresnel(fabsf(direction[face >> 1]));
        shade_add(sum, through, result.rgb, none, 0.0F, share);
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            through[i] *= 1.0F - share;
        }
    }
    float32_t colour[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        colour[i] = shade_srgb_byte(surface.texel[i]) * light[i];
    }
    const float32_t cover = (float32_t)surface.texel[3] / 255.0F;
    shade_add(sum, through, colour, none, 0.0F, cover);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        through[i] *= 1.0F - cover;
    }
    float32_t full[3], passed[3], given[3];
    shade_surface_light(sun, TRAVERSE_FACE_POS_Z, surface.stored, 1.0F, full);
    shade_water_tint(fminf(walk.out, entity_t) - hit.t, passed, given);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        given[i] *= full[i];
    }
    shade_add(sum, through, given, none, 0.0F, 1.0F);
    out[index] = shade_pixel_t{{sum[0], sum[1], sum[2]}, hit.t};
    hits[index].info = (uint16_t)((uint32_t)TRAVERSE_FACE_MISS | TRAVERSE_INFO_CAPPED);
}

__global__ void shade_water_kernel(const traverse_view_t view, const traverse_camera_t camera,
                                   const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                                   uint32_t material_count, uint32_t features, uint32_t water_material,
                                   uint32_t glass_material, const shade_sun_t sun, const post_settings_t post,
                                   const entity_rays_hit_t *entity_hits, const entity_rays_mark_t mark,
                                   traverse_hit_t *hits, shade_pixel_t *out) {
    shade_water_kernel_body(&view, &camera, textures, faces, pixels, material_count, features, water_material,
                            glass_material, &sun, &post, entity_hits, &mark, hits, out, nullptr);
}

__global__ void shade_water_kernel_counted(const traverse_view_t view, const traverse_camera_t camera,
                                           const shade_texture_t *textures, const uint16_t *faces,
                                           const uint8_t *pixels, uint32_t material_count, uint32_t features,
                                           uint32_t water_material, uint32_t glass_material, const shade_sun_t sun,
                                           const post_settings_t post, const entity_rays_hit_t *entity_hits,
                                           const entity_rays_mark_t mark, traverse_hit_t *hits, shade_pixel_t *out,
                                           traverse_counts_t *counts) {
    shade_water_kernel_body(&view, &camera, textures, faces, pixels, material_count, features, water_material,
                            glass_material, &sun, &post, entity_hits, &mark, hits, out, counts);
}

static __device__ __forceinline__ void
shade_kernel_body(const traverse_view_t *view, const traverse_camera_t *camera, const shade_texture_t *textures,
                  const uint16_t *faces, const uint8_t *pixels, uint32_t material_count, uint32_t features,
                  uint32_t water_material, uint32_t glass_material, const shade_sun_t *sun, const post_settings_t *post,
                  shade_ao_pixel_t *ao_next, const entity_rays_box_t *entity_boxes,
                  const entity_rays_hit_t *entity_hits, const shade_texture_t *entity_textures,
                  const entity_rays_mark_t *mark, const lod_hit_t *lod_hits, traverse_hit_t *hits, shade_pixel_t *out,
                  traverse_counts_t *counts) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera->width || pixel_y >= camera->height) {
        return;
    }
    const uint64_t index = (uint64_t)pixel_y * camera->width + pixel_x;
    traverse_hit_t hit = hits[index];
    /* shade_water_kernel has finished this one. A ray that truly ran out of steps says the same, and it is never seen: a test says so. */
    if ((hit.info & TRAVERSE_INFO_CAPPED) != 0u && (features & SHADE_FEATURE_TRANSPARENCY) != 0u) {
        return;
    }
    float32_t direction[3];
    traverse_camera_direction(camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
    const shade_context_t context = {
        view,
        camera,
        textures,
        faces,
        pixels,
        sun,
        post,
        shade_pixel_tangent(camera),
        material_count,
        features,
        (uint16_t)water_material,
        (uint16_t)glass_material,
    };
    const bool see_through = (features & SHADE_FEATURE_TRANSPARENCY) != 0u;
    const bool tint = (features & SHADE_FEATURE_WATER_TINT) != 0u;
    traverse_trace_ray_t ray;
    ray.eye_cell[0] = camera->eye_cell[0];
    ray.eye_cell[1] = camera->eye_cell[1];
    ray.eye_cell[2] = camera->eye_cell[2] - TRAVERSE_Z_BASE;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        ray.eye_frac[i] = camera->eye_frac[i];
        ray.direction[i] = traverse_guard(direction[i]);
    }
    ray.resume = 1u;
    /* Plan 009 phase 04: on past what it can see through, as the primary ray went: to this pixel's d_1, and past it the far field's hit,
     * which lod_frame followed from there. */
    ray.t_end = traverse_band_from(view, traverse_band_factor(view, pixel_x, pixel_y), 1u);
    const entity_rays_hit_t entity =
        entity_hits != nullptr
            ? entity_hits[index]
            : entity_rays_hit_t{TRAVERSE_COARSE_SKY, (uint16_t)ENTITY_RAYS_NO_BOX, (uint16_t)TRAVERSE_FACE_MISS};
    float32_t sky[3];
    post_sky(post, direction, sky);
    /* From near to far: sum is what is seen so far, and through how much of whatever is further on still shows. */
    float32_t sum[3] = {0.0F, 0.0F, 0.0F};
    float32_t through[3] = {1.0F, 1.0F, 1.0F};
    float32_t first_t = 0.0F;
    bool drawn = false;
    /* Nothing but water has been drawn on this ray: for post_under_water, whose distance is to the first thing that is not water. */
    bool only_water = true;
    uint32_t last_material = SHADE_NO_MATERIAL;
    int16_t last_offset[3] = {0, 0, 0};
    shade_pixel_t result;
    /* AO is about the surface a pixel's ray ends on, the one it sees whole: a bed under water, and not what lies over it. */
    const bool ao_on = (features & SHADE_FEATURE_AO) != 0u;
    /* With nowhere to leave it, as a bare call with no history, a last surface's sun ray is asked here. */
    const bool defer = ao_next != nullptr;
    bool deferred = false;
    /* Each pass is one surface. A ray never comes back to a surface, so this ends by itself, and SHADE_ALPHA_LAYERS is only what a
     * line of leaves may cost a pixel. */
    for (uint32_t layer = 0u;; layer += 1u) {
        const uint32_t face = (uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK;
        if (entity.box != ENTITY_RAYS_NO_BOX && (face == TRAVERSE_FACE_MISS || entity.t < hit.t)) {
            first_t = drawn ? first_t : entity.t;
            float32_t colour[3];
            shade_box_colour(&context, &entity_boxes[entity.box], entity_textures, &entity, direction, colour);
            shade_add(sum, through, colour, sky, drawn ? shade_fog_behind(post, direction[2], first_t, entity.t) : 0.0F,
                      1.0F);
            result.t = first_t;
            break;
        }
        if (face == TRAVERSE_FACE_MISS && lod_hits != nullptr && lod_hits[index].material != LOD_NO_MATERIAL) {
            /* Past level 0, at its edge or at this pixel's d_1, and on to the far field: the same ray, which lod_frame has already followed. */
            const lod_hit_t far = lod_hits[index];
            first_t = drawn ? first_t : far.t;
            float32_t colour[3] = {SHADE_MISSING_R, SHADE_MISSING_G, SHADE_MISSING_B};
            const uint32_t texture = far.material < material_count ? faces[far.material * SHADE_FACES + far.face]
                                                                   : (uint32_t)SHADE_NO_TEXTURE;
            if (texture != SHADE_NO_TEXTURE) {
                const bool water = see_through && far.material == water_material;
                shade_far_colour(sun, textures[texture].mean_rgba, water && tint,
                                 water && (features & SHADE_FEATURE_WATER_REFLECTION) != 0u,
                                 water && last_material == water_material, &far, direction, sky, colour);
            }
            shade_add(sum, through, colour, sky, drawn ? shade_fog_behind(post, direction[2], first_t, far.t) : 0.0F,
                      1.0F);
            result.t = first_t;
            break;
        }
        if (face == TRAVERSE_FACE_MISS) {
            if (drawn) {
                /* Post draws the sky only where nothing was drawn, so here it is this pixel's to draw. */
                post_sky_discs(post, direction, sky);
                const float32_t none[3] = {0.0F, 0.0F, 0.0F};
                shade_add(sum, through, sky, none, 0.0F, 1.0F);
                result.t = first_t;
            } else {
                result.t = TRAVERSE_COARSE_SKY;
            }
            break;
        }
        shade_surface_t surface;
        shade_surface_read(&context, camera->eye_frac, direction, &hit, hit.t, &surface);
        if (surface.texture == nullptr) {
            first_t = drawn ? first_t : hit.t;
            const float32_t missing[3] = {SHADE_MISSING_R, SHADE_MISSING_G, SHADE_MISSING_B};
            shade_add(sum, through, missing, sky, drawn ? shade_fog_behind(post, direction[2], first_t, hit.t) : 0.0F,
                      1.0F);
            result.t = first_t;
            break;
        }
        const bool may_go_on = layer + 1u < SHADE_ALPHA_LAYERS;
        const uint32_t alpha = surface.texel[3];
        const bool own_kind =
            see_through && shade_own_kind_behind(last_material, last_offset, surface.material, hit.offset, face);
        const bool is_water = see_through && may_go_on && surface.material == water_material;
        float32_t next_t = hit.t;
        if (own_kind && may_go_on && !is_water) {
            /* The far side of what it just left is no surface, and what it knew of that voxel is true of this one. */
        } else if (alpha == 0u && (features & SHADE_FEATURE_ALPHA) != 0u && may_go_on) {
            last_material = surface.material == glass_material ? surface.material : (uint32_t)SHADE_NO_MATERIAL;
        } else {
            first_t = drawn ? first_t : hit.t;
            const float32_t fog = drawn ? shade_fog_behind(post, direction[2], first_t, hit.t) : 0.0F;
            drawn = true;
            const bool part_covered = see_through && may_go_on && alpha != SHADE_ALPHA_OPAQUE;
            /* From inside water, or going on through water from water, there is no face: only what is behind. */
            const bool has_face = !is_water || (face != TRAVERSE_FACE_INSIDE && !own_kind);
            only_water = only_water && is_water;
            float32_t light[3] = {0.0F, 0.0F, 0.0F};
            /* What AO and the sun ray will find on a last surface is added by their own kernels. With no shadows there is no ray, and the
             * sun is here. */
            const bool last_surface = defer && !is_water && !part_covered;
            const float32_t visible = last_surface && ao_on ? 0.0F : 1.0F;
            /* On the first surface, shade_water_kernel has already asked. */
            const bool water_asked = is_water && has_face && layer == 0u;
            const shade_pixel_t asked = water_asked ? out[index] : shade_pixel_t{};
            if (water_asked) {
                shade_surface_light_near(sun, surface.face, surface.stored, asked.t, 1.0F, light);
            } else if (last_surface && (features & SHADE_FEATURE_SHADOW) != 0u) {
                shade_surface_light_near(sun, surface.face, surface.stored, 0.0F, visible, light);
            } else if (has_face) {
                shade_surface_lit(&context, &hit, &surface, visible, light, counts, (uint32_t)index);
            }
            if (is_water && has_face && (features & SHADE_FEATURE_WATER_REFLECTION) != 0u) {
                const float32_t share = shade_water_fresnel(fabsf(direction[face >> 1]));
                float32_t mirror[3] = {asked.rgb[0], asked.rgb[1], asked.rgb[2]};
                if (!water_asked) {
                    shade_reflection(&context, &hit, &surface, direction, first_t, mirror, counts, (uint32_t)index);
                }
                shade_add(sum, through, mirror, sky, 0.0F, share);
                for (uint32_t i = 0u; i < 3u; i += 1u) {
                    through[i] *= 1.0F - share;
                }
            }
            const bool cracked = mark->crack_frame != 0u && face < SHADE_FACES &&
                                 hit.offset[0] == mark->crack_cell[0] && hit.offset[1] == mark->crack_cell[1] &&
                                 hit.offset[2] == mark->crack_cell[2];
            if (!is_water && !part_covered) {
                float32_t colour[3];
                shade_texel_colour(surface.texture, surface.texel, colour);
                if (cracked) {
                    shade_crack_over(&context, &entity_textures[ENTITY_RAYS_TEXTURE_CRACK], mark->crack_frame, &surface,
                                     hit.t, colour);
                }
                if (last_surface) {
                    shade_ao_work_t work;
                    for (uint32_t i = 0u; i < 3u; i += 1u) {
                        work.share[i] = through[i] * (1.0F - fog) * colour[i];
                    }
                    ((shade_ao_work_t *)(void *)ao_next)[index] = work;
                    hits[index] = hit;
                    deferred = true;
                }
                for (uint32_t i = 0u; i < 3u; i += 1u) {
                    colour[i] *= light[i];
                }
                shade_add(sum, through, colour, sky, fog, 1.0F);
                result.t = first_t;
                break;
            }
            if (has_face) {
                float32_t colour[3];
                for (uint32_t i = 0u; i < 3u; i += 1u) {
                    colour[i] = shade_srgb_byte(surface.texel[i]);
                }
                if (cracked) {
                    shade_crack_over(&context, &entity_textures[ENTITY_RAYS_TEXTURE_CRACK], mark->crack_frame, &surface,
                                     hit.t, colour);
                }
                for (uint32_t i = 0u; i < 3u; i += 1u) {
                    colour[i] *= light[i];
                }
                const float32_t cover = (float32_t)alpha / 255.0F;
                shade_add(sum, through, colour, sky, fog, cover);
                for (uint32_t i = 0u; i < 3u; i += 1u) {
                    through[i] *= 1.0F - cover;
                }
            }
            last_material = surface.material;
            if (is_water) {
                shade_water_walk_t walk;
                ray.t_start = hit.t;
                shade_water_through(view, &ray, &hit, surface.material,
                                    tint ? SHADE_WATER_SEEN_BLOCKS : TRAVERSE_COARSE_SKY, nullptr, &walk);
                if (tint) {
                    /* It gives back by the light on a top of that water with all the sun: there is no second ray for this. */
                    float32_t full[3], passed[3], given[3];
                    shade_surface_light(sun, TRAVERSE_FACE_POS_Z, surface.stored, 1.0F, full);
                    /* A box in the water is as far as this ray looks through it. */
                    shade_water_tint(fminf(walk.out, entity.t) - hit.t, passed, given);
                    for (uint32_t i = 0u; i < 3u; i += 1u) {
                        given[i] *= full[i];
                    }
                    shade_add(sum, through, given, sky, fog, 1.0F);
                    for (uint32_t i = 0u; i < 3u; i += 1u) {
                        through[i] *= passed[i];
                    }
                }
                if (post->under_water != 0u && only_water) {
                    /* An eye in the water: the surface it started in is at no distance, and post goes by how far off the water stops. An
                     * eye among kelp comes to its first water with a face and past a clear texel, and that is no different. */
                    first_t = fminf(walk.out, entity.t);
                }
                if (walk.cut_short != 0u && entity.t >= walk.out) {
                    /* Nothing that far down can be told from more water, and the tint up to here has taken nearly all of it. */
                    result.t = first_t;
                    break;
                }
                next_t = walk.last_in;
                hit.offset[0] = walk.last[0];
                hit.offset[1] = walk.last[1];
                hit.offset[2] = walk.last[2];
            }
        }
        last_offset[0] = hit.offset[0];
        last_offset[1] = hit.offset[1];
        last_offset[2] = hit.offset[2];
        ray.t_start = next_t;
        traverse_tally_t tally;
        traverse_trace_counted(
            view, &ray, nullptr, &hit,
            traverse_tally_for(&tally, counts, TRAVERSE_COUNT_ALPHA, (uint32_t)index, TRAVERSE_COUNT_NO_LEVEL, 1.0F));
    }
    result.rgb[0] = sum[0];
    result.rgb[1] = sum[1];
    result.rgb[2] = sum[2];
    out[index] = result;
    if (defer && !deferred) {
        hits[index].info = (uint16_t)TRAVERSE_FACE_MISS;
    }
}

__global__ void shade_kernel(const traverse_view_t view, const traverse_camera_t camera,
                             const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                             uint32_t material_count, uint32_t features, uint32_t water_material,
                             uint32_t glass_material, const shade_sun_t sun, const post_settings_t post,
                             shade_ao_pixel_t *ao_next, const entity_rays_box_t *entity_boxes,
                             const entity_rays_hit_t *entity_hits, const shade_texture_t *entity_textures,
                             const entity_rays_mark_t mark, const lod_hit_t *lod_hits, traverse_hit_t *hits,
                             shade_pixel_t *out) {
    shade_kernel_body(&view, &camera, textures, faces, pixels, material_count, features, water_material, glass_material,
                      &sun, &post, ao_next, entity_boxes, entity_hits, entity_textures, &mark, lod_hits, hits, out,
                      nullptr);
}

__global__ void shade_kernel_counted(const traverse_view_t view, const traverse_camera_t camera,
                                     const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                                     uint32_t material_count, uint32_t features, uint32_t water_material,
                                     uint32_t glass_material, const shade_sun_t sun, const post_settings_t post,
                                     shade_ao_pixel_t *ao_next, const entity_rays_box_t *entity_boxes,
                                     const entity_rays_hit_t *entity_hits, const shade_texture_t *entity_textures,
                                     const entity_rays_mark_t mark, const lod_hit_t *lod_hits, traverse_hit_t *hits,
                                     shade_pixel_t *out, traverse_counts_t *counts) {
    shade_kernel_body(&view, &camera, textures, faces, pixels, material_count, features, water_material, glass_material,
                      &sun, &post, ao_next, entity_boxes, entity_hits, entity_textures, &mark, lod_hits, hits, out,
                      counts);
}

/* One texel of the god rays: see README.md. */
__global__ void shade_shafts_kernel(const traverse_view_t view, const traverse_camera_t camera,
                                    const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                                    uint32_t material_count, uint32_t features, uint32_t water_material,
                                    uint32_t glass_material, const shade_sun_t sun, uint32_t frame, bool moving_noise,
                                    const shade_pixel_t *shaded, post_shaft_t *out) {
    const uint32_t shaft_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t shaft_y = blockIdx.y * blockDim.y + threadIdx.y;
    const uint32_t across = post_shafts_across(camera.width);
    if (shaft_x >= across || shaft_y >= post_shafts_across(camera.height)) {
        return;
    }
    const uint32_t pixel_x = shaft_x * 2u, pixel_y = shaft_y * 2u;
    const float32_t t = shaded[(uint64_t)pixel_y * camera.width + pixel_x].t;
    post_shaft_t result = {0.0F, t};
    const float32_t reach = fminf(t, SHADE_SHAFT_REACH);
    if (reach > 0.0F && sun.direct[0] + sun.direct[1] + sun.direct[2] > 0.0F) {
        float32_t direction[3];
        traverse_camera_direction(&camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
        const shade_context_t context = {
            &view,
            &camera,
            textures,
            faces,
            pixels,
            &sun,
            nullptr,
            shade_pixel_tangent(&camera),
            material_count,
            features,
            (uint16_t)water_material,
            (uint16_t)glass_material,
        };
        const float32_t noise = shade_shaft_noise(shaft_x, shaft_y, moving_noise ? frame : 0u);
        const float32_t step = reach / (float32_t)SHADE_SHAFT_STEPS;
        float32_t amount = 0.0F;
        for (uint32_t i = 0u; i < SHADE_SHAFT_STEPS; i += 1u) {
            const float32_t from = step * (float32_t)i;
            const float32_t at = from + step * noise;
            shade_surface_t surface = {};
            traverse_hit_t place = {};
            place.t = at;
            place.info = (uint16_t)TRAVERSE_FACE_POS_Z;
            surface.face = TRAVERSE_FACE_POS_Z;
            for (uint32_t a = 0u; a < 3u; a += 1u) {
                const float32_t coordinate = camera.eye_frac[a] + direction[a] * at;
                const float32_t cell = floorf(coordinate);
                place.offset[a] = (int16_t)cell;
                surface.local[a] = coordinate - cell;
            }
            amount += shade_shaft_part(from, from + step) *
                      shade_sun_seen(&context, &place, &surface, nullptr, TRAVERSE_COUNT_NO_PIXEL);
        }
        const float32_t cosine =
            direction[0] * sun.direction[0] + direction[1] * sun.direction[1] + direction[2] * sun.direction[2];
        result.amount = amount * shade_shaft_phase(cosine);
    }
    out[(uint64_t)shaft_y * across + shaft_x] = result;
}

static gpu_status_t shade_refuse(gpu_error_t *error, const char *what) {
    *error = gpu_error_t{what, nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
    return GPU_STATUS_INVALID_ARGUMENT;
}

/* A block that is there, and as large as this asks. */
static bool shade_block_fits(const device_block_t *block, uint64_t bytes) {
    return block != nullptr && block->bytes >= bytes;
}

gpu_status_t shade_frame(gpu_error_t *error, const shade_scene_t *scene, const post_settings_t *post,
                         const traverse_world_t *world, const traverse_camera_t *camera, const device_block_t *hits,
                         const device_block_t *pixels) {
    return shade_frame_entities(error, scene, post, world, camera, hits, pixels, nullptr);
}

gpu_status_t shade_frame_entities(gpu_error_t *error, const shade_scene_t *scene, const post_settings_t *post,
                                  const traverse_world_t *world, const traverse_camera_t *camera,
                                  const device_block_t *hits, const device_block_t *pixels,
                                  const entity_rays_scene_t *entities) {
    return shade_frame_marked(error, scene, post, world, camera, hits, pixels, entities, nullptr);
}

const char *shade_span_name(shade_span_t span) {
    static const char *const names[(uint32_t)SHADE_SPAN_COUNT] = {"water", "surfaces", "sun", "ao", "ao_fresh"};
    return (uint32_t)span < (uint32_t)SHADE_SPAN_COUNT ? names[(uint32_t)span] : "?";
}

/* The end of every span from first on: what did not run is a span of nothing, so the marks always come in order. */
static gpu_status_t shade_marks_from(gpu_error_t *error, const shade_marks_t *marks, uint32_t first) {
    if (marks == nullptr) {
        return GPU_STATUS_OK;
    }
    for (uint32_t i = first; i + 1u < (uint32_t)SHADE_SPAN_COUNT; i += 1u) {
        const gpu_status_t status = device_memory_check(error, cudaEventRecord((cudaEvent_t)marks->borrowed_events[i]),
                                                        "shade: reading the card's clock");
        if (status != GPU_STATUS_OK) {
            return status;
        }
    }
    return GPU_STATUS_OK;
}

/* One span's end, and nothing more. */
static gpu_status_t shade_mark(gpu_error_t *error, const shade_marks_t *marks, uint32_t span) {
    if (marks == nullptr) {
        return GPU_STATUS_OK;
    }
    return device_memory_check(error, cudaEventRecord((cudaEvent_t)marks->borrowed_events[span]),
                               "shade: reading the card's clock");
}

gpu_status_t shade_frame_marked(gpu_error_t *error, const shade_scene_t *scene, const post_settings_t *post,
                                const traverse_world_t *world, const traverse_camera_t *camera,
                                const device_block_t *hits, const device_block_t *pixels,
                                const entity_rays_scene_t *entities, shade_marks_t *marks) {
    if (marks != nullptr) {
        marks->fresh_count = nullptr;
    }
    if (camera->width == 0u || camera->height == 0u) {
        return shade_refuse(error, "shade: the camera has no pixels");
    }
    if (hits->bytes < traverse_hits_bytes(camera->width, camera->height)) {
        return shade_refuse(error, "shade: the hits are too small for the camera");
    }
    if (pixels->bytes < shade_pixels_bytes(camera->width, camera->height)) {
        return shade_refuse(error, "shade: the pixel target is too small for the camera");
    }
    if (scene->borrowed_textures->bytes < (uint64_t)scene->texture_count * sizeof(shade_texture_t) ||
        scene->borrowed_faces->bytes < (uint64_t)scene->material_count * SHADE_FACES * sizeof(uint16_t)) {
        return shade_refuse(error, "shade: a table is smaller than the scene says");
    }
    const bool ao_on = (scene->features & SHADE_FEATURE_AO) != 0u;
    post_temporal_t temporal = {};
    const shade_ao_pixel_t *ao_last = nullptr;
    shade_ao_pixel_t *ao_next = nullptr;
    const uint64_t ao_bytes = shade_ao_bytes(camera->width, camera->height);
    const bool ao_blocks = scene->borrowed_temporal != nullptr &&
                           (ao_on || scene->borrowed_temporal->sun_apart != 0u) &&
                           shade_block_fits(scene->borrowed_temporal->borrowed_ao_last, ao_bytes) &&
                           shade_block_fits(scene->borrowed_temporal->borrowed_ao_next, ao_bytes);
    if (ao_on && !ao_blocks) {
        return shade_refuse(error, "shade: AO is on, and its history is missing or too small for the camera");
    }
    if (ao_blocks) {
        temporal = *scene->borrowed_temporal;
        ao_last = (const shade_ao_pixel_t *)(const void *)device_memory_pointer(temporal.borrowed_ao_last);
        ao_next = (shade_ao_pixel_t *)(void *)device_memory_pointer(temporal.borrowed_ao_next);
    }
    const bool any_box = entities != nullptr && entities->box_count != 0u;
    const bool any_crack = entities != nullptr && entities->mark.crack_frame != 0u;
    if ((any_box || any_crack) &&
        !shade_block_fits(entities->borrowed_lists, entity_rays_textures_offset(camera->width, camera->height))) {
        return shade_refuse(error, "shade: the entity lists are missing or too small for the camera");
    }
    if (any_box && !shade_block_fits(entities->borrowed_hits, entity_rays_hits_bytes(camera->width, camera->height))) {
        return shade_refuse(error, "shade: the entity hits are missing or too small for the camera");
    }
    if (scene->borrowed_lod_hits != nullptr &&
        scene->borrowed_lod_hits->bytes < lod_hits_bytes(camera->width, camera->height)) {
        return shade_refuse(error, "shade: the far field's hits are too small for the camera");
    }
    const uint8_t *const lists = any_box || any_crack ? device_memory_pointer(entities->borrowed_lists) : nullptr;
    traverse_view_t view;
    const gpu_status_t status = traverse_view_from(error, world, &view);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    const dim3 grid(traverse_tiles_across(camera->width), traverse_tiles_across(camera->height));
    const dim3 tile(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS);
    const device_block_t *const counts_block = marks != nullptr ? marks->borrowed_counts : nullptr;
    if (counts_block != nullptr && counts_block->bytes < sizeof(traverse_counts_t)) {
        return shade_refuse(error, "shade: the counts block is too small");
    }
    /* Plan 009 phase 01: with counts, the counted copy of each kernel; the same work and the same picture. */
    traverse_counts_t *const counts =
        counts_block != nullptr ? (traverse_counts_t *)(void *)device_memory_pointer(counts_block) : nullptr;
    const shade_texture_t *const textures =
        (const shade_texture_t *)(const void *)device_memory_pointer(scene->borrowed_textures);
    const uint16_t *const faces = (const uint16_t *)(const void *)device_memory_pointer(scene->borrowed_faces);
    const uint8_t *const texels = device_memory_pointer(scene->borrowed_pixels);
    traverse_hit_t *const hit_list = (traverse_hit_t *)(void *)device_memory_pointer(hits);
    shade_pixel_t *const out = (shade_pixel_t *)(void *)device_memory_pointer(pixels);
    const entity_rays_hit_t *const entity_hits =
        any_box ? (const entity_rays_hit_t *)(const void *)device_memory_pointer(entities->borrowed_hits) : nullptr;
    const entity_rays_mark_t mark = entities != nullptr ? entities->mark : entity_rays_mark_t{};
    if ((scene->features & SHADE_FEATURE_TRANSPARENCY) != 0u) {
        if (counts != nullptr) {
            shade_water_kernel_counted<<<grid, tile>>>(view, *camera, textures, faces, texels, scene->material_count,
                                                       scene->features, scene->water_material, scene->glass_material,
                                                       scene->sun, *post, entity_hits, mark, hit_list, out, counts);
        } else {
            shade_water_kernel<<<grid, tile>>>(view, *camera, textures, faces, texels, scene->material_count,
                                               scene->features, scene->water_material, scene->glass_material,
                                               scene->sun, *post, entity_hits, mark, hit_list, out);
        }
        const gpu_status_t water_started = device_memory_check(error, cudaGetLastError(), "shade: starting water");
        if (water_started != GPU_STATUS_OK) {
            return water_started;
        }
    }
    const gpu_status_t water_marked = shade_mark(error, marks, SHADE_SPAN_WATER);
    if (water_marked != GPU_STATUS_OK) {
        return water_marked;
    }
    const entity_rays_box_t *const entity_boxes = any_box ? (const entity_rays_box_t *)(const void *)lists : nullptr;
    const shade_texture_t *const entity_textures =
        lists != nullptr ? (const shade_texture_t *)(const void *)(lists + entity_rays_textures_offset(camera->width,
                                                                                                       camera->height))
                         : nullptr;
    const lod_hit_t *const lod_hits =
        scene->borrowed_lod_hits != nullptr
            ? (const lod_hit_t *)(const void *)device_memory_pointer(scene->borrowed_lod_hits)
            : nullptr;
    if (counts != nullptr) {
        shade_kernel_counted<<<grid, tile>>>(view, *camera, textures, faces, texels, scene->material_count,
                                             scene->features, scene->water_material, scene->glass_material, scene->sun,
                                             *post, ao_next, entity_boxes, entity_hits, entity_textures, mark, lod_hits,
                                             hit_list, out, counts);
    } else {
        shade_kernel<<<grid, tile>>>(view, *camera, textures, faces, texels, scene->material_count, scene->features,
                                     scene->water_material, scene->glass_material, scene->sun, *post, ao_next,
                                     entity_boxes, entity_hits, entity_textures, mark, lod_hits, hit_list, out);
    }
    const gpu_status_t started = device_memory_check(error, cudaGetLastError(), "shade: starting the kernel");
    if (started != GPU_STATUS_OK) {
        return started;
    }
    if (!ao_blocks) {
        return shade_marks_from(error, marks, SHADE_SPAN_SURFACES);
    }
    const gpu_status_t surfaces_marked = shade_mark(error, marks, SHADE_SPAN_SURFACES);
    if (surfaces_marked != GPU_STATUS_OK) {
        return surfaces_marked;
    }
    if ((scene->features & SHADE_FEATURE_SHADOW) != 0u) {
        const shade_ao_work_t *const work = (const shade_ao_work_t *)(const void *)ao_next;
        if (counts != nullptr) {
            shade_sun_kernel_counted<<<grid, tile>>>(view, *camera, textures, faces, texels, scene->material_count,
                                                     scene->features, scene->water_material, scene->glass_material,
                                                     scene->sun, work, hit_list, out, counts);
        } else {
            shade_sun_kernel<<<grid, tile>>>(view, *camera, textures, faces, texels, scene->material_count,
                                             scene->features, scene->water_material, scene->glass_material, scene->sun,
                                             work, hit_list, out);
        }
        const gpu_status_t sun_started = device_memory_check(error, cudaGetLastError(), "shade: starting the sun rays");
        if (sun_started != GPU_STATUS_OK) {
            return sun_started;
        }
    }
    if (!ao_on) {
        return shade_marks_from(error, marks, SHADE_SPAN_SUN);
    }
    const gpu_status_t sun_marked = shade_mark(error, marks, SHADE_SPAN_SUN);
    if (sun_marked != GPU_STATUS_OK) {
        return sun_marked;
    }
    /* Plan 008 phase 04: the AO cache, and the table of blocks opaque on every face that render_frame_t's face table carries after the faces,
     * for the corner estimate. A scene without either goes on without it. */
    shade_ao_cache_args_t cache = {nullptr, nullptr};
    if (temporal.borrowed_ao_cache != nullptr && temporal.borrowed_ao_cache->bytes >= shade_ao_cache_bytes()) {
        cache.words = (uint64_t *)(void *)device_memory_pointer(temporal.borrowed_ao_cache);
        /* Its ages count frames modulo 2^21 and the newest wins, so once in that many frames (9.7 hours at 60) it starts again empty. */
        if (temporal.frame != 0u && (temporal.frame & SHADE_AO_CACHE_AGE_MASK) == 0u) {
            const gpu_status_t cleared = device_memory_check(
                error, cudaMemsetAsync(cache.words, 0, shade_ao_cache_bytes()), "shade: emptying the AO cache");
            if (cleared != GPU_STATUS_OK) {
                return cleared;
            }
        }
    }
    const uint64_t stops_at = lod_stops_offset((uint64_t)scene->material_count * SHADE_FACES * sizeof(uint16_t));
    if (scene->borrowed_faces->bytes >= stops_at + sizeof(traverse_stops_t)) {
        cache.stops = (const uint32_t *)(const void *)((const uint8_t *)faces + stops_at);
    }
    const uint64_t pixel_count = (uint64_t)camera->width * camera->height;
    uint32_t *const fresh = (uint32_t *)(void *)(ao_next + pixel_count);
    uint32_t *const fresh_count = fresh + pixel_count;
    shade_ao_fresh_clear_kernel<<<1, 1>>>(fresh_count);
    if (counts != nullptr) {
        shade_ao_kernel_counted<<<grid, tile>>>(view, *camera, textures, faces, texels, scene->material_count,
                                                scene->features, scene->water_material, scene->glass_material,
                                                scene->sun, temporal, cache, ao_last, ao_next, fresh, fresh_count,
                                                hit_list, out, counts);
    } else {
        shade_ao_kernel<<<grid, tile>>>(view, *camera, textures, faces, texels, scene->material_count, scene->features,
                                        scene->water_material, scene->glass_material, scene->sun, temporal, cache,
                                        ao_last, ao_next, fresh, fresh_count, hit_list, out);
    }
    const gpu_status_t ao_started = device_memory_check(error, cudaGetLastError(), "shade: starting AO's history pass");
    if (ao_started != GPU_STATUS_OK) {
        return ao_started;
    }
    const gpu_status_t ao_marked = shade_mark(error, marks, SHADE_SPAN_AO);
    if (ao_marked != GPU_STATUS_OK) {
        return ao_marked;
    }
    if (marks != nullptr) {
        marks->fresh_count = fresh_count;
    }
    /* How long the list is, only the card knows yet, so this is as long as it could be, and what is past its end returns at once. Waiting to
     * hear, and launching only what is on it, came to the same: see the phase 17 stage 3 file. */
    const uint32_t per_warp = 32u;
    const uint32_t warps = (uint32_t)((pixel_count + per_warp - 1u) / per_warp);
    if (counts != nullptr) {
        shade_ao_fresh_kernel_counted<<<warps, per_warp>>>(
            view, *camera, textures, faces, texels, scene->material_count, scene->features, scene->water_material,
            scene->glass_material, scene->sun, temporal.frame, temporal.flags, cache, ao_next, fresh, fresh_count,
            hit_list, out, counts);
    } else {
        shade_ao_fresh_kernel<<<warps, per_warp>>>(view, *camera, textures, faces, texels, scene->material_count,
                                                   scene->features, scene->water_material, scene->glass_material,
                                                   scene->sun, temporal.frame, temporal.flags, cache, ao_next, fresh,
                                                   fresh_count, hit_list, out);
    }
    return device_memory_check(error, cudaGetLastError(), "shade: starting AO");
}

gpu_status_t shade_shafts_frame(gpu_error_t *error, const shade_scene_t *scene, const traverse_world_t *world,
                                const traverse_camera_t *camera, const device_block_t *pixels) {
    if (camera->width == 0u || camera->height == 0u) {
        return shade_refuse(error, "shade: the camera has no pixels");
    }
    const post_temporal_t *const temporal = scene->borrowed_temporal;
    if (temporal == nullptr ||
        !shade_block_fits(temporal->borrowed_shafts, post_shafts_bytes(camera->width, camera->height))) {
        return shade_refuse(error, "shade: god rays are on, and their target is missing or too small for the camera");
    }
    if (pixels->bytes < shade_pixels_bytes(camera->width, camera->height)) {
        return shade_refuse(error, "shade: the pixels are too small for the camera");
    }
    traverse_view_t view;
    const gpu_status_t status = traverse_view_from(error, world, &view);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    const uint32_t across = post_shafts_across(camera->width), down = post_shafts_across(camera->height);
    const dim3 grid(traverse_tiles_across(across), traverse_tiles_across(down));
    shade_shafts_kernel<<<grid, dim3(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS)>>>(
        view, *camera, (const shade_texture_t *)(const void *)device_memory_pointer(scene->borrowed_textures),
        (const uint16_t *)(const void *)device_memory_pointer(scene->borrowed_faces),
        device_memory_pointer(scene->borrowed_pixels), scene->material_count, scene->features, scene->water_material,
        scene->glass_material, scene->sun, temporal->frame, (scene->features & SHADE_FEATURE_TAA) != 0u,
        (const shade_pixel_t *)(const void *)device_memory_pointer(pixels),
        (post_shaft_t *)(void *)device_memory_pointer(temporal->borrowed_shafts));
    return device_memory_check(error, cudaGetLastError(), "shade: starting the god rays");
}
