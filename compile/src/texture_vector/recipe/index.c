/* See README.md. */
#include "texture_vector/recipe/index.h"

#include <inttypes.h>
#include <string.h>

#define TV_RECIPE_MAX_OPS 4096u
#define TV_RECIPE_MAX_COORDINATE 16384
#define TV_RECIPE_MAX_COUNT 65536
#define TV_RECIPE_MAX_RADIUS 256
#define TV_RECIPE_MAX_AMOUNT 255

/* One frame being painted, and what its operations need. */
typedef struct tv_canvas_t {
    tp_arena_t *arena;               /* 0 */
    const yaml_document_t *document; /* 8 */
    yaml_error_t *error;             /* 16 */
    uint8_t *pixels;                 /* 24 */
    uint64_t seed;                   /* 32: for this operation in this frame, unless it names its own */
    uint32_t width;                  /* 40 */
    uint32_t height;                 /* 44 */
} tv_canvas_t;
static_assert(sizeof(tv_canvas_t) == 48);

/* A rectangle inside the canvas. */
typedef struct tv_region_t {
    uint32_t x;      /* 0 */
    uint32_t y;      /* 4 */
    uint32_t width;  /* 8 */
    uint32_t height; /* 12 */
} tv_region_t;
static_assert(sizeof(tv_region_t) == 16);

typedef struct tv_colour_bytes_t {
    uint8_t rgba[4]; /* 0 */
} tv_colour_bytes_t;
static_assert(sizeof(tv_colour_bytes_t) == 4);

typedef bool (*tv_op_function_t)(tv_canvas_t *canvas, const yaml_node_t *op);

typedef struct tv_op_row_t {
    const char *name;          /* 0 */
    tv_op_function_t function; /* 8 */
    const char *const *keys;   /* 16: ends in nullptr; op itself is always allowed */
} tv_op_row_t;
static_assert(sizeof(tv_op_row_t) == 24);

/* ---- Random numbers --------------------------------------------------------------------------------------------- */

