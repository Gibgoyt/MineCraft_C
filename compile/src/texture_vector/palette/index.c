/* See README.md. */
#include "texture_vector/palette/index.h"

#include <stdlib.h>
#include <string.h>

/* One distinct colour. rgba is the bytes in file order, as one little-endian number. */
typedef struct tv_colour_t {
    uint32_t rgba;  /* 0 */
    uint32_t count; /* 4 */
} tv_colour_t;
static_assert(sizeof(tv_colour_t) == 8);

/* A run of colours in one median-cut box. */
typedef struct tv_box_t {
    uint32_t first;  /* 0 */
    uint32_t length; /* 4 */
} tv_box_t;
static_assert(sizeof(tv_box_t) == 8);

static uint32_t tv_channel(uint32_t rgba, uint32_t channel) {
    return (rgba >> (channel * 8u)) & 0xFFu;
}

static uint32_t tv_pixel_at(const uint8_t *pixels, uint64_t position) {
    const uint8_t *pixel = pixels + position * 4u;
    if (pixel[3] == 0) {
        return 0;
    }
    return (uint32_t)pixel[0] | ((uint32_t)pixel[1] << 8) | ((uint32_t)pixel[2] << 16) | ((uint32_t)pixel[3] << 24);
}

static int32_t tv_compare_u32(const void *left, const void *right) {
    const uint32_t a = *(const uint32_t *)left;
    const uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

/* Compares one channel first, and the whole colour after that, so that no two distinct colours compare equal. */
static int32_t tv_compare_by_channel(const tv_colour_t *a, const tv_colour_t *b, uint32_t channel) {
    const uint32_t left = tv_channel(a->rgba, channel);
    const uint32_t right = tv_channel(b->rgba, channel);
    if (left != right) {
        return left < right ? -1 : 1;
    }
    return (a->rgba > b->rgba) - (a->rgba < b->rgba);
}

static int32_t tv_compare_channel_0(const void *a, const void *b) {
    return tv_compare_by_channel(a, b, 0);
}
static int32_t tv_compare_channel_1(const void *a, const void *b) {
    return tv_compare_by_channel(a, b, 1);
}
static int32_t tv_compare_channel_2(const void *a, const void *b) {
    return tv_compare_by_channel(a, b, 2);
}
static int32_t tv_compare_channel_3(const void *a, const void *b) {
    return tv_compare_by_channel(a, b, 3);
}

typedef int32_t (*tv_compare_t)(const void *, const void *);
static const tv_compare_t tv_channel_compares[4] = {tv_compare_channel_0, tv_compare_channel_1, tv_compare_channel_2,
                                                    tv_compare_channel_3};

/* Most used first, and by value on a tie. */
static int32_t tv_compare_entry(const void *left, const void *right) {
    const tv_colour_t *a = left;
    const tv_colour_t *b = right;
    if (a->count != b->count) {
        return a->count > b->count ? -1 : 1;
    }
    return (a->rgba > b->rgba) - (a->rgba < b->rgba);
}

/* The sorted distinct colours of the image, and how many pixels use each. */
static uint32_t tv_palette_distinct(tp_arena_t *arena, const tv_image_t *image, uint64_t pixel_count,
                                    tv_colour_t **out_colours) {
    uint32_t *sorted = tp_arena_alloc(arena, pixel_count * sizeof(uint32_t));
    for (uint64_t i = 0; i < pixel_count; i++) {
        sorted[i] = tv_pixel_at(image->borrowed_pixels, i);
    }
    qsort(sorted, pixel_count, sizeof(uint32_t), tv_compare_u32);
    uint32_t distinct = 0;
    tv_colour_t *colours = tp_arena_alloc(arena, pixel_count * sizeof(tv_colour_t));
    for (uint64_t i = 0; i < pixel_count; i++) {
        if (distinct > 0 && colours[distinct - 1u].rgba == sorted[i]) {
            colours[distinct - 1u].count++;
        } else {
            colours[distinct].rgba = sorted[i];
            colours[distinct].count = 1;
            distinct++;
        }
    }
    *out_colours = colours;
    return distinct;
}

/* The widest channel of a box, and how wide it is. A box of one colour has width 0, and cannot be split. */
static uint32_t tv_box_widest(const tv_colour_t *colours, tv_box_t box, uint32_t *out_channel) {
    uint32_t widest = 0;
    *out_channel = 0;
    for (uint32_t channel = 0; channel < 4u; channel++) {
        uint32_t low = 255;
        uint32_t high = 0;
        for (uint32_t i = box.first; i < box.first + box.length; i++) {
            const uint32_t value = tv_channel(colours[i].rgba, channel);
            low = value < low ? value : low;
            high = value > high ? value : high;
        }
        if (high - low > widest) {
            widest = high - low;
            *out_channel = channel;
        }
    }
    return widest;
}

/* Cuts boxes until there are TV_PALETTE_MAX, always the box with the widest channel. Returns how many there are. */
static uint32_t tv_median_cut(tv_colour_t *colours, uint32_t distinct, tv_box_t *boxes) {
    boxes[0] = (tv_box_t){.first = 0, .length = distinct};
    uint32_t box_count = 1;
    while (box_count < TV_PALETTE_MAX) {
        uint32_t best = TV_PALETTE_MAX;
        uint32_t best_width = 0;
        uint32_t best_channel = 0;
        for (uint32_t b = 0; b < box_count; b++) {
            uint32_t channel = 0;
            const uint32_t width = tv_box_widest(colours, boxes[b], &channel);
            if (width > best_width) {
                best = b;
                best_width = width;
                best_channel = channel;
            }
        }
        if (best == TV_PALETTE_MAX) {
            break;
        }
        const tv_box_t box = boxes[best];
        qsort(colours + box.first, box.length, sizeof(tv_colour_t), tv_channel_compares[best_channel]);
        uint64_t total = 0;
        for (uint32_t i = box.first; i < box.first + box.length; i++) {
            total += colours[i].count;
        }
        /* The cut goes where half the pixels fall on each side, and leaves at least one colour on each. */
        uint64_t running = 0;
        uint32_t left = 0;
        while (left + 1u < box.length && (left == 0 || running * 2u < total)) {
            running += colours[box.first + left].count;
            left++;
        }
        boxes[best].length = left;
        boxes[box_count] = (tv_box_t){.first = box.first + left, .length = box.length - left};
        box_count++;
    }
    return box_count;
}

static uint32_t tv_box_mean(const tv_colour_t *colours, tv_box_t box, uint32_t *out_pixels) {
    uint64_t sums[4] = {0, 0, 0, 0};
    uint64_t total = 0;
    for (uint32_t i = box.first; i < box.first + box.length; i++) {
        total += colours[i].count;
        for (uint32_t channel = 0; channel < 4u; channel++) {
            sums[channel] += (uint64_t)tv_channel(colours[i].rgba, channel) * colours[i].count;
        }
    }
    uint32_t rgba = 0;
    for (uint32_t channel = 0; channel < 4u; channel++) {
        rgba |= (uint32_t)((sums[channel] + total / 2u) / total) << (channel * 8u);
    }
    *out_pixels = (uint32_t)total;
    return rgba;
}

static uint32_t tv_palette_nearest(const tv_colour_t *entries, uint32_t count, uint32_t rgba) {
    uint32_t best = 0;
    uint64_t best_distance = UINT64_MAX;
    for (uint32_t e = 0; e < count; e++) {
        uint64_t distance = 0;
        for (uint32_t channel = 0; channel < 4u; channel++) {
            const int64_t gap = (int64_t)tv_channel(entries[e].rgba, channel) - (int64_t)tv_channel(rgba, channel);
            distance += (uint64_t)(gap * gap);
        }
        if (distance < best_distance) {
            best_distance = distance;
            best = e;
        }
    }
    return best;
}

void tv_palette_build(tp_arena_t *arena, const tv_image_t *image, tv_palette_t *out, uint8_t *out_indices) {
    const uint64_t mark = arena->used;
    const uint64_t pixel_count = image->pixel_bytes / 4u;
    memset(out, 0, sizeof(*out));

    tv_colour_t *colours = nullptr;
    const uint32_t distinct = tv_palette_distinct(arena, image, pixel_count, &colours);
    tv_colour_t entries[TV_PALETTE_MAX];
    uint32_t count = 0;
    if (distinct <= TV_PALETTE_MAX) {
        memcpy(entries, colours, (uint64_t)distinct * sizeof(tv_colour_t));
        count = distinct;
        out->exact = 1;
    } else {
        tv_box_t boxes[TV_PALETTE_MAX];
        count = tv_median_cut(colours, distinct, boxes);
        for (uint32_t b = 0; b < count; b++) {
            uint32_t pixels = 0;
            entries[b].rgba = tv_box_mean(colours, boxes[b], &pixels);
            entries[b].count = pixels;
        }
    }
    qsort(entries, count, sizeof(tv_colour_t), tv_compare_entry);
    out->count = count;
    for (uint32_t e = 0; e < count; e++) {
        memcpy(&out->rgba[e * 4u], &entries[e].rgba, 4u);
    }

    /* Each distinct colour finds its entry once. Pixels then only look their colour up, so this is not pixels * 256. */
    uint32_t *distinct_values = tp_arena_alloc(arena, (uint64_t)distinct * sizeof(uint32_t));
    uint8_t *distinct_index = tp_arena_alloc(arena, distinct);
    for (uint32_t i = 0; i < distinct; i++) {
        distinct_values[i] = colours[i].rgba;
    }
    qsort(distinct_values, distinct, sizeof(uint32_t), tv_compare_u32);
    for (uint32_t i = 0; i < distinct; i++) {
        distinct_index[i] = (uint8_t)tv_palette_nearest(entries, count, distinct_values[i]);
    }
    for (uint64_t i = 0; i < pixel_count; i++) {
        const uint32_t value = tv_pixel_at(image->borrowed_pixels, i);
        const uint32_t *found = bsearch(&value, distinct_values, distinct, sizeof(uint32_t), tv_compare_u32);
        out_indices[i] = distinct_index[found - distinct_values];
    }
    arena->used = mark;
}
