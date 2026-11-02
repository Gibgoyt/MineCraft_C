/* See README.md. */
#include "texture_blob/mip/index.h"

#include "texture_vector/analyze/index.h"

#include <string.h>

uint64_t tb_mip_chain_bytes(uint32_t width, uint32_t height, uint32_t frame_count,
                            uint32_t out_level_offset[TEXTURE_BLOB_MAX_LEVELS]) {
    const uint32_t levels = texture_blob_level_count(width, height);
    uint64_t offset = 0;
    if (out_level_offset != nullptr) {
        memset(out_level_offset, 0, sizeof(uint32_t) * TEXTURE_BLOB_MAX_LEVELS);
    }
    for (uint32_t level = 0; level < levels; level++) {
        if (out_level_offset != nullptr) {
            out_level_offset[level] = (uint32_t)offset;
        }
        offset += texture_blob_level_frame_bytes(width, height, level) * frame_count;
    }
    return offset;
}

/* Level 0 keeps its colours, and says outright what its class promises. */
static void tb_mip_level_zero(const tb_mip_source_t *source, uint8_t *out) {
    const uint64_t pixels = (uint64_t)source->width * source->height * source->frame_count;
    memcpy(out, source->borrowed_pixels, pixels * 4u);
    for (uint64_t i = 0; i < pixels; i++) {
        uint8_t *alpha = out + i * 4u + 3u;
        if (source->alpha_class == TEXTURE_BLOB_ALPHA_OPAQUE) {
            *alpha = 255u;
        } else if (source->alpha_class == TEXTURE_BLOB_ALPHA_CUTOUT) {
            *alpha = *alpha >= TEXTURE_BLOB_CUTOUT_THRESHOLD ? 255u : 0u;
        }
    }
}

/*
 * One texel of a reduced image: an area average over exactly what it covers, so odd sizes such as 165 are not a special
 * case. Lengths are in units of 1 / target_size of a source texel: a target texel spans [x * source_size, (x + 1) *
 * source_size), and a source texel [i * target_size, (i + 1) * target_size).
 *
 * Colour is averaged in linear light, or a far tree goes darker than a near one, and weighted by alpha, or the colour under
 * a transparent texel bleeds in. This is the same rule as mean_rgba in compile/src/texture_vector/analyze/.
 */
static void tb_mip_texel(const uint8_t *source, uint32_t source_w, uint32_t source_h, uint32_t target_w,
                         uint32_t target_h, uint32_t x, uint32_t y, const uint8_t hole_rgb[3], uint8_t *out) {
    const uint64_t left = (uint64_t)x * source_w;
    const uint64_t right = left + source_w;
    const uint64_t top = (uint64_t)y * source_h;
    const uint64_t bottom = top + source_h;
    uint64_t alpha_sum = 0;
    uint64_t linear_sums[3] = {0, 0, 0};
    for (uint64_t sy = top / target_h; sy * target_h < bottom; sy++) {
        const uint64_t span_top = sy * target_h > top ? sy * target_h : top;
        const uint64_t span_bottom = (sy + 1u) * target_h < bottom ? (sy + 1u) * target_h : bottom;
        for (uint64_t sx = left / target_w; sx * target_w < right; sx++) {
            const uint64_t span_left = sx * target_w > left ? sx * target_w : left;
            const uint64_t span_right = (sx + 1u) * target_w < right ? (sx + 1u) * target_w : right;
            const uint64_t weight = (span_right - span_left) * (span_bottom - span_top);
            const uint8_t *pixel = source + (sy * source_w + sx) * 4u;
            const uint64_t weighted = weight * pixel[3];
            alpha_sum += weighted;
            for (uint32_t channel = 0; channel < 3u; channel++) {
                linear_sums[channel] += weighted * tv_linear_from_srgb(pixel[channel]);
            }
        }
    }
    /* All the weights of one target texel add up to its area, source_w * source_h. */
    const uint64_t area = (uint64_t)source_w * source_h;
    out[3] = (uint8_t)((alpha_sum + area / 2u) / area);
    for (uint32_t channel = 0; channel < 3u; channel++) {
        out[channel] = alpha_sum == 0u
                           ? hole_rgb[channel]
                           : tv_srgb_from_linear((uint16_t)((linear_sums[channel] + alpha_sum / 2u) / alpha_sum));
    }
}

/*
 * The texels of a level in an order that spreads out: position in a Bayer matrix, so that the first few of them are far
 * apart wherever you stop. The grid is the power of two that holds the image, and positions outside the image are skipped.
 * Each pair of bits of the position, lowest first, chooses a quadrant, biggest first, in the order (0,0) (1,1) (1,0) (0,1).
 */
