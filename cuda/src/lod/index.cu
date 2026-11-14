/* See README.md. Rays through the coarse grids, from the finest level out: each level is a world of the same format as the blocks',
 * traced by the same loop in that level's voxels. */
#include "lod/index.hu"

#include "shade/index.hu"

#include <math.h>
#include <string.h>

/* Plan 008 phase 06: what a frame's far rays need of the turn cache. */
typedef struct lod_turn_args_t {
    lod_turn_texel_t *texels; /* 0: nullptr for no cache */
    lod_turn_tally_t *tally;  /* 8 */
    uint32_t side;            /* 16 */
    uint32_t generation;      /* 20 */
    float32_t
        spread; /* 24: how far apart, as a share of the pixel's own, two rays' starts may be for one to stand for the other */
    uint32_t reading; /* 28: bit 0: 1 on a turn; bits 8 to 14: this frame's stamp, LOD_TURN_STAMP_MASK of a count of frames */
} lod_turn_args_t;
static_assert(sizeof(lod_turn_args_t) == 32);

/* Whether a ray can go on past what it first hit: nothing hit at all, or a face whose texture has anything to see through. */
static __device__ __forceinline__ bool lod_ray_goes_on(const traverse_hit_t *hit, const shade_texture_t *textures,
                                                       const uint16_t *faces, uint32_t material_count) {
    const uint32_t info_face = (uint32_t)hit->info & TRAVERSE_INFO_FACE_MASK;
    if (info_face == TRAVERSE_FACE_MISS) {
        return true;
    }
    const uint32_t material = hit->value & TRAVERSE_VALUE_MATERIAL_MASK;
    if (material >= material_count) {
        return false;
    }
    const uint32_t face = info_face < SHADE_FACES ? info_face : (uint32_t)TRAVERSE_FACE_POS_Z;
    const uint32_t texture = faces[material * SHADE_FACES + face];
    return texture != SHADE_NO_TEXTURE && textures[texture].mean_rgba[3] != SHADE_ALPHA_OPAQUE;
}

static __device__ __forceinline__ void lod_hit_write(const lod_view_t *view, const lod_reach_t *bands,
                                                     const lod_origin_t *origin, const float32_t direction[3],
                                                     float32_t t_start, bool met_at_handover, bool shadows,
                                                     const float32_t sun[3], lod_hit_t *out, traverse_counts_t *counts,
                                                     uint32_t pixel) {
    lod_hit_t result = {TRAVERSE_COARSE_SKY, (uint16_t)LOD_NO_MATERIAL, (uint8_t)TRAVERSE_FACE_MISS,
                        (uint8_t)LOD_SUN_SEEN};
    lod_found_t found;
    if (lod_trace(view, bands, origin, direction, 0u, t_start, met_at_handover, &found, counts, TRAVERSE_COUNT_FAR,
                  pixel)) {
        result.t = found.t;
        result.material = (uint16_t)found.material;
        result.face = (uint8_t)found.face;
        if (shadows) {
            /* A face turned from the sun gets none of it whatever is in the way, so no ray is sent for it. */
            const uint32_t axis = found.face >> 1;
            const bool facing = (found.face & 1u) != 0u ? sun[axis] > 0.0F : sun[axis] < 0.0F;
            const lod_reach_t cone = shade_shadow_cone();
            result.sun = facing && !lod_sun_reaches(view, &cone, &found, sun, counts, pixel) ? (uint8_t)LOD_SUN_HIDDEN
                                                                                             : (uint8_t)LOD_SUN_SEEN;
        }
    }
    *out = result;
}

/* Whether shade's ray goes on past a surface it met, as shade_kernel decides it from the texel there: one that is not opaque, water,
 * or the far side of what it just left (own). With no pixels, by the texture's mean alpha: a ray going on wherever it could. */
static __device__ __forceinline__ bool lod_surface_goes_on(const shade_context_t *context, const float32_t direction[3],
                                                           const traverse_hit_t *hit, bool own) {
    if (context->pixels == nullptr) {
        return own || lod_ray_goes_on(hit, context->textures, context->faces, context->material_count);
    }
    shade_surface_t surface;
    shade_surface_read(context, context->camera->eye_frac, direction, hit, hit->t, &surface);
    if (surface.texture == nullptr) {
        return false;
    }
    return own || surface.material == context->water_material || surface.texel[3] != SHADE_ALPHA_OPAQUE;
}

