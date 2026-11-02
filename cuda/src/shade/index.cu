/* See README.md. One colour per pixel of the hits: from the near surface to the far, the texture of each face a ray meets, at a mip
 * chosen by distance, under that face's brightness, the light stored in its voxel and what its one sun ray saw, for as far as what
 * it meets lets the ray on. Water reflects one ray, and takes colour by how far a ray goes in it. */
#include "shade/index.hu"

#include <math.h>

/* What every step of a pixel reads, so that the functions below do not take a dozen arguments each. Lives on the kernel's stack. */
typedef struct shade_context_t {
    const traverse_view_t *view;     /* 0 */
    const traverse_camera_t *camera; /* 8 */
    const shade_texture_t *textures; /* 16 */
    const uint16_t *faces;           /* 24 */
    const uint8_t *pixels;           /* 32 */
    const shade_sun_t *sun;          /* 40 */
    const post_settings_t *post;     /* 48 */
    float32_t tangent;               /* 56 */
    uint32_t material_count;         /* 60 */
    uint32_t features;               /* 64 */
    uint16_t water_material;         /* 68 */
    uint16_t glass_material;         /* 70 */
} shade_context_t;
static_assert(sizeof(shade_context_t) == 72);

/* A surface a ray has reached. */
typedef struct shade_surface_t {
    const shade_texture_t *texture; /* 0: nullptr when the block has none, or its material is past the table */
    const uint8_t *texel;           /* 8: its four bytes */
    float32_t local[3];             /* 16: where on the voxel, from its low corner */
    uint32_t face;                  /* 28 */
    uint32_t material;              /* 32 */
    uint32_t stored;                /* 36: the light its voxel holds, as far as the setting lets it */
} shade_surface_t;
static_assert(sizeof(shade_surface_t) == 40);

/* How a ray got through a body of water. */
typedef struct shade_water_walk_t {
    float32_t
        last_in; /* 0: where it came into the last voxel of water: going on from here takes only what is past that voxel */
    float32_t out;      /* 4: where it left that voxel */
    int16_t last[3];    /* 8: that voxel, as a hit's offset */
    uint16_t cut_short; /* 14: it went on past SHADE_WATER_SEEN_BLOCKS, and out is only how far it looked */
} shade_water_walk_t;
static_assert(sizeof(shade_water_walk_t) == 16);

/* A texel's four bytes. */
static __device__ __forceinline__ const uint8_t *shade_texel(const shade_texture_t *texture, const uint8_t *pixels,
                                                             uint32_t level, float32_t u, float32_t v) {
    const uint32_t width = shade_level_dimension(texture->width, level);
    const uint32_t height = shade_level_dimension(texture->height, level);
    const uint32_t x = shade_texel_of(u, width), y = shade_texel_of(v, height);
    return pixels + texture->pixels_offset + texture->level_offset[level] + ((uint64_t)y * width + x) * 4u;
}

/* A texel in linear light, with what shows through a part-covered one in its place, for a ray that stops on it. */
static __device__ __forceinline__ void shade_texel_colour(const shade_texture_t *texture, const uint8_t *texel,
                                                          float32_t out[3]) {
    const uint32_t alpha = texel[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float32_t seen = shade_srgb_to_linear((float32_t)texel[i] / 255.0F);
        if (alpha == SHADE_ALPHA_OPAQUE) {
            out[i] = seen;
        } else {
            const float32_t behind = shade_srgb_to_linear((float32_t)texture->mean_rgba[i] / 255.0F);
            const float32_t cover = (float32_t)alpha / 255.0F;
            out[i] = behind + (seen - behind) * cover;
        }
    }
}

