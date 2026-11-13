/* See README.md. Encodes one section into the bitmask tree, in time proportional to its blocks and not to its 4096 voxels:
 * most sections with anything in them are mostly air, and this runs for every section that changes. */
#include "render/encode/index.h"

#include "simd/bitmask/index.h"
#include "tables/blocks/index.h"

#include <stdbit.h>
#include <string.h>

enum : uint32_t {
    /* Words in a mask of one section's voxels, and of one brick's. */
    ENCODE_WORDS = SECTION_VOXELS / 64u,
};
static_assert((uint32_t)ENCODE_WORDS == (uint32_t)WORLD_BUFFERS_BRICKS);

/* Which voxel in section_voxel_index order a brick's bit stands for. */
static inline uint32_t encode_voxel_index(uint32_t brick, uint32_t voxel) {
    const uint32_t x = ((brick & 3u) << 2) | (voxel & 3u);
    const uint32_t y = (((brick >> 2) & 3u) << 2) | ((voxel >> 2) & 3u);
    const uint32_t z = ((brick >> 4) << 2) | (voxel >> 4);
    return section_voxel_index(x, y, z);
}

/* One bit per voxel in section_voxel_index order, re-sorted by brick. */
static void encode_bricks_from_occupied(const uint64_t *occupied, uint64_t *bricks) {
    memset(bricks, 0, ENCODE_WORDS * sizeof(bricks[0]));
    for (uint32_t word = 0; word < ENCODE_WORDS; word++) {
        for (uint64_t rest = occupied[word]; rest != 0u; rest &= rest - 1u) {
            const uint32_t index = (word << 6) | stdc_trailing_zeros(rest);
            const uint32_t x = index & 15u;
            const uint32_t y = (index >> 4) & 15u;
            const uint32_t z = index >> 8;
            bricks[world_buffers_brick_of(x, y, z)] |= 1ull << world_buffers_brick_voxel_of(x, y, z);
        }
    }
}

/* Whether every voxel is the one block with the one light: stone far below the ground, mostly. Worth a look, as this is 4096
 * comparisons at the most, against 12944 bytes to send and keep. Only asked of a section with no air in it. */
static bool encode_is_uniform(const render_encode_source_t *source) {
    const uint16_t *const blocks = source->borrowed_blocks;
    const uint8_t *const light = source->borrowed_light;
    if (blocks != nullptr) {
        for (uint32_t i = 1; i < SECTION_VOXELS; i++) {
            if (blocks[i] != blocks[0]) {
                return false;
            }
        }
    }
    if (light != nullptr) {
        for (uint32_t i = 1; i < SECTION_VOXELS; i++) {
            if (light[i] != light[0]) {
                return false;
            }
        }
    }
    return true;
}

static uint32_t encode_uniform(const render_encode_source_t *source, uint8_t *out) {
    const uint16_t material = source->borrowed_blocks != nullptr ? source->borrowed_blocks[0] : source->block_uniform;
    const uint8_t light = source->borrowed_light != nullptr ? source->borrowed_light[0] : source->light_uniform;
    const uint32_t light_offset = (uint32_t)sizeof(world_buffers_section_t) + (uint32_t)sizeof(uint16_t);
    const world_buffers_section_t header = {
        .brick_mask = ~0ull,
        .uniform_mask = ~0ull,
        .voxel_count = (uint16_t)SECTION_VOXELS,
        .material_offset = (uint16_t)sizeof(world_buffers_section_t),
        .light_offset = (uint16_t)light_offset,
        .brick_count = 0u,
        ._pad0 = {0},
    };
    memset(out, 0, WORLD_BUFFERS_SECTION_BYTES_UNIFORM);
    memcpy(out, &header, sizeof(header));
    memcpy(out + sizeof(header), &material, sizeof(material));
    out[light_offset] = light;
    return WORLD_BUFFERS_SECTION_BYTES_UNIFORM;
}