/* Whether shade's own ray for a pixel gets past level 0 at last, at its edge or at the pixel's d_1 (t_end), where it looks at lod_hit_t: the
 * same trace from the eye, surface by surface as shade_kernel goes, past each texel it can see through and each body of water, walked as
 * shade walks it. Shade's ray can stop sooner, as on a feature turned off, but never later. Almost every ray past a leaf or a pane comes to
 * near ground, and a far ray for it would cost a trace through every level for nothing. direction is the pixel's as shade reads a surface
 * with it, and guarded is the ray's. */
static __device__ __forceinline__ bool lod_goes_out(const shade_context_t *context, const float32_t direction[3],
                                                    const float32_t guarded[3], float32_t t_end, traverse_hit_t hit,
                                                    bool water_tint, traverse_counts_t *counts, uint32_t pixel,
                                                    float32_t *out_t) {
    traverse_trace_ray_t ray;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        ray.eye_cell[i] = context->camera->eye_cell[i];
        ray.eye_frac[i] = context->camera->eye_frac[i];
        ray.direction[i] = guarded[i];
    }
    ray.eye_cell[2] -= TRAVERSE_Z_BASE;
    ray.resume = 1u;
    ray.t_end = t_end;
    uint32_t last_material = SHADE_NO_MATERIAL;
    int16_t last_offset[3] = {0, 0, 0};
    /* Layer for layer as shade_kernel: on its last, which is fewer past SHADE_ALPHA_FAR_FROM (shade_alpha_layers_at), what is met is drawn
     * whole. Where it got out is the miss's t: TRAVERSE_COARSE_SKY, or where it stopped short (traverse_hit_past). */
    for (uint32_t layer = 0u;; layer += 1u) {
        const uint32_t face = (uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK;
        if (face == TRAVERSE_FACE_MISS) {
            *out_t = hit.t;
            return true;
        }
        if (layer + 1u >= shade_alpha_layers_at(hit.t)) {
            return false;
        }
        const uint32_t material = hit.value & TRAVERSE_VALUE_MATERIAL_MASK;
        const bool own = shade_own_kind_behind(last_material, last_offset, material, hit.offset, face);
        if (!lod_surface_goes_on(context, direction, &hit, own)) {
            return false;
        }
        last_material = material;
        float32_t next_t = hit.t;
        if (material == context->water_material) {
            ray.t_start = hit.t;
            /* The walk shade_water_kernel takes, which reads the tree again only on leaving a section or a brick: the same answers
             * as shade_kernel's, and a sea is most of a picture. */
            shade_cell_cursor_t cursor;
            cursor.section[0] = INT32_MAX;
            cursor.section[1] = 0;
            cursor.section[2] = 0;
            shade_water_walk_t walk;
            shade_water_through(context->view, &ray, &hit, material,
                                water_tint ? SHADE_WATER_SEEN_BLOCKS : TRAVERSE_COARSE_SKY, &cursor, &walk);
            if (walk.cut_short != 0u) {
                return false;
            }
            next_t = walk.last_in;
            for (uint32_t i = 0u; i < 3u; i += 1u) {
                hit.offset[i] = walk.last[i];
            }
        }
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            last_offset[i] = hit.offset[i];
        }
        ray.t_start = next_t;
        traverse_tally_t tally;
        traverse_trace_counted(
            context->view, &ray, nullptr, &hit,
            traverse_tally_for(&tally, counts, TRAVERSE_COUNT_PAST, pixel, TRAVERSE_COUNT_NO_LEVEL, 1.0F));
    }
}

/* What lod_past_kernel leaves in a pixel's lod_hit_t.material for lod_frame_kernel: a far ray to follow, or shade's own ray to
 * follow first. Anything else there is LOD_NO_MATERIAL, and stays so. */
enum : uint16_t {
    LOD_ASK_FAR = 0xFFFEu,
    LOD_ASK_SHADE = 0xFFFDu,
};