/* What a hit met, for a ray from eye_frac along direction. mip_t chooses the level. */
static __device__ __forceinline__ void shade_surface_read(const shade_context_t *context, const float32_t eye_frac[3],
                                                          const float32_t direction[3], const traverse_hit_t *hit,
                                                          float32_t mip_t, shade_surface_t *out) {
    out->face = (uint32_t)hit->info & TRAVERSE_INFO_FACE_MASK;
    out->material = hit->value & TRAVERSE_VALUE_MATERIAL_MASK;
    out->stored = (context->features & SHADE_FEATURE_LIGHT) != 0u ? (hit->value >> TRAVERSE_VALUE_LIGHT_SHIFT) & 0xFFu
                                                                  : (uint32_t)SHADE_LIGHT_OPEN_SKY;
    out->texture = nullptr;
    out->texel = nullptr;
    const uint32_t lookup_face = out->face == TRAVERSE_FACE_INSIDE ? (uint32_t)TRAVERSE_FACE_POS_Z : out->face;
    const uint32_t texture_index = out->material < context->material_count
                                       ? context->faces[out->material * SHADE_FACES + lookup_face]
                                       : (uint32_t)SHADE_NO_TEXTURE;
    /* From the voxel's own corner, so that this is as exact far from the origin as near it. */
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        out->local[i] = eye_frac[i] + direction[i] * hit->t - (float32_t)hit->offset[i];
    }
    if (texture_index == SHADE_NO_TEXTURE) {
        return;
    }
    float32_t u = 0.0F, v = 0.0F;
    shade_face_uv(out->face, out->local, &u, &v);
    out->texture = &context->textures[texture_index];
    const uint32_t level = shade_mip_level(mip_t, context->tangent, out->texture->width, out->texture->level_count);
    out->texel = shade_texel(out->texture, context->pixels, level, u, v);
}

/* The material of the block in a cell, or SHADE_NO_MATERIAL for air and for what is not loaded. The tree's three levels, read once:
 * the ray loop's own form of this cannot stop at a change of material. */
static __device__ __forceinline__ uint32_t shade_cell_material(const traverse_view_t *view, const int32_t cell[3]) {
    const int32_t x = cell[0], y = cell[1], z = cell[2];
    if (((uint32_t)(x - view->window_min_block[0]) >= view->window_blocks) |
        ((uint32_t)(y - view->window_min_block[1]) >= view->window_blocks) |
        ((uint32_t)z >= (uint32_t)TRAVERSE_HEIGHT_BLOCKS)) {
        return SHADE_NO_MATERIAL;
    }
    const world_buffers_column_t *const column = &view->borrowed_table[traverse_slot(view, x >> 4, y >> 4)];
    if (z >= traverse_column_top(column, x >> 4, y >> 4) || ((column->section_mask >> (z >> 4)) & 1u) == 0u) {
        return SHADE_NO_MATERIAL;
    }
    const uint8_t *const encoded = traverse_section_bytes(view, column->sections[z >> 4]);
    const world_buffers_section_t header = *(const world_buffers_section_t *)(const void *)encoded;
    if (header.brick_count == 0u) {
        return traverse_section_value(encoded, &header, 0u) & TRAVERSE_VALUE_MATERIAL_MASK;
    }
    const int32_t brick = ((x >> 2) & 3) | (((y >> 2) & 3) << 2) | (((z >> 2) & 3) << 4);
    if (((header.brick_mask >> brick) & 1u) == 0u) {
        return SHADE_NO_MATERIAL;
    }
    const uint8_t *const after_header = encoded + sizeof(world_buffers_section_t);
    const uint32_t rank = (uint32_t)__popcll(header.brick_mask & ((1ull << brick) - 1ull));
    const uint64_t brick_mask = ((const uint64_t *)(const void *)after_header)[rank];
    const int32_t voxel = (x & 3) | ((y & 3) << 2) | ((z & 3) << 4);
    if (((brick_mask >> voxel) & 1u) == 0u) {
        return SHADE_NO_MATERIAL;
    }
    const uint8_t *const after_words = after_header + (uint32_t)header.brick_count * 8u;
    const uint32_t first = ((const uint16_t *)(const void *)after_words)[rank];
    const uint32_t place = first + (uint32_t)__popcll(brick_mask & ((1ull << voxel) - 1ull));
    return traverse_section_value(encoded, &header, place) & TRAVERSE_VALUE_MATERIAL_MASK;
}

