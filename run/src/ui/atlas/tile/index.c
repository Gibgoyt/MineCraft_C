/* See README.md. Pure functions on buffers the caller gives: no allocation, no locks, no disk. */
#include "ui/atlas/tile/index.h"

#include "machine/index.h"
#include "simd/bitmask/index.h"
#include "tables/blocks/index.h"
#include "texture/blob/index.h"
#include "world/column/index.h"
#include "world/section/index.h"

#include <inttypes.h>
#include <stdbit.h>
#include <stdio.h>
#include <string.h>

static const uint8_t ATLAS_TILE_MAGIC[8] = {'M', 'C', 'A', 'T', 'L', 'A', 'S', 'T'};

uint16_t ui_atlas_rgb565_of(uint32_t rgb) {
    const uint32_t r = (((rgb >> 16) & 0xFFu) * 31u + 127u) / 255u;
    const uint32_t g = (((rgb >> 8) & 0xFFu) * 63u + 127u) / 255u;
    const uint32_t b = ((rgb & 0xFFu) * 31u + 127u) / 255u;
    return (uint16_t)(r << 11 | g << 5 | b);
}

uint32_t ui_atlas_rgb_of(uint16_t rgb565) {
    const uint32_t r = ((uint32_t)(rgb565 >> 11) & 31u) * 255u / 31u;
    const uint32_t g = ((uint32_t)(rgb565 >> 5) & 63u) * 255u / 63u;
    const uint32_t b = ((uint32_t)rgb565 & 31u) * 255u / 31u;
    return r << 16 | g << 8 | b;
}