/* The first of a frame's two kernels, one tile a block as in primary rays: which pixels get a far ray. One that hit nothing in level 0, up
 * to its edge or the pixel's d_1, does. One whose first surface shade goes on past is asked by one trace, which goes through every block
 * but those in stops, whether anything opaque stands in its way before it gets past level 0 the same way: nearly always, and then shade
 * never looks past it, and nor does the far field. Water stops it too, and then lod_frame_kernel follows shade's own ray, which a long way of water ends: a second
 * trace on through the water to see what is behind it costs more than that walk, as one towards the horizon crosses hundreds of
 * voxels of it one at a time. A kernel of its own, small and with no far ray in it, so that it keeps many rays on the card at once,
 * and the far rays of a warp do not wait on these, nor these on them. With no stops, every such pixel asks lod_frame_kernel to
 * follow shade. */
static __device__ __forceinline__ void
lod_past_kernel_body(const traverse_view_t *fine, const traverse_camera_t *camera, const shade_texture_t *textures,
                     const uint16_t *faces, const uint8_t *pixels, uint32_t material_count, uint32_t water_material,
                     const uint32_t *stops, const traverse_hit_t *hits, lod_hit_t *out, traverse_counts_t *counts) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera->width || pixel_y >= camera->height) {
        return;
    }
    const uint64_t index = (uint64_t)pixel_y * camera->width + pixel_x;
    const traverse_hit_t hit = hits[index];
    uint16_t ask = LOD_ASK_FAR;
    if (((uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK) != TRAVERSE_FACE_MISS) {
        ask = (uint16_t)LOD_NO_MATERIAL;
        float32_t direction[3];
        traverse_camera_direction(camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
        const shade_context_t context = {
            fine,
            camera,
            textures,
            faces,
            pixels,
            nullptr,
            nullptr,
            shade_pixel_tangent(camera),
            material_count,
            0u,
            (uint16_t)water_material,
            (uint16_t)SHADE_NO_MATERIAL,
            nullptr,
        };
        if (lod_surface_goes_on(&context, direction, &hit, false)) {
            ask = LOD_ASK_SHADE;
            if (stops != nullptr) {
                traverse_trace_ray_t ray;
                for (uint32_t i = 0u; i < 3u; i += 1u) {
                    ray.eye_cell[i] = camera->eye_cell[i];
                    ray.eye_frac[i] = camera->eye_frac[i];
                    ray.direction[i] = traverse_guard(direction[i]);
                }
                ray.eye_cell[2] -= TRAVERSE_Z_BASE;
                ray.t_start = hit.t;
                ray.resume = 1u;
                ray.t_end = traverse_band_from(fine, traverse_band_factor(fine, pixel_x, pixel_y), 1u);
                traverse_hit_t blocked;
                traverse_tally_t tally;
                traverse_trace_counted(fine, &ray, stops, &blocked,
                                       traverse_tally_for(&tally, counts, TRAVERSE_COUNT_PAST, (uint32_t)index,
                                                          TRAVERSE_COUNT_NO_LEVEL, 1.0F));
                if (((uint32_t)blocked.info & TRAVERSE_INFO_FACE_MASK) == TRAVERSE_FACE_MISS) {
                    ask = LOD_ASK_FAR;
                } else if ((blocked.value & TRAVERSE_VALUE_MATERIAL_MASK) != water_material) {
                    ask = (uint16_t)LOD_NO_MATERIAL;
                }
            }
        }
    }
    out[index] = lod_hit_t{TRAVERSE_COARSE_SKY, ask, (uint8_t)TRAVERSE_FACE_MISS, (uint8_t)LOD_SUN_SEEN};
}

__global__ void lod_past_kernel(const traverse_view_t fine, const traverse_camera_t camera,
                                const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                                uint32_t material_count, uint32_t water_material, const uint32_t *stops,
                                const traverse_hit_t *hits, lod_hit_t *out) {
    lod_past_kernel_body(&fine, &camera, textures, faces, pixels, material_count, water_material, stops, hits, out,
                         nullptr);
}

__global__ void lod_past_kernel_counted(const traverse_view_t fine, const traverse_camera_t camera,
                                        const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                                        uint32_t material_count, uint32_t water_material, const uint32_t *stops,
                                        const traverse_hit_t *hits, lod_hit_t *out, traverse_counts_t *counts) {
    lod_past_kernel_body(&fine, &camera, textures, faces, pixels, material_count, water_material, stops, hits, out,
                         counts);
}

/* A warp's far rays into the tally: read from a texel, and traced. One lane adds for all. */
static __device__ __forceinline__ void lod_turn_count(lod_turn_tally_t *tally, bool read) {
    const uint32_t active = __activemask();
    const uint32_t reads = __ballot_sync(active, read);
    const uint32_t lane = (threadIdx.y * blockDim.x + threadIdx.x) & 31u;
    if (lane == (uint32_t)(__ffs((int32_t)active) - 1)) {
        const uint32_t read_count = (uint32_t)__popc(reads);
        traverse_count_add(&tally->read, read_count);
        traverse_count_add(&tally->traced, (uint32_t)__popc(active) - read_count);
    }
}

/* Whether this pixel is the one a texel's middle falls in on this frame's camera: one pixel or none for each texel, worked out alike by every
 * pixel, so at most one pixel of a frame writes a texel. The camera's right and up are taken as square to forward, as the frame makes them; were
 * they not, it would be another pixel, still only one. */
static __device__ __forceinline__ bool lod_turn_owns(const traverse_camera_t *camera, uint32_t texel, uint32_t side,
                                                     uint32_t pixel_x, uint32_t pixel_y) {
    float32_t middle[3];
    lod_turn_texel_direction(texel, side, middle);
    const float32_t *const f = camera->forward, *const r = camera->right, *const u = camera->up;
    const float32_t along = (middle[0] * f[0] + middle[1] * f[1] + middle[2] * f[2]) / (f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (!(along > 0.0F)) {
        return false;
    }
    const float32_t across = (middle[0] * r[0] + middle[1] * r[1] + middle[2] * r[2]) / (r[0] * r[0] + r[1] * r[1] + r[2] * r[2]) / along;
    const float32_t down = (middle[0] * u[0] + middle[1] * u[1] + middle[2] * u[2]) / (u[0] * u[0] + u[1] * u[1] + u[2] * u[2]) / along;
    const float32_t x = (across + 1.0F) * 0.5F * (float32_t)camera->width;
    const float32_t y = (1.0F - down) * 0.5F * (float32_t)camera->height;
    return x >= 0.0F && y >= 0.0F && (uint32_t)x == pixel_x && (uint32_t)y == pixel_y;
}

/* The far ray of one pixel, from t_from: with the turn cache, its direction's texel if this is a turn and the texel stands for it, or traced.
 * The same picture whatever order the pixels run in (plan 009 phase 08's follow-up): a texel is written once in a generation, by the one pixel
 * its middle falls in (lod_turn_owns), and is read only on a later frame, by the stamp of the frame that wrote it in the top of its hit's sun
 * byte. Before, any pixel of the texel wrote it, the last one in won, and a pixel could read what another wrote that same frame, so a settled
 * row that turns (bench_world's orbits) drew a different picture each run. */
static __device__ __forceinline__ void lod_far_ray(const lod_view_t *view, const lod_reach_t *bands,
                                                   const lod_origin_t *origin, const float32_t guarded[3],
                                                   float32_t t_from, bool shadows, const float32_t sun[3],
                                                   const lod_turn_args_t *turn, lod_hit_t *out,
                                                   traverse_counts_t *counts, uint32_t pixel,
                                                   const traverse_camera_t *camera, uint32_t pixel_x, uint32_t pixel_y) {
    if (turn->texels == nullptr) {
        lod_hit_write(view, bands, origin, guarded, t_from, true, shadows, sun, out, counts, pixel);
        return;
    }
    const uint32_t index = lod_turn_texel_of(guarded, turn->side);
    uint4 *const slot = (uint4 *)(void *)&turn->texels[index];
    const uint32_t stamp = (turn->reading >> 8) & LOD_TURN_STAMP_MASK;
    lod_turn_texel_t texel;
    const uint4 raw = *slot;
    memcpy(&texel, &raw, sizeof(texel));
    const bool current = texel.generation == turn->generation;
    const bool read = (turn->reading & 1u) != 0u && current && ((uint32_t)texel.hit.sun & LOD_TURN_STAMP_MASK) != stamp &&
                      fabsf(texel.from - t_from) <= turn->spread * t_from;
    lod_turn_count(turn->tally, read);
    if (read) {
        texel.hit.sun = (texel.hit.sun & LOD_TURN_SUN_BIT) != 0u ? (uint8_t)LOD_SUN_SEEN : (uint8_t)LOD_SUN_HIDDEN;
        *out = texel.hit;
        return;
    }
    lod_hit_write(view, bands, origin, guarded, t_from, true, shadows, sun, out, counts, pixel);
    if (!current && lod_turn_owns(camera, index, turn->side, pixel_x, pixel_y)) {
        texel.hit = *out;
        texel.hit.sun = (uint8_t)((out->sun != LOD_SUN_HIDDEN ? LOD_TURN_SUN_BIT : 0u) | stamp);
        texel.from = t_from;
        texel.generation = turn->generation;
        uint4 written;
        memcpy(&written, &texel, sizeof(written));
        *slot = written;
    }
}

/* The second: the far rays lod_past_kernel asked for, and for one past a see-through surface only if shade's own ray, followed as shade
 * follows it, gets past level 0. Each starts where level 0 stopped for it (plan 009 phase 04): the pixel's d_1, or where the ray leaves level
 * 0's square if that is nearer, as a ray that met nothing there may have left by the top. So nothing of level 0's is passed by, as nothing
 * nearer is looked at. Every other pixel keeps the nothing it was given. */
static __device__ __forceinline__ void
lod_frame_kernel_body(const lod_view_t *view, const traverse_view_t *fine, const traverse_camera_t *camera,
                      const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                      uint32_t material_count, uint32_t water_material, bool water_tint, bool shadows, float32_t sun_x,
                      float32_t sun_y, float32_t sun_z, const traverse_hit_t *hits, lod_hit_t *out,
                      const lod_turn_args_t *turn, traverse_counts_t *counts) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera->width || pixel_y >= camera->height) {
        return;
    }
    const uint64_t index = (uint64_t)pixel_y * camera->width + pixel_x;
    const uint16_t ask = out[index].material;
    if (ask != LOD_ASK_FAR && ask != LOD_ASK_SHADE) {
        return;
    }
    float32_t direction[3], guarded[3];
    traverse_camera_direction(camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        guarded[i] = traverse_guard(direction[i]);
    }
    const float32_t factor = traverse_band_factor(fine, pixel_x, pixel_y);
    const float32_t t_end = traverse_band_from(fine, factor, 1u);
    /* Where level 0 let the ray go: a primary miss says, short of its d_1 at a column level 0 does not hold. */
    const traverse_hit_t first = hits[index];
    float32_t t_out =
        ((uint32_t)first.info & TRAVERSE_INFO_FACE_MASK) == TRAVERSE_FACE_MISS ? first.t : TRAVERSE_COARSE_SKY;
    if (ask == LOD_ASK_SHADE) {
        const shade_context_t context = {
            fine,
            camera,
            textures,
            faces,
            pixels,
            nullptr,
            nullptr,
            shade_pixel_tangent(camera),
            material_count,
            0u,
            (uint16_t)water_material,
            (uint16_t)SHADE_NO_MATERIAL,
            nullptr,
        };
        if (!lod_goes_out(&context, direction, guarded, t_end, first, water_tint, counts, (uint32_t)index, &t_out)) {
            out[index].material = (uint16_t)LOD_NO_MATERIAL;
            return;
        }
    }
    lod_origin_t origin;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        origin.cell[i] = camera->eye_cell[i];
        origin.frac[i] = camera->eye_frac[i];
    }
    origin.cell[2] -= TRAVERSE_Z_BASE;
    const float32_t sun[3] = {sun_x, sun_y, sun_z};
    const float32_t t_from = fminf(fminf(t_out, t_end), lod_square_exit(fine, 0u, &origin, guarded));
    const lod_reach_t bands = {fine->band_base, factor, (float32_t)MC_TUNE_RENDER_LOD_NEAR_GUARANTEE, {0}};
    lod_far_ray(view, &bands, &origin, guarded, t_from, shadows, sun, turn, &out[index], counts, (uint32_t)index, camera,
                pixel_x, pixel_y);
}