/* From a voxel of water a ray has reached, on along it, voxel by voxel, to where the water stops: a voxel of anything else, or air.
 * A sea is a great many voxels of one block with nothing to see between them, and starting a trace for each would cost a ray per
 * block of depth. Every plane is worked out from the eye, whole number first, as the ray loop's are, so that going on from last_in
 * gives the same bits and finds what is behind and nothing before. */
static __device__ __forceinline__ void shade_water_through(const traverse_view_t *view, const traverse_trace_ray_t *ray,
                                                           const traverse_hit_t *hit, uint32_t water, float32_t reach,
                                                           shade_water_walk_t *out) {
    int32_t cell[3];
    int32_t plane_offset[3];
    float32_t inverse[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        cell[i] = ray->eye_cell[i] + (int32_t)hit->offset[i];
        plane_offset[i] = ray->direction[i] > 0.0F ? 1 : 0;
        inverse[i] = 1.0F / ray->direction[i];
    }
    out->last_in = hit->t;
    out->out = hit->t;
    out->cut_short = 0u;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        out->last[i] = hit->offset[i];
    }
    for (uint32_t step = 0u; step < view->max_steps; step += 1u) {
        float32_t nearest = 1e30F;
        uint32_t axis = 0u;
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            const float32_t plane = (float32_t)(cell[i] + plane_offset[i] - ray->eye_cell[i]);
            const float32_t t = (plane - ray->eye_frac[i]) * inverse[i];
            if (t < nearest) {
                nearest = t;
                axis = i;
            }
        }
        out->out = nearest;
        if (nearest - hit->t >= reach) {
            out->cut_short = 1u;
            return;
        }
        cell[axis] += ray->direction[axis] > 0.0F ? 1 : -1;
        if (shade_cell_material(view, cell) != water) {
            return;
        }
        out->last_in = nearest;
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            out->last[i] = (int16_t)(cell[i] - ray->eye_cell[i]);
        }
    }
}

/* How much of the sun reaches this point of a surface, from 0 to 1: the one sun ray. It starts from the surface's own voxel, so it
 * is as exact far from the eye as near it, and goes on through what is see-through as a primary ray does, with a part-covered
 * texel taking its share, and a body of water by how much of it there is. from_t, how far off the surface is seen, chooses the mip
 * of whatever is in the way, so that a far tree's shadow has the same holes as its leaves. */