/* Plan 009 phase 05: a bit for each full brick whose voxels are all one material, light aside: see world_buffers_section_t. */
static uint64_t encode_uniform_mask(const render_encode_source_t *source, const uint64_t *bricks) {
    uint64_t mask = 0u;
    for (uint32_t brick = 0; brick < ENCODE_WORDS; brick++) {
        if (bricks[brick] != ~0ull) {
            continue;
        }
        bool same = true;
        if (source->borrowed_blocks != nullptr) {
            const uint16_t first = source->borrowed_blocks[encode_voxel_index(brick, 0u)];
            for (uint32_t voxel = 1; voxel < 64u && same; voxel++) {
                same = source->borrowed_blocks[encode_voxel_index(brick, voxel)] == first;
            }
        }
        mask |= same ? 1ull << brick : 0u;
    }
    return mask;
}

uint32_t render_encode_section(const render_encode_source_t *source, uint8_t *out) {
    uint64_t occupied[ENCODE_WORDS];
    uint64_t bricks[ENCODE_WORDS];
    if (source->borrowed_blocks != nullptr) {
        bitmask_from_nonzero_u16_batch(source->borrowed_blocks, SECTION_VOXELS, occupied);
    } else if (source->block_uniform != 0u) {
        memset(occupied, 0xFF, sizeof(occupied));
    } else {
        return 0u;
    }
    encode_bricks_from_occupied(occupied, bricks);

    uint64_t brick_mask = 0u;
    uint32_t brick_count = 0u;
    uint32_t voxel_count = 0u;
    for (uint32_t brick = 0; brick < ENCODE_WORDS; brick++) {
        if (bricks[brick] != 0u) {
            brick_mask |= 1ull << brick;
            brick_count++;
            voxel_count += stdc_count_ones(bricks[brick]);
        }
    }
    if (voxel_count == 0u) {
        return 0u;
    }
    if (voxel_count == SECTION_VOXELS && encode_is_uniform(source)) {
        return encode_uniform(source, out);
    }

    const uint32_t words_offset = (uint32_t)sizeof(world_buffers_section_t);
    const uint32_t first_offset = words_offset + brick_count * (uint32_t)sizeof(uint64_t);
    const uint32_t material_offset = first_offset + brick_count * (uint32_t)sizeof(uint16_t);
    const uint32_t light_offset = material_offset + voxel_count * (uint32_t)sizeof(uint16_t);
    const uint32_t end = light_offset + voxel_count;
    const uint32_t total = (end + 7u) & ~7u;

    uint64_t *const out_words = (uint64_t *)(void *)(out + words_offset);
    uint16_t *const out_first = (uint16_t *)(void *)(out + first_offset);
    uint16_t *const out_material = (uint16_t *)(void *)(out + material_offset);
    uint8_t *const out_light = out + light_offset;
    const world_buffers_section_t header = {
        .brick_mask = brick_mask,
        .uniform_mask = encode_uniform_mask(source, bricks),
        .voxel_count = (uint16_t)voxel_count,
        .material_offset = (uint16_t)material_offset,
        .light_offset = (uint16_t)light_offset,
        .brick_count = (uint8_t)brick_count,
        ._pad0 = {0},
    };
    memcpy(out, &header, sizeof(header));

    uint32_t rank = 0u;
    uint32_t place = 0u;
    for (uint32_t brick = 0; brick < ENCODE_WORDS; brick++) {
        const uint64_t word = bricks[brick];
        if (word == 0u) {
            continue;
        }
        out_words[rank] = word;
        out_first[rank] = (uint16_t)place;
        rank++;
        for (uint64_t rest = word; rest != 0u; rest &= rest - 1u) {
            const uint32_t index = encode_voxel_index(brick, stdc_trailing_zeros(rest));
            out_material[place] =
                source->borrowed_blocks != nullptr ? source->borrowed_blocks[index] : source->block_uniform;
            out_light[place] =
                source->borrowed_light != nullptr ? source->borrowed_light[index] : source->light_uniform;
            place++;
        }
    }
    /* Padding goes to the card as zeros, not as whatever this buffer last held: plans/CODING_STANDARD.md section 3. */
    memset(out + end, 0, total - end);
    return total;
}

