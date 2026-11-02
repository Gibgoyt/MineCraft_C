/* See README.md. Encodes one section into the bitmask tree, in time proportional to its blocks and not to its 4096 voxels:
 * most sections with anything in them are mostly air, and this runs for every section that changes. */
#include "render/encode/index.h"

#include "simd/bitmask/index.h"

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

void render_encode_job_begin(render_encode_job_t *job, const world_edit_snapshot_view_t *view, uint32_t section,
                             uint8_t *out) {
    job->borrowed_view = view;
    job->borrowed_out = out;
    job->section = section;
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
