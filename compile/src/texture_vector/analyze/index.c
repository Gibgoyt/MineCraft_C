/* See README.md. */
#include "texture_vector/analyze/index.h"

#include <string.h>

/* A flame or a coal is warm when its red beats its blue by this much. */
#define TV_WARM_MARGIN 48

/* round(65535 * sRGB-decode(i / 255)). A table, not pow, so the result cannot depend on a libm. The test checks it against pow. */
static const uint16_t tv_srgb_to_linear[256] = {
    0,     20,    40,    60,    80,    99,    119,   139,   159,   179,   199,   219,   241,   264,   288,   313,
    340,   367,   396,   427,   458,   491,   526,   562,   599,   637,   677,   718,   761,   805,   851,   898,
    947,   997,   1048,  1101,  1156,  1212,  1270,  1330,  1391,  1453,  1517,  1583,  1651,  1720,  1790,  1863,
    1937,  2013,  2090,  2170,  2250,  2333,  2418,  2504,  2592,  2681,  2773,  2866,  2961,  3058,  3157,  3258,
    3360,  3464,  3570,  3678,  3788,  3900,  4014,  4129,  4247,  4366,  4488,  4611,  4736,  4864,  4993,  5124,
    5257,  5392,  5530,  5669,  5810,  5953,  6099,  6246,  6395,  6547,  6700,  6856,  7014,  7174,  7335,  7500,
    7666,  7834,  8004,  8177,  8352,  8528,  8708,  8889,  9072,  9258,  9445,  9635,  9828,  10022, 10219, 10417,
    10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090, 12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909,
    14146, 14387, 14629, 14874, 15122, 15371, 15623, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001,
    18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281, 20577, 20876, 21177, 21481, 21787, 22096, 22407, 22721,
    23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325, 25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094,
    28452, 28813, 29176, 29542, 29911, 30282, 30656, 31033, 31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143,
    34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429, 37852, 38278, 38706, 39138, 39572, 40009, 40449, 40891,
    41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534, 45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359,
    48850, 49344, 49841, 50341, 50844, 51349, 51858, 52369, 52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567,
    57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955, 61517, 62082, 62650, 63221, 63795, 64372, 64952, 65535,
};

uint16_t tv_linear_from_srgb(uint8_t value) {
    return tv_srgb_to_linear[value];
}