/* SplitMix64: small, and fixed here, so that a recipe's pixels depend on nothing outside this file. */
static uint64_t tv_random_next(uint64_t *state) {
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static uint32_t tv_random_below(uint64_t *state, uint32_t limit) {
    return (uint32_t)(tv_random_next(state) % limit);
}

static uint64_t tv_seed_mix(uint64_t a, uint64_t b) {
    uint64_t state = a ^ (b * 0xD6E8FEB86659FD93ull);
    return tv_random_next(&state);
}

/* ---- Reading an operation's parameters ------------------------------------------------------------------------- */

static bool tv_read_int(const tv_canvas_t *canvas, const yaml_node_t *node, int64_t low, int64_t high, int64_t *out) {
    if (!yaml_get_int64(node, out, canvas->error)) {
        return false;
    }
    if (*out < low || *out > high) {
        return yaml_fail_at_node(node, canvas->error, "must be from %" PRId64 " to %" PRId64, low, high);
    }
    return true;
}

static bool tv_need_int(const tv_canvas_t *canvas, const yaml_node_t *op, const char *key, int64_t low, int64_t high,
                        int64_t *out) {
    const yaml_node_t *node = yaml_map_get(canvas->document, op, key);
    if (node == nullptr) {
        return yaml_fail_at_node(op, canvas->error, "needs %s", key);
    }
    return tv_read_int(canvas, node, low, high, out);
}

static bool tv_want_int(const tv_canvas_t *canvas, const yaml_node_t *op, const char *key, int64_t low, int64_t high,
                        int64_t fallback, int64_t *out) {
    const yaml_node_t *node = yaml_map_get(canvas->document, op, key);
    *out = fallback;
    return node == nullptr || tv_read_int(canvas, node, low, high, out);
}

static int32_t tv_hex_digit(uint8_t c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* "#RRGGBB" or "#RRGGBBAA". */
static bool tv_read_colour_node(const tv_canvas_t *canvas, const yaml_node_t *node, tv_colour_bytes_t *out) {
    const uint8_t *text = nullptr;
    uint32_t length = 0;
    if (!yaml_get_text(node, &text, &length, canvas->error)) {
        return false;
    }
    if ((length != 7u && length != 9u) || text[0] != '#') {
        return yaml_fail_at_node(node, canvas->error, "a colour is \"#RRGGBB\" or \"#RRGGBBAA\"");
    }
    out->rgba[3] = 255;
    for (uint32_t i = 0; i < (length - 1u) / 2u; i++) {
        const int32_t high = tv_hex_digit(text[1u + i * 2u]);
        const int32_t low = tv_hex_digit(text[2u + i * 2u]);
        if (high < 0 || low < 0) {
            return yaml_fail_at_node(node, canvas->error, "a colour is \"#RRGGBB\" or \"#RRGGBBAA\"");
        }
        out->rgba[i] = (uint8_t)(high * 16 + low);
    }
    return true;
}

static bool tv_need_colour(const tv_canvas_t *canvas, const yaml_node_t *op, const char *key, tv_colour_bytes_t *out) {
    const yaml_node_t *node = yaml_map_get(canvas->document, op, key);
    if (node == nullptr) {
        return yaml_fail_at_node(op, canvas->error, "needs %s", key);
    }
    return tv_read_colour_node(canvas, node, out);
}

/* x, y, w and h, all optional, default to the whole canvas. The result must fit in it. */
static bool tv_read_region(const tv_canvas_t *canvas, const yaml_node_t *op, tv_region_t *out) {
    int64_t x = 0;
    int64_t y = 0;
    int64_t w = 0;
    int64_t h = 0;
    if (!tv_want_int(canvas, op, "x", 0, canvas->width - 1, 0, &x) ||
        !tv_want_int(canvas, op, "y", 0, canvas->height - 1, 0, &y) ||
        !tv_want_int(canvas, op, "w", 1, canvas->width, canvas->width - x, &w) ||
        !tv_want_int(canvas, op, "h", 1, canvas->height, canvas->height - y, &h)) {
        return false;
    }
    if (x + w > canvas->width || y + h > canvas->height) {
        return yaml_fail_at_node(op, canvas->error, "this rectangle does not fit the %ux%u canvas", canvas->width,
                                 canvas->height);
    }
    *out = (tv_region_t){.x = (uint32_t)x, .y = (uint32_t)y, .width = (uint32_t)w, .height = (uint32_t)h};
    return true;
}

/* The operation's own seed, if it names one, still mixed with the frame, and otherwise the canvas's. */
static bool tv_read_seed(const tv_canvas_t *canvas, const yaml_node_t *op, uint64_t *out) {
    const yaml_node_t *node = yaml_map_get(canvas->document, op, "seed");
    if (node == nullptr) {
        *out = canvas->seed;
        return true;
    }
    int64_t value = 0;
    if (!tv_read_int(canvas, node, 0, INT64_MAX, &value)) {
        return false;
    }
    *out = tv_seed_mix(canvas->seed, (uint64_t)value);
    return true;
}

/* A truth value, false if absent. */
static bool tv_want_bool(const tv_canvas_t *canvas, const yaml_node_t *op, const char *key, bool *out) {
    const yaml_node_t *node = yaml_map_get(canvas->document, op, key);
    *out = false;
    return node == nullptr || yaml_get_bool(node, out, canvas->error);
}

/* ---- Pixels ----------------------------------------------------------------------------------------------------- */

static uint8_t *tv_at(const tv_canvas_t *canvas, uint32_t x, uint32_t y) {
    return canvas->pixels + ((uint64_t)y * canvas->width + x) * 4u;
}

static void tv_put(const tv_canvas_t *canvas, uint32_t x, uint32_t y, tv_colour_bytes_t colour) {
    memcpy(tv_at(canvas, x, y), colour.rgba, 4u);
}

/* The same, for coordinates that may lie outside: what falls outside is dropped. */
static void tv_put_clipped(const tv_canvas_t *canvas, int64_t x, int64_t y, tv_colour_bytes_t colour) {
    if (x >= 0 && y >= 0 && x < canvas->width && y < canvas->height) {
        tv_put(canvas, (uint32_t)x, (uint32_t)y, colour);
    }
}

static uint8_t tv_clamp_byte(int32_t value) {
    return (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
}

/* ---- Operations ------------------------------------------------------------------------------------------------- */

static const char *const tv_keys_fill[] = {"color", "x", "y", "w", "h", nullptr};
static bool tv_op_fill(tv_canvas_t *canvas, const yaml_node_t *op) {
    tv_colour_bytes_t colour;
    tv_region_t region;
    if (!tv_need_colour(canvas, op, "color", &colour) || !tv_read_region(canvas, op, &region)) {
        return false;
    }
    for (uint32_t y = region.y; y < region.y + region.height; y++) {
        for (uint32_t x = region.x; x < region.x + region.width; x++) {
            tv_put(canvas, x, y, colour);
        }
    }
    return true;
}

static const char *const tv_keys_outline[] = {"color", "x", "y", "w", "h", nullptr};
static bool tv_op_outline(tv_canvas_t *canvas, const yaml_node_t *op) {
    tv_colour_bytes_t colour;
    tv_region_t region;
    if (!tv_need_colour(canvas, op, "color", &colour) || !tv_read_region(canvas, op, &region)) {
        return false;
    }
    for (uint32_t x = region.x; x < region.x + region.width; x++) {
        tv_put(canvas, x, region.y, colour);
        tv_put(canvas, x, region.y + region.height - 1u, colour);
    }
    for (uint32_t y = region.y; y < region.y + region.height; y++) {
        tv_put(canvas, region.x, y, colour);
        tv_put(canvas, region.x + region.width - 1u, y, colour);
    }
    return true;
}

static const char *const tv_keys_line[] = {"color", "x0", "y0", "x1", "y1", nullptr};
static bool tv_op_line(tv_canvas_t *canvas, const yaml_node_t *op) {
    tv_colour_bytes_t colour;
    int64_t x0 = 0;
    int64_t y0 = 0;
    int64_t x1 = 0;
    int64_t y1 = 0;
    if (!tv_need_colour(canvas, op, "color", &colour) ||
        !tv_need_int(canvas, op, "x0", -TV_RECIPE_MAX_COORDINATE, TV_RECIPE_MAX_COORDINATE, &x0) ||
        !tv_need_int(canvas, op, "y0", -TV_RECIPE_MAX_COORDINATE, TV_RECIPE_MAX_COORDINATE, &y0) ||
        !tv_need_int(canvas, op, "x1", -TV_RECIPE_MAX_COORDINATE, TV_RECIPE_MAX_COORDINATE, &x1) ||
        !tv_need_int(canvas, op, "y1", -TV_RECIPE_MAX_COORDINATE, TV_RECIPE_MAX_COORDINATE, &y1)) {
        return false;
    }
    /* Bresenham. */
    const int64_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int64_t dy = y1 > y0 ? y0 - y1 : y1 - y0;
    const int64_t step_x = x0 < x1 ? 1 : -1;
    const int64_t step_y = y0 < y1 ? 1 : -1;
    int64_t balance = dx + dy;
    while (true) {
        tv_put_clipped(canvas, x0, y0, colour);
        if (x0 == x1 && y0 == y1) {
            return true;
        }
        const int64_t twice = balance * 2;
        if (twice >= dy) {
            balance += dy;
            x0 += step_x;
        }
        if (twice <= dx) {
            balance += dx;
            y0 += step_y;
        }
    }
}

static const char *const tv_keys_disc[] = {"color", "cx", "cy", "rx", "ry", nullptr};
static bool tv_op_disc(tv_canvas_t *canvas, const yaml_node_t *op) {
    tv_colour_bytes_t colour;
    int64_t cx = 0;
    int64_t cy = 0;
    int64_t rx = 0;
    int64_t ry = 0;
    if (!tv_need_colour(canvas, op, "color", &colour) ||
        !tv_need_int(canvas, op, "cx", -TV_RECIPE_MAX_COORDINATE, TV_RECIPE_MAX_COORDINATE, &cx) ||
        !tv_need_int(canvas, op, "cy", -TV_RECIPE_MAX_COORDINATE, TV_RECIPE_MAX_COORDINATE, &cy) ||
        !tv_need_int(canvas, op, "rx", 0, TV_RECIPE_MAX_RADIUS, &rx) ||
        !tv_need_int(canvas, op, "ry", 0, TV_RECIPE_MAX_RADIUS, &ry)) {
        return false;
    }
    /* (x / rx)^2 + (y / ry)^2 <= 1, in whole numbers. Half a radius more gives a rounder shape than the exact test. */
    const int64_t ax = rx * 2 + 1;
    const int64_t ay = ry * 2 + 1;
    for (int64_t y = -ry; y <= ry; y++) {
        for (int64_t x = -rx; x <= rx; x++) {
            if ((x * 2 * ay) * (x * 2 * ay) + (y * 2 * ax) * (y * 2 * ax) <= (ax * ay) * (ax * ay)) {
                tv_put_clipped(canvas, cx + x, cy + y, colour);
            }
        }
    }
    return true;
}

static const char *const tv_keys_gradient[] = {"from", "to", "axis", "x", "y", "w", "h", nullptr};
static bool tv_op_gradient(tv_canvas_t *canvas, const yaml_node_t *op) {
    tv_colour_bytes_t from;
    tv_colour_bytes_t to;
    tv_region_t region;
    if (!tv_need_colour(canvas, op, "from", &from) || !tv_need_colour(canvas, op, "to", &to) ||
        !tv_read_region(canvas, op, &region)) {
        return false;
    }
    const yaml_node_t *axis = yaml_map_get(canvas->document, op, "axis");
    if (axis == nullptr || (!yaml_text_equals(axis, "x") && !yaml_text_equals(axis, "y"))) {
        return yaml_fail_at_node(axis != nullptr ? axis : op, canvas->error, "needs axis, x or y");
    }
    const bool along_x = yaml_text_equals(axis, "x");
    const int32_t steps = (int32_t)(along_x ? region.width : region.height) - 1;
    for (uint32_t y = 0; y < region.height; y++) {
        for (uint32_t x = 0; x < region.width; x++) {
            const int32_t position = (int32_t)(along_x ? x : y);
            tv_colour_bytes_t colour = from;
            for (uint32_t c = 0; c < 4u && steps > 0; c++) {
                const int32_t gap = ((int32_t)to.rgba[c] - (int32_t)from.rgba[c]) * position;
                const int32_t rounded = gap >= 0 ? (gap + steps / 2) / steps : -((-gap + steps / 2) / steps);
                colour.rgba[c] = (uint8_t)(from.rgba[c] + rounded);
            }
            tv_put(canvas, region.x + x, region.y + y, colour);
        }
    }
    return true;
}

/* Interpolates a tiling lattice of random numbers at one pixel. The result is from -256 to 256. */
static int32_t tv_noise_at(const int16_t *lattice, uint32_t lattice_w, uint32_t lattice_h, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height) {
    const uint32_t fine_x = x * lattice_w * 256u / width;
    const uint32_t fine_y = y * lattice_h * 256u / height;
    const uint32_t left = (fine_x >> 8) % lattice_w;
    const uint32_t top = (fine_y >> 8) % lattice_h;
    const uint32_t right = (left + 1u) % lattice_w;
    const uint32_t bottom = (top + 1u) % lattice_h;
    const int32_t weight_x = (int32_t)(fine_x & 255u);
    const int32_t weight_y = (int32_t)(fine_y & 255u);
    const int32_t upper =
        lattice[top * lattice_w + left] * (256 - weight_x) + lattice[top * lattice_w + right] * weight_x;
    const int32_t lower =
        lattice[bottom * lattice_w + left] * (256 - weight_x) + lattice[bottom * lattice_w + right] * weight_x;
    return (upper * (256 - weight_y) + lower * weight_y) / 65536;
}

static const char *const tv_keys_noise[] = {"amount", "cell", "cell_x", "cell_y", "seed", "x", "y", "w", "h", nullptr};
static bool tv_op_noise(tv_canvas_t *canvas, const yaml_node_t *op) {
    int64_t amount = 0;
    int64_t cell = 0;
    int64_t cell_x = 0;
    int64_t cell_y = 0;
    tv_region_t region;
    uint64_t state = 0;
    if (!tv_need_int(canvas, op, "amount", 0, TV_RECIPE_MAX_AMOUNT, &amount) ||
        !tv_want_int(canvas, op, "cell", 1, TV_RECIPE_MAX_RADIUS, 1, &cell) ||
        !tv_want_int(canvas, op, "cell_x", 1, TV_RECIPE_MAX_RADIUS, cell, &cell_x) ||
        !tv_want_int(canvas, op, "cell_y", 1, TV_RECIPE_MAX_RADIUS, cell, &cell_y) ||
        !tv_read_region(canvas, op, &region) || !tv_read_seed(canvas, op, &state)) {
        return false;
    }
    /* The lattice spans the whole canvas, in a whole number of cells, so the result tiles. */
    const uint32_t lattice_w = (uint32_t)((canvas->width + (uint64_t)cell_x / 2u) / (uint64_t)cell_x);
    const uint32_t lattice_h = (uint32_t)((canvas->height + (uint64_t)cell_y / 2u) / (uint64_t)cell_y);
    const uint32_t cells_w = lattice_w == 0 ? 1u : lattice_w;
    const uint32_t cells_h = lattice_h == 0 ? 1u : lattice_h;
    const uint64_t mark = canvas->arena->used;
    int16_t *lattice = tp_arena_alloc(canvas->arena, (uint64_t)cells_w * cells_h * sizeof(int16_t));
    for (uint64_t i = 0; i < (uint64_t)cells_w * cells_h; i++) {
        lattice[i] = (int16_t)((int32_t)tv_random_below(&state, 513u) - 256);
    }
    for (uint32_t y = region.y; y < region.y + region.height; y++) {
        for (uint32_t x = region.x; x < region.x + region.width; x++) {
            uint8_t *pixel = tv_at(canvas, x, y);
            if (pixel[3] != 0) {
                const int32_t change =
                    tv_noise_at(lattice, cells_w, cells_h, x, y, canvas->width, canvas->height) * (int32_t)amount / 256;
                pixel[0] = tv_clamp_byte(pixel[0] + change);
                pixel[1] = tv_clamp_byte(pixel[1] + change);
                pixel[2] = tv_clamp_byte(pixel[2] + change);
            }
        }
    }
    canvas->arena->used = mark;
    return true;
}

static const char *const tv_keys_speckle[] = {"color", "permille", "seed", "visible", "x", "y", "w", "h", nullptr};
static bool tv_op_speckle(tv_canvas_t *canvas, const yaml_node_t *op) {
    tv_colour_bytes_t colour;
    int64_t permille = 0;
    tv_region_t region;
    uint64_t state = 0;
    bool visible_only = false;
    if (!tv_need_colour(canvas, op, "color", &colour) || !tv_need_int(canvas, op, "permille", 0, 1000, &permille) ||
        !tv_want_bool(canvas, op, "visible", &visible_only) || !tv_read_region(canvas, op, &region) ||
        !tv_read_seed(canvas, op, &state)) {
        return false;
    }
    for (uint32_t y = region.y; y < region.y + region.height; y++) {
        for (uint32_t x = region.x; x < region.x + region.width; x++) {
            const bool chosen = (int64_t)tv_random_below(&state, 1000u) < permille;
            if (chosen && (!visible_only || tv_at(canvas, x, y)[3] != 0)) {
                tv_put(canvas, x, y, colour);
            }
        }
    }
    return true;
}

static const char *const tv_keys_blobs[] = {"color",   "count", "radius_min", "radius_max", "jitter", "seed",
                                            "visible", "x",     "y",          "w",          "h",      nullptr};
/* Rough round patches, wrapping inside the region: ore clusters, moss, stones in gravel. */
static bool tv_op_blobs(tv_canvas_t *canvas, const yaml_node_t *op) {
    tv_colour_bytes_t colour;
    int64_t count = 0;
    int64_t radius_min = 0;
    int64_t radius_max = 0;
    int64_t jitter = 0;
    tv_region_t region;
    uint64_t state = 0;
    bool visible_only = false;
    if (!tv_want_bool(canvas, op, "visible", &visible_only) || !tv_need_colour(canvas, op, "color", &colour) ||
        !tv_need_int(canvas, op, "count", 0, TV_RECIPE_MAX_COUNT, &count) ||
        !tv_need_int(canvas, op, "radius_min", 0, TV_RECIPE_MAX_RADIUS, &radius_min) ||
        !tv_need_int(canvas, op, "radius_max", radius_min, TV_RECIPE_MAX_RADIUS, &radius_max) ||
        !tv_want_int(canvas, op, "jitter", 0, TV_RECIPE_MAX_AMOUNT, 0, &jitter) ||
        !tv_read_region(canvas, op, &region) || !tv_read_seed(canvas, op, &state)) {
        return false;
    }
    for (int64_t i = 0; i < count; i++) {
        const int64_t cx = tv_random_below(&state, region.width);
        const int64_t cy = tv_random_below(&state, region.height);
        const int64_t r = radius_min + tv_random_below(&state, (uint32_t)(radius_max - radius_min + 1));
        for (int64_t dy = -r; dy <= r; dy++) {
            for (int64_t dx = -r; dx <= r; dx++) {
                /* A little random slack on the circle's test, so that the edge is rough. Always draws from the generator. */
                const int64_t slack = tv_random_below(&state, (uint32_t)r + 1u);
                const int32_t shade = (int32_t)tv_random_below(&state, (uint32_t)jitter * 2u + 1u) - (int32_t)jitter;
                if (dx * dx + dy * dy > r * r + slack - r / 2) {
                    continue;
                }
                const int64_t w = region.width;
                const int64_t h = region.height;
                tv_colour_bytes_t shaded = colour;
                for (uint32_t c = 0; c < 3u; c++) {
                    shaded.rgba[c] = tv_clamp_byte(colour.rgba[c] + shade);
                }
                const uint32_t at_x = region.x + (uint32_t)(((cx + dx) % w + w) % w);
                const uint32_t at_y = region.y + (uint32_t)(((cy + dy) % h + h) % h);
                if (!visible_only || tv_at(canvas, at_x, at_y)[3] != 0) {
                    tv_put(canvas, at_x, at_y, shaded);
                }
            }
        }
    }
    return true;
}

static const char *const tv_keys_bricks[] = {"color", "brick_w", "brick_h", "offset", "x", "y", "w", "h", nullptr};
/* Only the mortar: a running bond. Each brick is brick_w by brick_h, counting one line of mortar at its top and left. */
static bool tv_op_bricks(tv_canvas_t *canvas, const yaml_node_t *op) {
    tv_colour_bytes_t colour;
    int64_t brick_w = 0;
    int64_t brick_h = 0;
    int64_t offset = 0;
    tv_region_t region;
    if (!tv_need_colour(canvas, op, "color", &colour) ||
        !tv_need_int(canvas, op, "brick_w", 1, TV_RECIPE_MAX_RADIUS, &brick_w) ||
        !tv_need_int(canvas, op, "brick_h", 1, TV_RECIPE_MAX_RADIUS, &brick_h) ||
        !tv_want_int(canvas, op, "offset", 0, TV_RECIPE_MAX_RADIUS, brick_w / 2, &offset) ||
        !tv_read_region(canvas, op, &region)) {
        return false;
    }
    for (uint32_t y = 0; y < region.height; y++) {
        const uint32_t row = y / (uint32_t)brick_h;
        for (uint32_t x = 0; x < region.width; x++) {
            const uint32_t shifted = x + (row % 2u == 1u ? (uint32_t)offset : 0u);
            if (y % (uint32_t)brick_h == 0 || shifted % (uint32_t)brick_w == 0) {
                tv_put(canvas, region.x + x, region.y + y, colour);
            }
        }
    }
    return true;
}

static const char *const tv_keys_tint[] = {"color", "x", "y", "w", "h", nullptr};
static bool tv_op_tint(tv_canvas_t *canvas, const yaml_node_t *op) {
    tv_colour_bytes_t colour;
    tv_region_t region;
    if (!tv_need_colour(canvas, op, "color", &colour) || !tv_read_region(canvas, op, &region)) {
        return false;
    }
    for (uint32_t y = region.y; y < region.y + region.height; y++) {
        for (uint32_t x = region.x; x < region.x + region.width; x++) {
            uint8_t *pixel = tv_at(canvas, x, y);
            for (uint32_t c = 0; c < 3u; c++) {
                pixel[c] = (uint8_t)(((uint32_t)pixel[c] * colour.rgba[c] + 127u) / 255u);
            }
        }
    }
    return true;
}

static const char *const tv_keys_copy[] = {"x", "y", "w", "h", "to_x", "to_y", "flip", nullptr};
static bool tv_op_copy(tv_canvas_t *canvas, const yaml_node_t *op) {
    tv_region_t region;
    int64_t to_x = 0;
    int64_t to_y = 0;
    if (!tv_read_region(canvas, op, &region) || !tv_need_int(canvas, op, "to_x", 0, canvas->width - 1, &to_x) ||
        !tv_need_int(canvas, op, "to_y", 0, canvas->height - 1, &to_y)) {
        return false;
    }
    if (to_x + region.width > canvas->width || to_y + region.height > canvas->height) {
        return yaml_fail_at_node(op, canvas->error, "the copy does not fit the %ux%u canvas", canvas->width,
                                 canvas->height);
    }
    const yaml_node_t *flip = yaml_map_get(canvas->document, op, "flip");
    if (flip != nullptr && !yaml_text_equals(flip, "x") && !yaml_text_equals(flip, "y")) {
        return yaml_fail_at_node(flip, canvas->error, "flip is x or y");
    }
    const bool flip_x = flip != nullptr && yaml_text_equals(flip, "x");
    const bool flip_y = flip != nullptr && yaml_text_equals(flip, "y");
    /* Through a scratch copy, so that the source and target may overlap. */
    const uint64_t mark = canvas->arena->used;
    uint8_t *scratch = tp_arena_alloc(canvas->arena, (uint64_t)region.width * region.height * 4u);
    for (uint32_t y = 0; y < region.height; y++) {
        memcpy(scratch + (uint64_t)y * region.width * 4u, tv_at(canvas, region.x, region.y + y),
               (uint64_t)region.width * 4u);
    }
    for (uint32_t y = 0; y < region.height; y++) {
        for (uint32_t x = 0; x < region.width; x++) {
            const uint32_t from_x = flip_x ? region.width - 1u - x : x;
            const uint32_t from_y = flip_y ? region.height - 1u - y : y;
            memcpy(tv_at(canvas, (uint32_t)to_x + x, (uint32_t)to_y + y),
                   scratch + ((uint64_t)from_y * region.width + from_x) * 4u, 4u);
        }
    }
    canvas->arena->used = mark;
    return true;
}

static const char *const tv_keys_art[] = {"x", "y", "palette", "rows", nullptr};
/* Pixel art: rows of text, one character a pixel, each named in palette. A '.' leaves the pixel alone. */
static bool tv_op_art(tv_canvas_t *canvas, const yaml_node_t *op) {
    int64_t at_x = 0;
    int64_t at_y = 0;
    if (!tv_want_int(canvas, op, "x", 0, canvas->width - 1, 0, &at_x) ||
        !tv_want_int(canvas, op, "y", 0, canvas->height - 1, 0, &at_y)) {
        return false;
    }
    const yaml_node_t *palette = yaml_map_get(canvas->document, op, "palette");
    const yaml_node_t *rows = yaml_map_get(canvas->document, op, "rows");
    if (palette == nullptr || palette->kind != YAML_KIND_MAP || rows == nullptr || rows->kind != YAML_KIND_SEQ) {
        return yaml_fail_at_node(op, canvas->error, "art needs a palette mapping and a rows sequence");
    }
    tv_colour_bytes_t colours[256];
    uint8_t known[256];
    memset(known, 0, sizeof(known));
    for (const yaml_node_t *entry = yaml_first_child(canvas->document, palette); entry != nullptr;
         entry = yaml_next_sibling(canvas->document, entry)) {
        if (entry->key_length != 1u || entry->borrowed_key[0] == '.') {
            return yaml_fail_at_node(entry, canvas->error, "a palette key is one character, and not a dot");
        }
        if (!tv_read_colour_node(canvas, entry, &colours[entry->borrowed_key[0]])) {
            return false;
        }
        known[entry->borrowed_key[0]] = 1;
    }
    uint32_t row_index = 0;
    for (const yaml_node_t *row = yaml_first_child(canvas->document, rows); row != nullptr;
         row = yaml_next_sibling(canvas->document, row)) {
        const uint8_t *text = nullptr;
        uint32_t length = 0;
        if (!yaml_get_text(row, &text, &length, canvas->error)) {
            return false;
        }
        if ((uint64_t)at_y + row_index >= canvas->height || (uint64_t)at_x + length > canvas->width) {
            return yaml_fail_at_node(row, canvas->error, "this row does not fit the %ux%u canvas", canvas->width,
                                     canvas->height);
        }
        for (uint32_t i = 0; i < length; i++) {
            if (text[i] == '.') {
                continue;
            }
            if (known[text[i]] == 0) {
                return yaml_fail_at_node(row, canvas->error, "character %u is not in the palette", i + 1u);
            }
            tv_put(canvas, (uint32_t)at_x + i, (uint32_t)at_y + row_index, colours[text[i]]);
        }
        row_index++;
    }
    return true;
}

/* ---- The recipe ------------------------------------------------------------------------------------------------- */

static const tv_op_row_t tv_op_rows[] = {
    {"fill", tv_op_fill, tv_keys_fill},
    {"outline", tv_op_outline, tv_keys_outline},
    {"line", tv_op_line, tv_keys_line},
    {"disc", tv_op_disc, tv_keys_disc},
    {"gradient", tv_op_gradient, tv_keys_gradient},
    {"noise", tv_op_noise, tv_keys_noise},
    {"speckle", tv_op_speckle, tv_keys_speckle},
    {"blobs", tv_op_blobs, tv_keys_blobs},
    {"bricks", tv_op_bricks, tv_keys_bricks},
    {"tint", tv_op_tint, tv_keys_tint},
    {"copy", tv_op_copy, tv_keys_copy},
    {"art", tv_op_art, tv_keys_art},
};
enum { TV_OP_ROW_COUNT = sizeof(tv_op_rows) / sizeof(tv_op_rows[0]) };

static bool tv_key_is(const yaml_node_t *node, const char *name) {
    return node->borrowed_key != nullptr && strlen(name) == node->key_length &&
           memcmp(name, node->borrowed_key, node->key_length) == 0;
}

static const tv_op_row_t *tv_op_find(const tv_canvas_t *canvas, const yaml_node_t *op) {
    if (op->kind != YAML_KIND_MAP) {
        yaml_fail_at_node(op, canvas->error, "each operation must be a mapping");
        return nullptr;
    }
    const yaml_node_t *name = yaml_map_get(canvas->document, op, "op");
    if (name == nullptr) {
        yaml_fail_at_node(op, canvas->error, "needs op, the name of an operation");
        return nullptr;
    }
    const tv_op_row_t *row = nullptr;
    for (uint32_t i = 0; i < TV_OP_ROW_COUNT && row == nullptr; i++) {
        if (yaml_text_equals(name, tv_op_rows[i].name)) {
            row = &tv_op_rows[i];
        }
    }
    if (row == nullptr) {
        yaml_fail_at_node(name, canvas->error, "no such operation");
        return nullptr;
    }
    for (const yaml_node_t *key = yaml_first_child(canvas->document, op); key != nullptr;
         key = yaml_next_sibling(canvas->document, key)) {
        bool is_known = tv_key_is(key, "op");
        for (const char *const *known = row->keys; *known != nullptr && !is_known; known++) {
            is_known = tv_key_is(key, *known);
        }
        if (!is_known) {
            yaml_fail_at_node(key, canvas->error, "%s has no such key", row->name);
            return nullptr;
        }
    }
    return row;
}

bool tv_recipe_run(tp_arena_t *arena, const yaml_document_t *document, const yaml_node_t *source, uint32_t width,
                   uint32_t height, uint32_t frames, uint8_t *out_pixels, yaml_error_t *error) {
    for (const yaml_node_t *key = yaml_first_child(document, source); key != nullptr;
         key = yaml_next_sibling(document, key)) {
        if (!tv_key_is(key, "kind") && !tv_key_is(key, "about") && !tv_key_is(key, "seed") && !tv_key_is(key, "ops")) {
            return yaml_fail_at_node(key, error, "a recipe has kind, about, seed and ops");
        }
    }
    const yaml_node_t *seed_node = yaml_map_get(document, source, "seed");
    const yaml_node_t *ops = yaml_map_get(document, source, "ops");
    if (seed_node == nullptr || ops == nullptr || ops->kind != YAML_KIND_SEQ || ops->child_count == 0) {
        return yaml_fail_at_node(source, error, "a recipe needs a seed, and a sequence of ops");
    }
    if (ops->child_count > TV_RECIPE_MAX_OPS) {
        return yaml_fail_at_node(ops, error, "over %u ops", TV_RECIPE_MAX_OPS);
    }
    int64_t seed = 0;
    if (!yaml_get_int64(seed_node, &seed, error)) {
        return false;
    }
    if (seed < 0) {
        return yaml_fail_at_node(seed_node, error, "a seed is not negative");
    }
    const uint64_t frame_bytes = (uint64_t)width * height * 4u;
    memset(out_pixels, 0, frame_bytes * frames);
    for (uint32_t frame = 0; frame < frames; frame++) {
        tv_canvas_t canvas = {.arena = arena,
                              .document = document,
                              .error = error,
                              .pixels = out_pixels + frame_bytes * frame,
                              .seed = 0,
                              .width = width,
                              .height = height};
        uint32_t position = 0;
        for (const yaml_node_t *op = yaml_first_child(document, ops); op != nullptr;
             op = yaml_next_sibling(document, op)) {
            const tv_op_row_t *row = tv_op_find(&canvas, op);
            if (row == nullptr) {
                return false;
            }
            canvas.seed = tv_seed_mix(tv_seed_mix((uint64_t)seed, frame), position);
            if (!row->function(&canvas, op)) {
                return false;
            }
            position++;
        }
    }
    return true;
}