__global__ void lod_frame_kernel(const lod_view_t view, const traverse_view_t fine, const traverse_camera_t camera,
                                 const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                                 uint32_t material_count, uint32_t water_material, bool water_tint, bool shadows,
                                 float32_t sun_x, float32_t sun_y, float32_t sun_z, const traverse_hit_t *hits,
                                 lod_hit_t *out, const lod_turn_args_t turn) {
    lod_frame_kernel_body(&view, &fine, &camera, textures, faces, pixels, material_count, water_material, water_tint,
                          shadows, sun_x, sun_y, sun_z, hits, out, &turn, nullptr);
}

__global__ void lod_frame_kernel_counted(const lod_view_t view, const traverse_view_t fine,
                                         const traverse_camera_t camera, const shade_texture_t *textures,
                                         const uint16_t *faces, const uint8_t *pixels, uint32_t material_count,
                                         uint32_t water_material, bool water_tint, bool shadows, float32_t sun_x,
                                         float32_t sun_y, float32_t sun_z, const traverse_hit_t *hits, lod_hit_t *out,
                                         const lod_turn_args_t turn, traverse_counts_t *counts) {
    lod_frame_kernel_body(&view, &fine, &camera, textures, faces, pixels, material_count, water_material, water_tint,
                          shadows, sun_x, sun_y, sun_z, hits, out, &turn, counts);
}