static __device__ __forceinline__ float32_t shade_sun_seen(const shade_context_t *context, const traverse_hit_t *from,
                                                           const shade_surface_t *surface) {
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
    float32_t seen = 1.0F;
    uint32_t last_material = SHADE_NO_MATERIAL;
    int16_t last_offset[3] = {0, 0, 0};
    for (uint32_t layer = 0u; layer < SHADE_SUN_LAYERS; layer += 1u) {
        traverse_hit_t hit;
        traverse_trace(context->view, &ray, &hit);
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
                                tint ? SHADE_WATER_SEEN_BLOCKS : TRAVERSE_COARSE_SKY, &walk);
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

/* The light on a surface, with its one sun ray if it wants one. */
static __device__ __forceinline__ void shade_surface_lit(const shade_context_t *context, const traverse_hit_t *hit,
                                                         const shade_surface_t *surface, float32_t light[3]) {
    float32_t sun_seen = 1.0F;
    const shade_sun_t *const sun = context->sun;
    if ((context->features & SHADE_FEATURE_SHADOW) != 0u &&
        shade_sun_towards(sun, surface->face, surface->stored) > 0.0F &&
        sun->direct[0] + sun->direct[1] + sun->direct[2] > 0.0F) {
        sun_seen = shade_sun_seen(context, hit, surface);
    }
    shade_surface_light(sun, surface->face, surface->stored, sun_seen, light);
}

/* What a surface of water reflects: one ray from it, mirrored in the face it came in by. Simple on purpose. What it meets has the
 * light stored in its voxel and all the sun that faces it, with no ray of its own to ask, and it goes on through holes only: what is
 * part-covered shows its mean colour, water in water included, so that this cannot go on and on. Its fog is by the whole way from
 * the eye. A ray that meets nothing sees the sky, with the sun's disc in it: that is the glitter on a lake. */
static __device__ __forceinline__ void shade_reflection(const shade_context_t *context, const traverse_hit_t *from,
                                                        const shade_surface_t *surface, const float32_t direction[3],
                                                        float32_t first_t, float32_t out[3]) {
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
    float32_t sky[3];
    post_sky(context->post, mirrored, sky);
    uint32_t last_material = SHADE_NO_MATERIAL;
    int16_t last_offset[3] = {0, 0, 0};
    for (uint32_t layer = 0u;; layer += 1u) {
        traverse_hit_t hit;
        traverse_trace(context->view, &ray, &hit);
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
        const float32_t fog = shade_fog_behind(context->post, first_t, whole_way);
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            out[i] = colour[i] + (sky[i] - colour[i]) * fog;
        }
        return;
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

__global__ void shade_kernel(const traverse_view_t view, const traverse_camera_t camera,
                             const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                             uint32_t material_count, uint32_t features, uint32_t water_material,
                             uint32_t glass_material, const shade_sun_t sun, const post_settings_t post,
                             const traverse_hit_t *hits, shade_pixel_t *out) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera.width || pixel_y >= camera.height) {
        return;
    }
    const uint64_t index = (uint64_t)pixel_y * camera.width + pixel_x;
    float32_t direction[3];
    traverse_camera_direction(&camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
    const shade_context_t context = {
        &view,
        &camera,
        textures,
        faces,
        pixels,
        &sun,
        &post,
        shade_pixel_tangent(&camera),
        material_count,
        features,
        (uint16_t)water_material,
        (uint16_t)glass_material,
    };
    const bool see_through = (features & SHADE_FEATURE_TRANSPARENCY) != 0u;
    const bool tint = (features & SHADE_FEATURE_WATER_TINT) != 0u;
    traverse_trace_ray_t ray;
    ray.eye_cell[0] = camera.eye_cell[0];
    ray.eye_cell[1] = camera.eye_cell[1];
    ray.eye_cell[2] = camera.eye_cell[2] - TRAVERSE_Z_BASE;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        ray.eye_frac[i] = camera.eye_frac[i];
        ray.direction[i] = traverse_guard(direction[i]);
    }
    ray.resume = 1u;
    traverse_hit_t hit = hits[index];
    float32_t sky[3];
    post_sky(&post, direction, sky);
    /* From near to far: sum is what is seen so far, and through how much of whatever is further on still shows. */
    float32_t sum[3] = {0.0F, 0.0F, 0.0F};
    float32_t through[3] = {1.0F, 1.0F, 1.0F};
    float32_t first_t = 0.0F;
    bool drawn = false;
    uint32_t last_material = SHADE_NO_MATERIAL;
    int16_t last_offset[3] = {0, 0, 0};
    shade_pixel_t result;
    /* Each pass is one surface. A ray never comes back to a surface, so this ends by itself, and SHADE_ALPHA_LAYERS is only what a
     * line of leaves may cost a pixel. */
    for (uint32_t layer = 0u;; layer += 1u) {
        const uint32_t face = (uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK;
        if (face == TRAVERSE_FACE_MISS) {
            if (drawn) {
                /* Post draws the sky only where nothing was drawn, so here it is this pixel's to draw. */
                post_sky_discs(&post, direction, sky);
                const float32_t none[3] = {0.0F, 0.0F, 0.0F};
                shade_add(sum, through, sky, none, 0.0F, 1.0F);
                result.t = first_t;
            } else {
                result.t = TRAVERSE_COARSE_SKY;
            }
            break;
        }
        shade_surface_t surface;
        shade_surface_read(&context, camera.eye_frac, direction, &hit, hit.t, &surface);
        if (surface.texture == nullptr) {
            first_t = drawn ? first_t : hit.t;
            const float32_t missing[3] = {SHADE_MISSING_R, SHADE_MISSING_G, SHADE_MISSING_B};
            shade_add(sum, through, missing, sky, drawn ? shade_fog_behind(&post, first_t, hit.t) : 0.0F, 1.0F);
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
            const float32_t fog = drawn ? shade_fog_behind(&post, first_t, hit.t) : 0.0F;
            drawn = true;
            const bool part_covered = see_through && may_go_on && alpha != SHADE_ALPHA_OPAQUE;
            /* From inside water, or going on through water from water, there is no face: only what is behind. */
            const bool has_face = !is_water || (face != TRAVERSE_FACE_INSIDE && !own_kind);
            float32_t light[3] = {0.0F, 0.0F, 0.0F};
            if (has_face) {
                shade_surface_lit(&context, &hit, &surface, light);
            }
            if (is_water && has_face && (features & SHADE_FEATURE_WATER_REFLECTION) != 0u) {
                const float32_t share = shade_water_fresnel(fabsf(direction[face >> 1]));
                float32_t mirror[3];
                shade_reflection(&context, &hit, &surface, direction, first_t, mirror);
                shade_add(sum, through, mirror, sky, 0.0F, share);
                for (uint32_t i = 0u; i < 3u; i += 1u) {
                    through[i] *= 1.0F - share;
                }
            }
            if (!is_water && !part_covered) {
                float32_t colour[3];
                shade_texel_colour(surface.texture, surface.texel, colour);
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
                    colour[i] = shade_srgb_to_linear((float32_t)surface.texel[i] / 255.0F) * light[i];
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
                shade_water_through(&view, &ray, &hit, surface.material,
                                    tint ? SHADE_WATER_SEEN_BLOCKS : TRAVERSE_COARSE_SKY, &walk);
                if (tint) {
                    /* It gives back by the light on a top of that water with all the sun: there is no second ray for this. */
                    float32_t full[3], passed[3], given[3];
                    shade_surface_light(&sun, TRAVERSE_FACE_POS_Z, surface.stored, 1.0F, full);
                    shade_water_tint(walk.out - hit.t, passed, given);
                    for (uint32_t i = 0u; i < 3u; i += 1u) {
                        given[i] *= full[i];
                    }
                    shade_add(sum, through, given, sky, fog, 1.0F);
                    for (uint32_t i = 0u; i < 3u; i += 1u) {
                        through[i] *= passed[i];
                    }
                }
                if (walk.cut_short != 0u) {
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
        traverse_trace(&view, &ray, &hit);
    }
    result.rgb[0] = sum[0];
    result.rgb[1] = sum[1];
    result.rgb[2] = sum[2];
    out[index] = result;
}

static gpu_status_t shade_refuse(gpu_error_t *error, const char *what) {
    *error = gpu_error_t{what, nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
    return GPU_STATUS_INVALID_ARGUMENT;
}

gpu_status_t shade_frame(gpu_error_t *error, const shade_scene_t *scene, const post_settings_t *post,
                         const traverse_world_t *world, const traverse_camera_t *camera, const device_block_t *hits,
                         const device_block_t *pixels) {
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
    traverse_view_t view;
    const gpu_status_t status = traverse_view_from(error, world, &view);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    const dim3 grid(traverse_tiles_across(camera->width), traverse_tiles_across(camera->height));
    shade_kernel<<<grid, dim3(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS)>>>(
        view, *camera, (const shade_texture_t *)(const void *)device_memory_pointer(scene->borrowed_textures),
        (const uint16_t *)(const void *)device_memory_pointer(scene->borrowed_faces),
        device_memory_pointer(scene->borrowed_pixels), scene->material_count, scene->features, scene->water_material,
        scene->glass_material, scene->sun, *post, (const traverse_hit_t *)(const void *)device_memory_pointer(hits),
        (shade_pixel_t *)(void *)device_memory_pointer(pixels));
    return device_memory_check(error, cudaGetLastError(), "shade: starting the kernel");
}