/* ---- The octant: a section reduced by two each way (plan 009 phase 03) ---------------------------------------------- */

bool render_encode_block_counts(uint16_t block) {
    if (block == BLOCK_ID_AIR) {
        return false;
    }
    /* A voxel holds its block's state above the id: which way stairs face is not what the table is indexed by. */
    const uint32_t id = (uint32_t)block & BLOCK_STATE_ID_MASK;
    if (id >= BLOCK_COUNT) {
        return true; /* no such block: drawn as a cube in the colour that shows the bug, so it counts as one */
    }
    switch ((block_shape_t)block_defs[id].shape) {
    case BLOCK_SHAPE_CUBE:
    case BLOCK_SHAPE_FLUID:
    case BLOCK_SHAPE_SLAB:
    case BLOCK_SHAPE_STAIRS:
        return true;
    case BLOCK_SHAPE_NONE:
    case BLOCK_SHAPE_CROSS:
    case BLOCK_SHAPE_CROP:
    case BLOCK_SHAPE_TORCH:
    case BLOCK_SHAPE_THIN:
    case BLOCK_SHAPE_TORCH_WALL:
    case BLOCK_SHAPE_DOOR:
        break;
    }
    return false;
}

/* The most common of a layer's four counted blocks, the first met winning a tie. 0 if none counts. */
static uint16_t encode_layer_block(const uint16_t *layer, const bool *counted) {
    uint16_t best = 0u;
    uint32_t best_count = 0u;
    for (uint32_t i = 0u; i < 4u; i++) {
        if (!counted[i]) {
            continue;
        }
        uint32_t count = 0u;
        for (uint32_t j = 0u; j < 4u; j++) {
            count += counted[j] && layer[j] == layer[i] ? 1u : 0u;
        }
        if (count > best_count) {
            best = layer[i];
            best_count = count;
        }
    }
    return best;
}

void render_encode_octant_voxel(const uint16_t blocks[RENDER_ENCODE_GROUP_VOXELS],
                                const uint8_t light[RENDER_ENCODE_GROUP_VOXELS], uint16_t *out_material,
                                uint8_t *out_light) {
    bool counted[RENDER_ENCODE_GROUP_VOXELS];
    uint32_t count = 0u;
    uint32_t sky = 0u, lamp = 0u;
    for (uint32_t i = 0u; i < RENDER_ENCODE_GROUP_VOXELS; i++) {
        counted[i] = render_encode_block_counts(blocks[i]);
        if (counted[i]) {
            count++;
            const uint32_t own_sky = (uint32_t)light[i] >> 4, own_lamp = (uint32_t)light[i] & 15u;
            sky = own_sky > sky ? own_sky : sky;
            lamp = own_lamp > lamp ? own_lamp : lamp;
        }
    }
    if (count * 2u < RENDER_ENCODE_GROUP_VOXELS) {
        *out_material = 0u;
        *out_light = 0u;
        return;
    }
    /* The top layer's face is what is seen from above: grass over dirt stays grass. */
    const uint16_t top = encode_layer_block(blocks + 4, counted + 4);
    *out_material = top != 0u ? top : encode_layer_block(blocks, counted);
    *out_light = (uint8_t)((sky << 4) | lamp);
}