__global__ void lod_rays_kernel(const lod_view_t view, bool shadows, float32_t sun_x, float32_t sun_y, float32_t sun_z,
                                const traverse_ray_t *rays, lod_hit_t *out, uint32_t ray_count) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= ray_count) {
        return;
    }
    const traverse_ray_t given = rays[index];
    const float32_t scale =
        1.0F / sqrtf(given.direction[0] * given.direction[0] + given.direction[1] * given.direction[1] +
                     given.direction[2] * given.direction[2]);
    float32_t direction[3];
    lod_origin_t origin;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        origin.cell[i] = given.eye_cell[i];
        origin.frac[i] = given.eye_frac[i];
        direction[i] = traverse_guard(given.direction[i] * scale);
    }
    origin.cell[2] -= TRAVERSE_Z_BASE;
    const float32_t sun[3] = {sun_x, sun_y, sun_z};
    const lod_reach_t no_bands = {0.0F, 1.0F, 0.0F, {0}};
    lod_hit_write(&view, &no_bands, &origin, direction, given.t_start, false, shadows, sun, &out[index], nullptr,
                  TRAVERSE_COUNT_NO_PIXEL);
}

/* Plan 008 phase 06: the turn cache's state for this frame, and what its kernel needs. A new generation when there was none, the eye has moved
 * LOD_TURN_EYE_BLOCKS from where this one began, the far field's revision is another, or the sun has moved or its rays come or go. A turn
 * when the camera's middle moved by more than LOD_TURN_PIXELS since the last frame. The first call zeroes the block, on the stream. */