uint8_t tv_srgb_from_linear(uint16_t value) {
    uint32_t low = 0;
    uint32_t high = 255;
    while (low < high) {
        const uint32_t middle = (low + high) / 2u;
        if (tv_srgb_to_linear[middle] < value) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    /* low is the first entry at or above value, so the nearest is it or the one before. */
    if (low > 0 && (uint32_t)value - tv_srgb_to_linear[low - 1u] <= (uint32_t)tv_srgb_to_linear[low] - value) {
        low--;
    }
    return (uint8_t)low;
}

uint8_t tv_luma(uint8_t red, uint8_t green, uint8_t blue) {
    /* 0.2126, 0.7152, 0.0722 in 1/256ths; the weights add up to exactly 256, so white stays 255. */
    return (uint8_t)((54u * red + 183u * green + 19u * blue + 128u) >> 8);
}

static uint64_t tv_square_root(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = 1ull << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

/* numerator / denominator, rounded to nearest, half away from zero. denominator is above 0. */
static int64_t tv_divide_rounded(int64_t numerator, int64_t denominator) {
    return numerator >= 0 ? (numerator + denominator / 2) / denominator
                          : -((-numerator + denominator / 2) / denominator);
}

static void tv_analyze_alpha_and_mean(const tv_image_t *image, tv_vector_t *out) {
    uint64_t alpha_sum = 0;
    uint64_t linear_sums[3] = {0, 0, 0};
    uint64_t luma_sum = 0;
    uint32_t visible = 0;
    uint32_t luma_min = 255;
    uint32_t luma_max = 0;
    bool any_partial = false;
    for (uint64_t i = 0; i < out->pixel_count; i++) {
        const uint8_t *pixel = image->borrowed_pixels + i * 4u;
        const uint32_t alpha = pixel[3];
        alpha_sum += alpha;
        any_partial = any_partial || (alpha != 0 && alpha != 255u);
        if (alpha == 0) {
            continue;
        }
        visible++;
        for (uint32_t channel = 0; channel < 3u; channel++) {
            linear_sums[channel] += (uint64_t)tv_srgb_to_linear[pixel[channel]] * alpha;
        }
        const uint32_t luma = tv_luma(pixel[0], pixel[1], pixel[2]);
        luma_sum += luma;
        luma_min = luma < luma_min ? luma : luma_min;
        luma_max = luma > luma_max ? luma : luma_max;
    }
    out->visible_pixels = visible;
    if (any_partial) {
        out->alpha_class = MCVEC_ALPHA_TRANSLUCENT;
    } else {
        out->alpha_class = visible == out->pixel_count ? MCVEC_ALPHA_OPAQUE : MCVEC_ALPHA_CUTOUT;
    }
    if (visible == 0) {
        return; /* mean, luma and the rest stay zero */
    }
    /* Averaging sRGB bytes directly would darken every far-away texture: a far tree must look as bright as a near one. */
    for (uint32_t channel = 0; channel < 3u; channel++) {
        const uint64_t linear = (linear_sums[channel] + alpha_sum / 2u) / alpha_sum;
        out->mean_rgba[channel] = tv_srgb_from_linear((uint16_t)linear);
    }
    out->mean_rgba[3] = (uint8_t)((alpha_sum + out->pixel_count / 2u) / out->pixel_count);
    out->luma_min = (uint8_t)luma_min;
    out->luma_max = (uint8_t)luma_max;
    out->luma_mean = (uint8_t)((luma_sum + visible / 2u) / visible);
}

static void tv_analyze_height(const tv_image_t *image, tv_vector_t *out) {
    const uint32_t span = (uint32_t)out->luma_max - out->luma_min;
    for (uint64_t i = 0; i < out->pixel_count; i++) {
        const uint8_t *pixel = image->borrowed_pixels + i * 4u;
        if (pixel[3] == 0) {
            out->height[i] = 0;
        } else if (span < TV_HEIGHT_MIN_SPAN) {
            out->height[i] = TV_HEIGHT_FLAT;
        } else {
            const uint32_t luma = tv_luma(pixel[0], pixel[1], pixel[2]);
            out->height[i] = (uint8_t)(((luma - out->luma_min) * 255u + span / 2u) / span);
        }
    }
}

/*
 * The luma of (x, y) in one frame, wrapping at the edges, since block faces tile. A transparent neighbour answers with the
 * centre's own luma, so that the edge of a torch or a leaf does not look like a cliff.
 */
static int32_t tv_neighbour_luma(const uint8_t *frame, uint32_t width, uint32_t height, uint32_t x, uint32_t y,
                                 int32_t offset_x, int32_t offset_y, int32_t centre) {
    const uint32_t wrapped_x = (uint32_t)(((int64_t)x + offset_x + (int64_t)width) % (int64_t)width);
    const uint32_t wrapped_y = (uint32_t)(((int64_t)y + offset_y + (int64_t)height) % (int64_t)height);
    const uint8_t *pixel = frame + ((uint64_t)wrapped_y * width + wrapped_x) * 4u;
    return pixel[3] == 0 ? centre : (int32_t)tv_luma(pixel[0], pixel[1], pixel[2]);
}

/* A Sobel filter on luma, not on the stretched height, so a flat-looking texture also gets flat-looking normals. */
static void tv_analyze_normal(const tv_image_t *image, tv_vector_t *out) {
    const uint64_t frame_pixels = (uint64_t)image->width * image->height;
    for (uint32_t f = 0; f < image->frames; f++) {
        const uint8_t *frame = image->borrowed_pixels + frame_pixels * f * 4u;
        uint8_t *normal = out->normal + frame_pixels * f * 2u;
        for (uint32_t y = 0; y < image->height; y++) {
            for (uint32_t x = 0; x < image->width; x++) {
                const uint8_t *pixel = frame + ((uint64_t)y * image->width + x) * 4u;
                const uint64_t at = ((uint64_t)y * image->width + x) * 2u;
                if (pixel[3] == 0) {
                    normal[at] = 0;
                    normal[at + 1u] = 0;
                    continue;
                }
                const int32_t centre = tv_luma(pixel[0], pixel[1], pixel[2]);
                int32_t s[3][3];
                for (int32_t dy = -1; dy <= 1; dy++) {
                    for (int32_t dx = -1; dx <= 1; dx++) {
                        s[dy + 1][dx + 1] = tv_neighbour_luma(frame, image->width, image->height, x, y, dx, dy, centre);
                    }
                }
                /* Sobel gives 8 times the slope. So the z of the vector below is 8 * 255, the slope's unit. */
                const int64_t slope_x = s[0][2] + 2 * s[1][2] + s[2][2] - s[0][0] - 2 * s[1][0] - s[2][0];
                const int64_t slope_y = s[2][0] + 2 * s[2][1] + s[2][2] - s[0][0] - 2 * s[0][1] - s[0][2];
                const int64_t vector_x = -slope_x * TV_NORMAL_STRENGTH;
                const int64_t vector_y = -slope_y * TV_NORMAL_STRENGTH;
                const int64_t vector_z = 8 * 255;
                const uint64_t length =
                    tv_square_root((uint64_t)(vector_x * vector_x) + (uint64_t)(vector_y * vector_y) +
                                   (uint64_t)(vector_z * vector_z));
                normal[at] = (uint8_t)(int8_t)tv_divide_rounded(vector_x * 127, (int64_t)length);
                normal[at + 1u] = (uint8_t)(int8_t)tv_divide_rounded(vector_y * 127, (int64_t)length);
            }
        }
    }
}

static void tv_analyze_emissive(const tv_image_t *image, tv_material_t material, tv_vector_t *out) {
    uint32_t lit = 0;
    if (material.emissive_level != 0) {
        for (uint64_t i = 0; i < out->pixel_count; i++) {
            const uint8_t *pixel = image->borrowed_pixels + i * 4u;
            const bool bright = tv_luma(pixel[0], pixel[1], pixel[2]) >= material.min_luma;
            const bool warm = material.warm == 0 || (int32_t)pixel[0] - (int32_t)pixel[2] >= TV_WARM_MARGIN;
            if (pixel[3] != 0 && bright && warm) {
                out->emissive[i] = material.emissive_level;
                lit++;
            }
        }
    }
    out->emissive_pixels = lit;
    if (lit == 0) {
        out->emissive_class = MCVEC_EMISSIVE_NONE;
        out->emissive_level = 0;
    } else {
        out->emissive_class = lit == out->visible_pixels ? MCVEC_EMISSIVE_FULL : MCVEC_EMISSIVE_PARTIAL;
        out->emissive_level = material.emissive_level;
    }
}

void tv_analyze(tp_arena_t *arena, const tv_image_t *image, tv_material_t material, tv_vector_t *out) {
    memset(out, 0, sizeof(*out));
    out->pixel_count = image->pixel_bytes / 4u;
    out->indices = tp_arena_alloc(arena, out->pixel_count);
    out->height = tp_arena_alloc(arena, out->pixel_count);
    out->normal = tp_arena_alloc(arena, out->pixel_count * 2u);
    out->emissive = tp_arena_alloc(arena, out->pixel_count);
    tv_palette_build(arena, image, &out->palette, out->indices);
    tv_analyze_alpha_and_mean(image, out);
    tv_analyze_height(image, out);
    tv_analyze_normal(image, out);
    tv_analyze_emissive(image, material, out);
    if (material.alpha_class != TV_ALPHA_FROM_PIXELS) {
        out->alpha_class = material.alpha_class;
    }
}