bool render_encode_section_octant(const render_encode_source_t *source, render_encode_octant_t *out) {
    const uint16_t *const blocks = source->borrowed_blocks;
    const uint8_t *const light = source->borrowed_light;
    if (blocks == nullptr && light == nullptr) {
        /* One block with one light all through: one group says it all. */
        uint16_t group_blocks[RENDER_ENCODE_GROUP_VOXELS];
        uint8_t group_light[RENDER_ENCODE_GROUP_VOXELS];
        for (uint32_t i = 0u; i < RENDER_ENCODE_GROUP_VOXELS; i++) {
            group_blocks[i] = source->block_uniform;
            group_light[i] = source->light_uniform;
        }
        uint16_t material = 0u;
        uint8_t packed = 0u;
        render_encode_octant_voxel(group_blocks, group_light, &material, &packed);
        for (uint32_t i = 0u; i < RENDER_ENCODE_OCTANT_VOXELS; i++) {
            out->material[i] = material;
        }
        memset(out->light, packed, sizeof(out->light));
        return true;
    }
    bool uniform = true;
    for (uint32_t z = 0u; z < RENDER_ENCODE_OCTANT_SIDE; z++) {
        for (uint32_t y = 0u; y < RENDER_ENCODE_OCTANT_SIDE; y++) {
            for (uint32_t x = 0u; x < RENDER_ENCODE_OCTANT_SIDE; x++) {
                uint16_t group_blocks[RENDER_ENCODE_GROUP_VOXELS];
                uint8_t group_light[RENDER_ENCODE_GROUP_VOXELS];
                for (uint32_t i = 0u; i < RENDER_ENCODE_GROUP_VOXELS; i++) {
                    const uint32_t index =
                        section_voxel_index(2u * x + (i & 1u), 2u * y + ((i >> 1) & 1u), 2u * z + (i >> 2));
                    group_blocks[i] = blocks != nullptr ? blocks[index] : source->block_uniform;
                    group_light[i] = light != nullptr ? light[index] : source->light_uniform;
                }
                const uint32_t place = x + RENDER_ENCODE_OCTANT_SIDE * (y + RENDER_ENCODE_OCTANT_SIDE * z);
                render_encode_octant_voxel(group_blocks, group_light, &out->material[place], &out->light[place]);
                uniform = uniform && out->material[place] == out->material[0] && out->light[place] == out->light[0];
            }
        }
    }
    return uniform;
}

void render_encode_job_octant_set(render_encode_job_t *job, render_encode_octant_t *octant) {
    job->borrowed_octant = octant;
}

void render_encode_job_begin(render_encode_job_t *job, const world_edit_snapshot_view_t *view, uint32_t section,
                             uint8_t *out) {
    job->borrowed_view = view;
    job->borrowed_out = out;
    job->section = section;
    job->borrowed_octant = nullptr;
    atomic_store_explicit(&job->done_bytes, 0u, memory_order_relaxed);
}

void render_encode_job_run(job_worker_t *worker, void *borrowed_job) {
    (void)worker;
    render_encode_job_t *const job = borrowed_job;
    const world_edit_snapshot_view_t *const view = job->borrowed_view;
    const world_edit_snapshot_section_t *const entry = &view->borrowed_sections[job->section];
    const world_edit_snapshot_voxels_t *const voxels = &view->borrowed_voxels[job->section];
    const render_encode_source_t source = {
        .borrowed_blocks = entry->has_blocks != 0u ? voxels->blocks : nullptr,
        .borrowed_light = entry->has_light != 0u ? voxels->light : nullptr,
        .block_uniform = entry->block_uniform,
        .light_uniform = entry->light_uniform,
        ._pad0 = {0},
    };
    const uint32_t bytes = render_encode_section(&source, job->borrowed_out);
    if (job->borrowed_octant != nullptr) {
        (void)render_encode_section_octant(&source, job->borrowed_octant);
    }
    /* Release gives the gpu thread the bytes just written, and pairs with the acquire in render_encode_job_done. */
    atomic_store_explicit(&job->done_bytes, bytes + 1u, memory_order_release);
}

bool render_encode_job_done(const render_encode_job_t *job, uint32_t *out_bytes) {
    const uint32_t done = atomic_load_explicit(&job->done_bytes, memory_order_acquire);
    if (done == 0u) {
        return false;
    }
    *out_bytes = done - 1u;
    return true;
}