static gpu_status_t lod_turn_begin(gpu_error_t *error, lod_turn_cache_t *cache, uint32_t revision,
                                   const traverse_camera_t *camera, const lod_light_t *light, lod_turn_args_t *out) {
    const uint64_t bytes = lod_turn_cache_bytes(cache->side);
    if (cache->side == 0u || cache->borrowed_block == nullptr || cache->borrowed_block->bytes < bytes) {
        return traverse_refuse(error, "lod: the turn cache's block is missing or too small for its side");
    }
    uint8_t *const base = device_memory_pointer(cache->borrowed_block);
    if (cache->cleared == 0u) {
        const gpu_status_t cleared =
            device_memory_check(error, cudaMemsetAsync(base, 0, bytes), "lod: emptying the turn cache");
        if (cleared != GPU_STATUS_OK) {
            return cleared;
        }
        cache->cleared = 1u;
        cache->generation = 0u;
    }
    float64_t moved = 0.0;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float64_t gap = (float64_t)(camera->eye_cell[i] - cache->eye_cell[i]) +
                              ((float64_t)camera->eye_frac[i] - (float64_t)cache->eye_frac[i]);
        moved += gap * gap;
    }
    const float32_t *const sun = light->sun_direction;
    const float32_t sun_dot = sun[0] * cache->sun[0] + sun[1] * cache->sun[1] + sun[2] * cache->sun[2];
    const uint8_t shadows = light->shadows != 0u ? 1u : 0u;
    if (cache->generation == 0u || moved > (float64_t)LOD_TURN_EYE_BLOCKS * (float64_t)LOD_TURN_EYE_BLOCKS ||
        revision != cache->revision || shadows != cache->shadows || (shadows != 0u && sun_dot < LOD_TURN_SUN_COSINE)) {
        cache->generation = cache->generation == UINT32_MAX ? 1u : cache->generation + 1u;
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            cache->eye_cell[i] = camera->eye_cell[i];
            cache->eye_frac[i] = camera->eye_frac[i];
            cache->sun[i] = sun[i];
        }
        cache->revision = revision;
        cache->shadows = shadows;
    }
    /* The angle between the two middles against one pixel's, 2 |up| / (|forward| height), both small enough to take as their sines. */
    const float32_t *const f = camera->forward;
    const float32_t *const g = cache->forward;
    const float32_t cross[3] = {f[1] * g[2] - f[2] * g[1], f[2] * g[0] - f[0] * g[2], f[0] * g[1] - f[1] * g[0]};
    const float32_t f_length = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    const float32_t g_length = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    const float32_t up =
        sqrtf(camera->up[0] * camera->up[0] + camera->up[1] * camera->up[1] + camera->up[2] * camera->up[2]);
    const float32_t sine =
        sqrtf(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]) / fmaxf(f_length * g_length, 1e-20F);
    const float32_t dot = f[0] * g[0] + f[1] * g[1] + f[2] * g[2];
    const float32_t pixel = 2.0F * up / (fmaxf(f_length, 1e-20F) * (float32_t)camera->height);
    cache->turning = g_length > 0.0F && (dot < 0.0F || sine > LOD_TURN_PIXELS * pixel) ? 1u : 0u;
    cache->stamp = (uint8_t)((cache->stamp + 1u) & LOD_TURN_STAMP_MASK);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        cache->forward[i] = f[i];
    }
    const uint64_t texels = (uint64_t)LOD_TURN_FACES * cache->side * cache->side;
    *out = lod_turn_args_t{(lod_turn_texel_t *)(void *)base,
                           (lod_turn_tally_t *)(void *)(base + texels * sizeof(lod_turn_texel_t)),
                           cache->side,
                           cache->generation,
                           0.0F,
                           (uint32_t)cache->turning | ((uint32_t)cache->stamp << 8)};
    return GPU_STATUS_OK;
}