static void tb_mip_bayer_position(uint64_t position, uint32_t grid_bits, uint32_t *out_x, uint32_t *out_y) {
    static const uint8_t quadrant_x[4] = {0u, 1u, 1u, 0u};
    static const uint8_t quadrant_y[4] = {0u, 1u, 0u, 1u};
    uint32_t x = 0;
    uint32_t y = 0;
    for (uint32_t bit = 0; bit < grid_bits; bit++) {
        const uint32_t pair = (uint32_t)(position >> (bit * 2u)) & 3u;
        x = (x << 1) | quadrant_x[pair];
        y = (y << 1) | quadrant_y[pair];
    }
    *out_x = x;
    *out_y = y;
}

/*
 * Rounding each averaged alpha at 128 makes leaves thin out or fill in level by level, depending on how much of them there
 * is, and a far tree then looks wrong. So each level is given exactly level 0's share of solid texels, the highest averaged
 * alphas first. Many texels often share one alpha, glass above all, and a threshold could only take them all or none, so
 * those are taken in Bayer order. Only a texel with something under it can be solid. A thing too sparse to fill one texel may
 * vanish at the smallest levels: a torch at 1 by 1 is empty, not a full cube.
 */
static void tb_mip_harden(uint8_t *image, uint32_t width, uint32_t height, uint64_t solid_zero, uint64_t texels_zero) {
    const uint64_t texels = (uint64_t)width * height;
    uint32_t counts[256] = {0};
    for (uint64_t i = 0; i < texels; i++) {
        counts[image[i * 4u + 3u]]++;
    }
    const uint64_t wanted = (solid_zero * texels + texels_zero / 2u) / texels_zero;
    /* The lowest alpha that is at least partly taken, and how many of that alpha are. */
    uint32_t tied_alpha = 0;
    uint64_t tied_taken = 0;
    uint64_t solid = 0;
    for (uint32_t alpha = 255u; alpha >= 1u && solid < wanted; alpha--) {
        tied_alpha = alpha;
        tied_taken = wanted - solid < counts[alpha] ? wanted - solid : counts[alpha];
        solid += tied_taken;
    }
    /* Undecided texels wait at alpha 1 for the pass below: at this point nothing else is left at 1. */
    for (uint64_t i = 0; i < texels; i++) {
        uint8_t *alpha = image + i * 4u + 3u;
        if (tied_alpha != 0u && *alpha == tied_alpha) {
            *alpha = 1u;
        } else {
            *alpha = tied_alpha != 0u && *alpha > tied_alpha ? 255u : 0u;
        }
    }
    uint32_t grid_bits = 0;
    while ((1ull << grid_bits) < width || (1ull << grid_bits) < height) {
        grid_bits++;
    }
    const uint64_t grid_texels = 1ull << (grid_bits * 2u);
    for (uint64_t position = 0; position < grid_texels && tied_alpha != 0u; position++) {
        uint32_t x = 0;
        uint32_t y = 0;
        tb_mip_bayer_position(position, grid_bits, &x, &y);
        if (x < width && y < height) {
            uint8_t *alpha = image + ((uint64_t)y * width + x) * 4u + 3u;
            if (*alpha == 1u) {
                *alpha = tied_taken > 0u ? 255u : 0u;
                tied_taken -= tied_taken > 0u ? 1u : 0u;
            }
        }
    }
}

void tb_mip_build(const tb_mip_source_t *source, uint8_t *out_chain) {
    const uint32_t width = source->width;
    const uint32_t height = source->height;
    uint32_t level_offset[TEXTURE_BLOB_MAX_LEVELS];
    tb_mip_chain_bytes(width, height, source->frame_count, level_offset);
    tb_mip_level_zero(source, out_chain);
    const uint32_t levels = texture_blob_level_count(width, height);
    const uint64_t texels_zero = (uint64_t)width * height;
    for (uint32_t frame = 0; frame < source->frame_count; frame++) {
        /* Every level comes straight from level 0, not from the one before, so rounding errors do not add up. */
        const uint8_t *zero = out_chain + texels_zero * 4u * frame;
        uint64_t solid_zero = 0;
        for (uint64_t i = 0; i < texels_zero; i++) {
            solid_zero += zero[i * 4u + 3u] != 0u ? 1u : 0u;
        }
        for (uint32_t level = 1; level < levels; level++) {
            const uint32_t level_w = texture_blob_level_dimension(width, level);
            const uint32_t level_h = texture_blob_level_dimension(height, level);
            const uint64_t frame_bytes = texture_blob_level_frame_bytes(width, height, level);
            uint8_t *image = out_chain + level_offset[level] + frame_bytes * frame;
            for (uint32_t y = 0; y < level_h; y++) {
                for (uint32_t x = 0; x < level_w; x++) {
                    tb_mip_texel(zero, width, height, level_w, level_h, x, y, source->hole_rgb,
                                 image + ((uint64_t)y * level_w + x) * 4u);
                }
            }
            if (source->alpha_class == TEXTURE_BLOB_ALPHA_CUTOUT) {
                tb_mip_harden(image, level_w, level_h, solid_zero, texels_zero);
            }
        }
    }
}