/* a / b rounded toward minus infinity, b > 0. */
static int64_t tile_floor_div(int64_t a, int64_t b) {
    const int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

int32_t ui_atlas_tile_of(int64_t block, uint32_t level) {
    return (int32_t)tile_floor_div(block, (int64_t)UI_ATLAS_TILE_SIDE << level);
}

int64_t ui_atlas_pixel_column_of(int64_t x, uint32_t level) {
    return tile_floor_div(x, (int64_t)1 << level);
}

int64_t ui_atlas_pixel_row_of(int64_t y, uint32_t level) {
    return tile_floor_div(-1 - y, (int64_t)1 << level);
}

int32_t ui_atlas_tile_x_of_column(int64_t column) {
    return (int32_t)tile_floor_div(column, UI_ATLAS_TILE_SIDE);
}

int32_t ui_atlas_tile_y_of_row(int64_t row) {
    /* Row 0 of tile ty is global row -(ty + 1) x 512. */
    return (int32_t)(-tile_floor_div(row, UI_ATLAS_TILE_SIDE) - 1);
}

uint32_t ui_atlas_in_tile(int64_t column_or_row) {
    return (uint32_t)(column_or_row - tile_floor_div(column_or_row, UI_ATLAS_TILE_SIDE) * UI_ATLAS_TILE_SIDE);
}

/* Shapes that never hide the ground: air, plants on top of it, torches, and doors, which are seen from above as a sliver. */
static bool tile_counts(uint16_t state, uint32_t colour_count) {
    if (state >= BLOCK_COUNT || state >= colour_count) {
        return state != 0u; /* a block this program does not know is still something there */
    }
    switch (block_defs[state].shape) {
    case BLOCK_SHAPE_NONE:
    case BLOCK_SHAPE_CROSS:
    case BLOCK_SHAPE_CROP:
    case BLOCK_SHAPE_TORCH:
    case BLOCK_SHAPE_TORCH_WALL:
    case BLOCK_SHAPE_DOOR:
        return false;
    default:
        return true;
    }
}

/* What a block is to the column top, as bits: TILE_COUNTS if it hides the ground, TILE_COVER if the Atlas also sees through
 * it to the ground for its contour lines (leaves, logs, vines, snow layers, cactus: BLOCK_FLAG_MAP_COVER). */
enum : uint8_t {
    TILE_COUNTS = 1u,
    TILE_COVER = 2u,
};
static uint8_t tile_kind(uint16_t state, uint32_t colour_count) {
    const bool cover = state < BLOCK_COUNT && (block_defs[state].flags & BLOCK_FLAG_MAP_COVER) != 0u;
    return (uint8_t)((tile_counts(state, colour_count) ? TILE_COUNTS : 0u) | (cover ? TILE_COVER : 0u));
}

enum : uint32_t {
    TILE_SECTION_WORDS = SECTION_VOXELS / 64u, /* a section's voxels as bits, one a voxel */
    TILE_LAYER_WORDS = UI_ATLAS_COLUMN_PIXELS / 64u,
};
static_assert(SECTION_VOXELS % 64u == 0u && UI_ATLAS_COLUMN_PIXELS % 64u == 0u);

/* Where a column's (x, y) is on the way down. */
enum : uint8_t {
    TILE_SEEKING = 0u,  /* above its top */
    TILE_IN_COVER = 1u, /* its top is cover: looking for the ground below */
    TILE_DONE = 2u,
};

void ui_atlas_column_top(const uint16_t *colours, uint32_t colour_count, const uint16_t *states, uint32_t *out) {
    /* Section by section from the top, a whole z layer at a time, and it stops once all 256 have their top and, under
     * cover, their ground. Most of a column above the ground is air, state 0: each section is first made a bit a voxel,
     * set where it is not air (simd/bitmask's kernel, which reads it at the memory's pace), so an all-air section is passed
     * over after one look at its 64 words, and in the rest only the voxels that hold something are looked at. */
    uint8_t where[UI_ATLAS_COLUMN_PIXELS];
    int16_t top[UI_ATLAS_COLUMN_PIXELS];
    uint8_t kinds[BLOCK_COUNT]; /* by block, once a call: a table look in the loop, not a switch and a flag */
    for (uint32_t b = 0u; b < BLOCK_COUNT; b++) {
        kinds[b] = tile_kind((uint16_t)b, colour_count);
    }
    uint64_t solid[TILE_SECTION_WORDS];
    uint32_t left = UI_ATLAS_COLUMN_PIXELS;
    for (uint32_t k = 0u; k < UI_ATLAS_COLUMN_PIXELS; k++) {
        where[k] = TILE_SEEKING;
        top[k] = 0;
        out[k] = ui_atlas_pixel_pack(0u, COLUMN_Z_STORAGE_MIN - MC_WORLD_Z_SEA);
    }
    for (uint32_t section = COLUMN_SECTIONS; section-- > 0u && left > 0u;) {
        const uint16_t *const voxels = states + (uint64_t)section * SECTION_VOXELS;
        bitmask_from_nonzero_u16_batch(voxels, SECTION_VOXELS, solid);
        uint64_t any = 0u;
        for (uint32_t w = 0u; w < TILE_SECTION_WORDS; w++) {
            any |= solid[w];
        }
        if (any == 0u) {
            continue;
        }
        for (uint32_t z = SECTION_SIZE; z-- > 0u && left > 0u;) {
            const uint16_t *const layer = voxels + section_voxel_index(0u, 0u, z);
            const int32_t height = (int32_t)(section * SECTION_SIZE + z) + COLUMN_Z_STORAGE_MIN - MC_WORLD_Z_SEA;
            for (uint32_t w = 0u; w < TILE_LAYER_WORDS; w++) {
                for (uint64_t bits = solid[z * TILE_LAYER_WORDS + w]; bits != 0u; bits &= bits - 1u) {
                    const uint32_t k = w * 64u + (uint32_t)stdc_trailing_zeros(bits); /* x fastest, then y */
                    if (where[k] == TILE_DONE) {
                        continue;
                    }
                    /* By the block alone: a saved column's voxels hold their state too. */
                    const uint16_t state = block_state_id(layer[k]);
                    const uint8_t kind = state < BLOCK_COUNT ? kinds[state] : tile_kind(state, colour_count);
                    if ((kind & TILE_COUNTS) == 0u) {
                        continue;
                    }
                    const bool cover = (kind & TILE_COVER) != 0u;
                    if (where[k] == TILE_IN_COVER && cover && (int32_t)top[k] - height < (int32_t)UI_ATLAS_COVER_MAX) {
                        continue; /* the common case under a canopy: still cover */
                    }
                    const uint32_t x = k % UI_ATLAS_COLUMN_SIDE, y = k / UI_ATLAS_COLUMN_SIDE;
                    const uint32_t at = (UI_ATLAS_COLUMN_SIDE - 1u - y) * UI_ATLAS_COLUMN_SIDE + x;
                    if (where[k] == TILE_SEEKING) {
                        const uint16_t colour = state < colour_count ? colours[state] : 0u;
                        out[at] = ui_atlas_pixel_pack(colour, height);
                        top[k] = (int16_t)height;
                        where[k] = cover ? TILE_IN_COVER : TILE_DONE;
                        left -= cover ? 0u : 1u;
                        continue;
                    }
                    /* Under cover: the first block that counts and is not cover is the ground, at most 31 down. */
                    const int32_t depth = (int32_t)top[k] - height;
                    out[at] = ui_atlas_pixel_pack_cover(ui_atlas_pixel_rgb565(out[at]), top[k], (uint32_t)depth);
                    where[k] = TILE_DONE;
                    left--;
                }
            }
        }
    }
    /* Cover with nothing under it to the floor: as deep as it may be. */
    for (uint32_t k = 0u; k < UI_ATLAS_COLUMN_PIXELS; k++) {
        if (where[k] == TILE_IN_COVER) {
            const uint32_t at = (UI_ATLAS_COLUMN_SIDE - 1u - k / UI_ATLAS_COLUMN_SIDE) * UI_ATLAS_COLUMN_SIDE +
                                k % UI_ATLAS_COLUMN_SIDE;
            out[at] = ui_atlas_pixel_pack_cover(ui_atlas_pixel_rgb565(out[at]), top[k], UI_ATLAS_COVER_MAX);
        }
    }
}

void ui_atlas_tile_column_put(uint32_t *tile, int32_t chunk_x, int32_t chunk_y, const uint32_t *column) {
    const int64_t west = (int64_t)chunk_x * UI_ATLAS_COLUMN_SIDE;
    const int64_t north = (int64_t)chunk_y * UI_ATLAS_COLUMN_SIDE + UI_ATLAS_COLUMN_SIDE - 1;
    const uint32_t i = ui_atlas_in_tile(ui_atlas_pixel_column_of(west, 0u));
    const uint32_t j = ui_atlas_in_tile(ui_atlas_pixel_row_of(north, 0u));
    for (uint32_t row = 0u; row < UI_ATLAS_COLUMN_SIDE; row++) {
        memcpy(tile + (uint64_t)(j + row) * UI_ATLAS_TILE_SIDE + i, column + (uint64_t)row * UI_ATLAS_COLUMN_SIDE,
               UI_ATLAS_COLUMN_SIDE * sizeof(uint32_t));
    }
}

/* (sum + floor(n / 2)) / n, floored: the same rounding for negative heights as for positive ones. */
static int32_t tile_mean(int32_t sum, int32_t n) {
    return (int32_t)tile_floor_div((int64_t)sum + n / 2, n);
}

uint32_t ui_atlas_pixel_mean(const uint32_t pixels[4]) {
    int32_t r = 0, g = 0, b = 0, z = 0, d = 0, n = 0;
    for (uint32_t k = 0u; k < 4u; k++) {
        if (!ui_atlas_pixel_generated(pixels[k])) {
            continue;
        }
        const uint16_t c = ui_atlas_pixel_rgb565(pixels[k]);
        r += (int32_t)(c >> 11);
        g += (int32_t)((c >> 5) & 63u);
        b += (int32_t)(c & 31u);
        z += (int32_t)ui_atlas_pixel_z(pixels[k]);
        d += (int32_t)ui_atlas_pixel_cover(pixels[k]);
        n++;
    }
    if (n == 0) {
        return UI_ATLAS_PIXEL_NONE;
    }
    const uint32_t colour =
        (uint32_t)tile_mean(r, n) << 11 | (uint32_t)tile_mean(g, n) << 5 | (uint32_t)tile_mean(b, n);
    return ui_atlas_pixel_pack_cover((uint16_t)colour, tile_mean(z, n), (uint32_t)tile_mean(d, n));
}

void ui_atlas_tile_halve(const uint32_t *child, uint32_t quarter, uint32_t *parent) {
    const uint32_t half = UI_ATLAS_TILE_SIDE / 2u;
    const uint32_t column0 = (quarter & 1u) != 0u ? half : 0u;
    const uint32_t row0 = (quarter & 2u) != 0u ? 0u : half; /* north is up: an odd tile y is the parent's north half */
    for (uint32_t j = 0u; j < half; j++) {
        uint32_t *const out = parent + (uint64_t)(row0 + j) * UI_ATLAS_TILE_SIDE + column0;
        if (child == nullptr) {
            for (uint32_t i = 0u; i < half; i++) {
                out[i] = UI_ATLAS_PIXEL_NONE;
            }
            continue;
        }
        const uint32_t *const top = child + (uint64_t)(2u * j) * UI_ATLAS_TILE_SIDE;
        const uint32_t *const bottom = top + UI_ATLAS_TILE_SIDE;
        for (uint32_t i = 0u; i < half; i++) {
            const uint32_t four[4] = {top[2u * i], top[2u * i + 1u], bottom[2u * i], bottom[2u * i + 1u]};
            out[i] = ui_atlas_pixel_mean(four);
        }
    }
}

static void tile_put_u32(uint8_t *at, uint32_t value) {
    for (uint32_t k = 0u; k < 4u; k++) {
        at[k] = (uint8_t)(value >> (8u * k));
    }
}

static uint32_t tile_get_u32(const uint8_t *at) {
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

uint64_t ui_atlas_tile_encode(const uint32_t *tile, uint64_t seed, uint32_t level, int32_t tile_x, int32_t tile_y,
                              uint8_t *out) {
    uint64_t runs = 0u;
    for (uint32_t k = 0u; k < UI_ATLAS_TILE_PIXELS; k++) {
        runs += (k == 0u || tile[k] != tile[k - 1u]) ? 1u : 0u;
    }
    const uint64_t run_bytes = runs * sizeof(ui_atlas_run_t);
    const bool use_runs = run_bytes < (uint64_t)UI_ATLAS_TILE_BYTES;
    uint8_t *const payload = out + sizeof(ui_atlas_file_header_t);
    uint64_t length = 0u;
    if (use_runs) {
        uint32_t k = 0u;
        while (k < UI_ATLAS_TILE_PIXELS) {
            uint32_t count = 1u;
            while (k + count < UI_ATLAS_TILE_PIXELS && tile[k + count] == tile[k]) {
                count++;
            }
            tile_put_u32(payload + length, tile[k]);
            tile_put_u32(payload + length + 4u, count);
            length += sizeof(ui_atlas_run_t);
            k += count;
        }
    } else {
        for (uint32_t k = 0u; k < UI_ATLAS_TILE_PIXELS; k++) {
            tile_put_u32(payload + length, tile[k]);
            length += 4u;
        }
    }
    ui_atlas_file_header_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, ATLAS_TILE_MAGIC, sizeof(header.magic));
    header.seed = seed;
    header.payload_checksum = texture_blob_hash(payload, length);
    header.tile_x = tile_x;
    header.tile_y = tile_y;
    header.version = UI_ATLAS_FILE_VERSION;
    header.header_bytes = (uint32_t)sizeof(header);
    header.payload_bytes = (uint32_t)length;
    header.level = (uint8_t)level;
    header.encoding = (uint8_t)(use_runs ? UI_ATLAS_ENCODING_RUNS : UI_ATLAS_ENCODING_RAW);
    header.checksum = texture_blob_hash((const uint8_t *)&header, offsetof(ui_atlas_file_header_t, checksum));
    memcpy(out, &header, sizeof(header));
    return sizeof(header) + length;
}

ui_atlas_decode_t ui_atlas_tile_decode(const uint8_t *bytes, uint64_t length, uint64_t seed, uint32_t level,
                                       int32_t tile_x, int32_t tile_y, uint32_t *out) {
    ui_atlas_file_header_t header;
    if (length < sizeof(header)) {
        return UI_ATLAS_DECODE_SHORT;
    }
    memcpy(&header, bytes, sizeof(header));
    if (memcmp(header.magic, ATLAS_TILE_MAGIC, sizeof(header.magic)) != 0) {
        return UI_ATLAS_DECODE_MAGIC;
    }
    const uint64_t sum = texture_blob_hash(bytes, offsetof(ui_atlas_file_header_t, checksum));
    const bool padding_clear = header._pad0[0] == 0u && header._pad0[1] == 0u &&
                               memcmp(header._pad1, (const uint8_t[8]){0}, sizeof(header._pad1)) == 0;
    const bool known = header.version >= UI_ATLAS_FILE_VERSION_OLDEST && header.version <= UI_ATLAS_FILE_VERSION;
    if (header.checksum != sum || !known || header.header_bytes != sizeof(header) ||
        header.encoding > UI_ATLAS_ENCODING_RUNS || !padding_clear) {
        return UI_ATLAS_DECODE_HEADER;
    }
    if (header.seed != seed || header.level != level || header.tile_x != tile_x || header.tile_y != tile_y) {
        return UI_ATLAS_DECODE_FOREIGN;
    }
    if (length - sizeof(header) != header.payload_bytes) {
        return length - sizeof(header) < header.payload_bytes ? UI_ATLAS_DECODE_SHORT : UI_ATLAS_DECODE_PAYLOAD;
    }
    const uint8_t *const payload = bytes + sizeof(header);
    if (texture_blob_hash(payload, header.payload_bytes) != header.payload_checksum) {
        return UI_ATLAS_DECODE_PAYLOAD;
    }
    if (header.encoding == UI_ATLAS_ENCODING_RAW) {
        if (header.payload_bytes != UI_ATLAS_TILE_BYTES) {
            return UI_ATLAS_DECODE_PAYLOAD;
        }
        for (uint32_t k = 0u; k < UI_ATLAS_TILE_PIXELS; k++) {
            const uint32_t pixel = tile_get_u32(payload + 4u * (uint64_t)k);
            out[k] = header.version == 1u ? ui_atlas_pixel_from_version_1(pixel) : pixel;
        }
        return UI_ATLAS_DECODE_OK;
    }
    if (header.payload_bytes % sizeof(ui_atlas_run_t) != 0u) {
        return UI_ATLAS_DECODE_PAYLOAD;
    }
    uint64_t covered = 0u;
    for (uint64_t at = 0u; at < header.payload_bytes; at += sizeof(ui_atlas_run_t)) {
        const uint32_t stored = tile_get_u32(payload + at);
        const uint32_t pixel = header.version == 1u ? ui_atlas_pixel_from_version_1(stored) : stored;
        const uint32_t count = tile_get_u32(payload + at + 4u);
        if (count == 0u || covered + count > UI_ATLAS_TILE_PIXELS) {
            return UI_ATLAS_DECODE_RUNS;
        }
        for (uint32_t k = 0u; k < count; k++) {
            out[covered + k] = pixel;
        }
        covered += count;
    }
    return covered == UI_ATLAS_TILE_PIXELS ? UI_ATLAS_DECODE_OK : UI_ATLAS_DECODE_RUNS;
}

const char *ui_atlas_decode_name(ui_atlas_decode_t status) {
    switch (status) {
    case UI_ATLAS_DECODE_OK:
        return "ok";
    case UI_ATLAS_DECODE_SHORT:
        return "too short";
    case UI_ATLAS_DECODE_MAGIC:
        return "not an Atlas tile";
    case UI_ATLAS_DECODE_HEADER:
        return "a damaged header";
    case UI_ATLAS_DECODE_FOREIGN:
        return "another world's or another tile's";
    case UI_ATLAS_DECODE_PAYLOAD:
        return "a damaged payload";
    case UI_ATLAS_DECODE_RUNS:
        return "runs that do not cover the tile";
    }
    return "unknown";
}

void ui_atlas_tile_file_name(uint32_t level, int32_t tile_x, int32_t tile_y, char *out) {
    (void)snprintf(out, UI_ATLAS_FILE_NAME_BYTES, "L%" PRIu32 "_%" PRId32 "_%" PRId32 ".tile", level, tile_x, tile_y);
}