gpu_status_t lod_frame(gpu_error_t *error, const lod_scene_t *scene, const lod_light_t *light,
                       const traverse_world_t *world, const traverse_camera_t *camera, const device_block_t *hits) {
    return lod_frame_counted(error, scene, light, world, camera, hits, nullptr);
}

gpu_status_t lod_frame_counted(gpu_error_t *error, const lod_scene_t *scene, const lod_light_t *light,
                               const traverse_world_t *world, const traverse_camera_t *camera,
                               const device_block_t *hits, const device_block_t *counts_block) {
    static_assert((uint32_t)LOD_LEVELS_MAX <= (uint32_t)TRAVERSE_COUNT_LEVELS);
    if (counts_block != nullptr && counts_block->bytes < sizeof(traverse_counts_t)) {
        return traverse_refuse(error, "lod: the counts block is too small");
    }
    if (camera->width == 0u || camera->height == 0u) {
        return traverse_refuse(error, "lod: the camera has no pixels");
    }
    if (hits->bytes < traverse_hits_bytes(camera->width, camera->height)) {
        return traverse_refuse(error, "lod: the hits are too small for the camera");
    }
    if (scene->borrowed_hits == nullptr ||
        scene->borrowed_hits->bytes < lod_hits_bytes(camera->width, camera->height)) {
        return traverse_refuse(error, "lod: its hits are missing or too small for the camera");
    }
    lod_view_t view;
    traverse_view_t fine;
    gpu_status_t status = lod_view_from(error, scene, &view);
    if (status == GPU_STATUS_OK) {
        status = traverse_view_from(error, world, &fine);
    }
    if (status != GPU_STATUS_OK) {
        return status;
    }
    const uint64_t stops_at = lod_stops_offset((uint64_t)light->material_count * SHADE_FACES * sizeof(uint16_t));
    if (light->stops_after_faces != 0u && light->borrowed_faces->bytes < stops_at + sizeof(traverse_stops_t)) {
        return traverse_refuse(error, "lod: the face table has no room for the stops it is said to hold");
    }
    const uint8_t *const faces_on_card = device_memory_pointer(light->borrowed_faces);
    const dim3 grid(traverse_tiles_across(camera->width), traverse_tiles_across(camera->height));
    const dim3 tile(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS);
    const shade_texture_t *const textures =
        (const shade_texture_t *)(const void *)device_memory_pointer(light->borrowed_textures);
    const uint16_t *const faces = (const uint16_t *)(const void *)faces_on_card;
    const uint8_t *const pixels =
        light->borrowed_pixels != nullptr ? device_memory_pointer(light->borrowed_pixels) : nullptr;
    const traverse_hit_t *const primary = (const traverse_hit_t *)(const void *)device_memory_pointer(hits);
    lod_hit_t *const out = (lod_hit_t *)(void *)device_memory_pointer(scene->borrowed_hits);
    const uint32_t *const stops =
        light->stops_after_faces != 0u ? (const uint32_t *)(const void *)(faces_on_card + stops_at) : nullptr;
    /* Plan 009 phase 01: with counts, the counted copies; the same work and the same hits. */
    traverse_counts_t *const counts =
        counts_block != nullptr ? (traverse_counts_t *)(void *)device_memory_pointer(counts_block) : nullptr;
    /* Plan 008 phase 06: checked, and its generation worked out, before anything starts. */
    lod_turn_args_t turn = {nullptr, nullptr, 0u, 0u, 0.0F, 0u};
    if (scene->borrowed_turn != nullptr) {
        status = lod_turn_begin(error, scene->borrowed_turn, scene->revision, camera, light, &turn);
        if (status != GPU_STATUS_OK) {
            return status;
        }
        turn.spread = 2.0F * fine.band_dither + 0.02F;
    }
    if (counts != nullptr) {
        lod_past_kernel_counted<<<grid, tile>>>(fine, *camera, textures, faces, pixels, light->material_count,
                                                light->water_material, stops, primary, out, counts);
    } else {
        lod_past_kernel<<<grid, tile>>>(fine, *camera, textures, faces, pixels, light->material_count,
                                        light->water_material, stops, primary, out);
    }
    status = device_memory_check(error, cudaGetLastError(), "lod: starting which pixels of a frame get a far ray");
    if (status != GPU_STATUS_OK) {
        return status;
    }
    const bool water_tint = light->water_tint != 0u, shadows = light->shadows != 0u;
    const float32_t *const sun = light->sun_direction;
    if (counts != nullptr) {
        lod_frame_kernel_counted<<<grid, tile>>>(view, fine, *camera, textures, faces, pixels, light->material_count,
                                                 light->water_material, water_tint, shadows, sun[0], sun[1], sun[2],
                                                 primary, out, turn, counts);
    } else {
        lod_frame_kernel<<<grid, tile>>>(view, fine, *camera, textures, faces, pixels, light->material_count,
                                         light->water_material, water_tint, shadows, sun[0], sun[1], sun[2], primary,
                                         out, turn);
    }
    return device_memory_check(error, cudaGetLastError(), "lod: starting the rays of a frame");
}

gpu_status_t lod_rays(gpu_error_t *error, const lod_scene_t *scene, const lod_light_t *light,
                      const device_block_t *rays, const device_block_t *out, uint32_t ray_count) {
    if (ray_count == 0u) {
        return GPU_STATUS_OK;
    }
    if (rays->bytes < (uint64_t)ray_count * sizeof(traverse_ray_t) ||
        out->bytes < (uint64_t)ray_count * sizeof(lod_hit_t)) {
        return traverse_refuse(error, "lod: a ray or hit block is too small for the ray count");
    }
    lod_view_t view;
    const gpu_status_t status = lod_view_from(error, scene, &view);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    const bool shadows = light != nullptr && light->shadows != 0u;
    const uint32_t per_block = 64u;
    lod_rays_kernel<<<(ray_count + per_block - 1u) / per_block, per_block>>>(
        view, shadows, shadows ? light->sun_direction[0] : 0.0F, shadows ? light->sun_direction[1] : 0.0F,
        shadows ? light->sun_direction[2] : 0.0F, (const traverse_ray_t *)(const void *)device_memory_pointer(rays),
        (lod_hit_t *)(void *)device_memory_pointer(out), ray_count);
    return device_memory_check(error, cudaGetLastError(), "lod: starting rays");
}
