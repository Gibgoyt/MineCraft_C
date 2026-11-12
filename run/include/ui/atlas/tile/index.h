/* See run/src/ui/atlas/tile/README.md. */
#ifndef MC_RUN_UI_ATLAS_TILE_INDEX_H
#define MC_RUN_UI_ATLAS_TILE_INDEX_H

#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* Plan 004 phase 09, plan 007 phase 02: what the Atlas knows of the ground, one uint32_t a pixel at every level:
 *   bits 0 to 15   RGB565 colour of the top block;
 *   bits 16 to 26  the top's height from sea level + 256, 0 to 1535 (-256 to 1279); 2047 means not generated;
 *   bits 27 to 31  cover depth d, 0 to 31: how far the top stands above the ground under trees and snow, so the ground is
 *                  z - d. */
enum : uint32_t {
    UI_ATLAS_TILE_SIDE = 512u,                                      /* pixels each way, at every level */
    UI_ATLAS_TILE_PIXELS = UI_ATLAS_TILE_SIDE * UI_ATLAS_TILE_SIDE, /* 262144 */
    UI_ATLAS_TILE_BYTES = UI_ATLAS_TILE_PIXELS * 4u,                /* 1 MiB */
    UI_ATLAS_LEVELS = 8u,                                           /* 0 is a block a pixel; 7 is 128 blocks */
    UI_ATLAS_COLUMN_SIDE = 16u,                                     /* a column's blocks each way */
    UI_ATLAS_COLUMN_PIXELS = 256u,                                  /* a column's 16 by 16 */
    UI_ATLAS_HEIGHT_NONE_CODE = 2047u,                              /* bits 16 to 26 of a pixel not generated */
    UI_ATLAS_COVER_MAX = 31u,                                       /* deeper cover is stored as 31 */
    UI_ATLAS_PIXEL_NONE = UI_ATLAS_HEIGHT_NONE_CODE << 16,          /* not generated: 0x07FF0000 */
    UI_ATLAS_FILE_VERSION = 2u,                                     /* what encode writes */
    UI_ATLAS_FILE_VERSION_OLDEST = 1u, /* what decode still reads: 16-bit heights, no cover (plan 004 phase 09) */
    UI_ATLAS_ENCODING_RAW = 0u,        /* UI_ATLAS_TILE_PIXELS uint32_t, row 0 first */
    UI_ATLAS_ENCODING_RUNS = 1u,       /* ui_atlas_run_t, until all UI_ATLAS_TILE_PIXELS are covered */
    UI_ATLAS_FILE_NAME_BYTES = 64u,
};
enum : int32_t {
    UI_ATLAS_HEIGHT_MIN = -256, /* COLUMN_Z_STORAGE_MIN - MC_WORLD_Z_SEA */
    UI_ATLAS_HEIGHT_MAX = 1279, /* the top storage layer */
};
static_assert(UI_ATLAS_TILE_SIDE % UI_ATLAS_COLUMN_SIDE == 0u); /* a column never straddles two tiles */
static_assert(UI_ATLAS_HEIGHT_MAX - UI_ATLAS_HEIGHT_MIN < (int32_t)UI_ATLAS_HEIGHT_NONE_CODE);

/* Packs a generated pixel. z is held to -256 to 1279 and cover to 0 to 31. */
static inline uint32_t ui_atlas_pixel_pack_cover(uint16_t rgb565, int32_t z, uint32_t cover) {
    const int32_t held =
        z < UI_ATLAS_HEIGHT_MIN ? UI_ATLAS_HEIGHT_MIN : (z > UI_ATLAS_HEIGHT_MAX ? UI_ATLAS_HEIGHT_MAX : z);
    const uint32_t d = cover > UI_ATLAS_COVER_MAX ? UI_ATLAS_COVER_MAX : cover;
    return (uint32_t)rgb565 | (uint32_t)(held - UI_ATLAS_HEIGHT_MIN) << 16 | d << 27;
}
/* A generated pixel with no cover. */
static inline uint32_t ui_atlas_pixel_pack(uint16_t rgb565, int32_t z) {
    return ui_atlas_pixel_pack_cover(rgb565, z, 0u);
}
static inline bool ui_atlas_pixel_generated(uint32_t pixel) {
    return ((pixel >> 16) & 0x7FFu) != UI_ATLAS_HEIGHT_NONE_CODE;
}
/* The top's height from sea level, tree crowns included: what the colour and the hill shading show. Only for a generated
 * pixel. */
static inline int16_t ui_atlas_pixel_z(uint32_t pixel) {
    return (int16_t)((int32_t)((pixel >> 16) & 0x7FFu) + UI_ATLAS_HEIGHT_MIN);
}
/* How far the top stands above the ground: 0 for bare ground, water and stone. */
static inline uint32_t ui_atlas_pixel_cover(uint32_t pixel) {
    return pixel >> 27;
}
/* The ground under the cover, from sea level: what contour lines follow. Only for a generated pixel. */
static inline int16_t ui_atlas_pixel_ground_z(uint32_t pixel) {
    return (int16_t)(ui_atlas_pixel_z(pixel) - (int32_t)ui_atlas_pixel_cover(pixel));
}
static inline uint16_t ui_atlas_pixel_rgb565(uint32_t pixel) {
    return (uint16_t)(pixel & 0xFFFFu);
}
/* A version-1 pixel (RGB565, then the height as int16_t, INT16_MIN not generated) in today's layout, with cover 0. */
static inline uint32_t ui_atlas_pixel_from_version_1(uint32_t old) {
    const int16_t z = (int16_t)(uint16_t)(old >> 16);
    return z == INT16_MIN ? UI_ATLAS_PIXEL_NONE : ui_atlas_pixel_pack((uint16_t)(old & 0xFFFFu), z);
}
/* 8-bit channels to RGB565, each rounded to its nearest step. */
uint16_t ui_atlas_rgb565_of(uint32_t rgb);
/* RGB565 to 0x00RRGGBB, each channel spread over 0 to 255 so 31 and 63 are 255. */
uint32_t ui_atlas_rgb_of(uint16_t rgb565);

/* At the start of every .tile file. All little-endian. */
typedef struct ui_atlas_file_header_t {
    uint8_t magic[8];          /* 0: "MCATLAST" */
    uint64_t seed;             /* 8: the world's */
    uint64_t payload_checksum; /* 16: texture_blob_hash (FNV-1a 64) of what follows the header */
    int32_t tile_x;            /* 24 */
    int32_t tile_y;            /* 28 */
    uint32_t version;          /* 32: UI_ATLAS_FILE_VERSION; decode reads UI_ATLAS_FILE_VERSION_OLDEST too */
    uint32_t header_bytes;     /* 36: sizeof this, 64 */
    uint32_t payload_bytes;    /* 40 */
    uint8_t level;             /* 44 */
    uint8_t encoding;          /* 45: UI_ATLAS_ENCODING_* */
    uint8_t _pad0[2];          /* 46 */
    uint64_t checksum;         /* 48: texture_blob_hash of the 48 bytes before this */
    uint8_t _pad1[8];          /* 56 */
} ui_atlas_file_header_t;
static_assert(sizeof(ui_atlas_file_header_t) == 64);
static_assert(alignof(ui_atlas_file_header_t) == 8);
static_assert(offsetof(ui_atlas_file_header_t, magic) == 0);
static_assert(offsetof(ui_atlas_file_header_t, seed) == 8);
static_assert(offsetof(ui_atlas_file_header_t, payload_checksum) == 16);
static_assert(offsetof(ui_atlas_file_header_t, tile_x) == 24);
static_assert(offsetof(ui_atlas_file_header_t, tile_y) == 28);
static_assert(offsetof(ui_atlas_file_header_t, version) == 32);
static_assert(offsetof(ui_atlas_file_header_t, header_bytes) == 36);
static_assert(offsetof(ui_atlas_file_header_t, payload_bytes) == 40);
static_assert(offsetof(ui_atlas_file_header_t, level) == 44);
static_assert(offsetof(ui_atlas_file_header_t, encoding) == 45);
static_assert(offsetof(ui_atlas_file_header_t, _pad0) == 46);
static_assert(offsetof(ui_atlas_file_header_t, checksum) == 48);
static_assert(offsetof(ui_atlas_file_header_t, _pad1) == 56);

/* A run of one pixel value, in row order. */
typedef struct ui_atlas_run_t {
    uint32_t pixel; /* 0 */
    uint32_t count; /* 4: at least 1 */
} ui_atlas_run_t;
static_assert(sizeof(ui_atlas_run_t) == 8);
static_assert(alignof(ui_atlas_run_t) == 4);
static_assert(offsetof(ui_atlas_run_t, pixel) == 0);
static_assert(offsetof(ui_atlas_run_t, count) == 4);

enum : uint64_t {
    UI_ATLAS_FILE_BYTES_MAX = sizeof(ui_atlas_file_header_t) + (uint64_t)UI_ATLAS_TILE_BYTES, /* raw is never beaten */
};

/* Floor division of a block coordinate by a level-L tile's side, 512 x 2^L blocks: the tile holding it. */
int32_t ui_atlas_tile_of(int64_t block, uint32_t level);

/* The level-L pixel holding a block: its column x / 2^L and its row (-1 - y) / 2^L, both floored, so row 0 of every tile is
 * its north edge and rows grow southward, as in a picture. */
int64_t ui_atlas_pixel_column_of(int64_t x, uint32_t level);
int64_t ui_atlas_pixel_row_of(int64_t y, uint32_t level);
/* The tile holding a level pixel column or row, and the pixel's place inside it, 0 to 511. */
int32_t ui_atlas_tile_x_of_column(int64_t column);
int32_t ui_atlas_tile_y_of_row(int64_t row);
uint32_t ui_atlas_in_tile(int64_t column_or_row);

/* A column's top, worked out on the column job's worker: for each of its 256 (x, y), walking down from the top section, the
 * first voxel whose block's shape is not none (air), cross, crop or a torch, packed with its height from sea level. states
 * is the whole column, COLUMN_SECTIONS sections lowest first, each in section_voxel_index order. colours is RGB565 by block
 * id, colour_count long. out is 256 pixels, row 0 north, x east. A column with nothing that counts gives its floor in black.
 * Pure. */
void ui_atlas_column_top(const uint16_t *colours, uint32_t colour_count, const uint16_t *states, uint32_t *out);

/* Copies a column's 256 pixels (row 0 north) into its place in the level-0 tile that holds it, which must be
 * (ui_atlas_tile_of(chunk_x * 16, 0), ui_atlas_tile_of(chunk_y * 16, 0)). Pure. */
void ui_atlas_tile_column_put(uint32_t *tile, int32_t chunk_x, int32_t chunk_y, const uint32_t *column);

/* One level-L tile halved into the quarter of its level-(L+1) parent it covers: quarter bit 0 set for the east half (an odd
 * tile x), bit 1 for the north half (an odd tile y). Each parent pixel is the rounded mean of the generated pixels of its 2
 * by 2 children, channel by channel and height, (sum + floor(n / 2)) / n floored; not generated only when none of the four
 * is. A child nullptr is a child that exists nowhere: its quarter becomes UI_ATLAS_PIXEL_NONE. Pure. */
void ui_atlas_tile_halve(const uint32_t *child, uint32_t quarter, uint32_t *parent);

/* The 2 x 2 pixels' parent, as ui_atlas_tile_halve makes it. Pure. */
uint32_t ui_atlas_pixel_mean(const uint32_t pixels[4]);

/* A tile's file: header then payload, runs where they are smaller than raw. Returns the bytes written, at most
 * UI_ATLAS_FILE_BYTES_MAX; out must hold that. Pure. */
uint64_t ui_atlas_tile_encode(const uint32_t *tile, uint64_t seed, uint32_t level, int32_t tile_x, int32_t tile_y,
                              uint8_t *out);

/* Why a file was refused, or UI_ATLAS_DECODE_OK. */
typedef enum ui_atlas_decode_t : uint8_t {
    UI_ATLAS_DECODE_OK = 0,
    UI_ATLAS_DECODE_SHORT = 1,   /* fewer bytes than the header says */
    UI_ATLAS_DECODE_MAGIC = 2,   /* not "MCATLAST" */
    UI_ATLAS_DECODE_HEADER = 3,  /* the header's own checksum, size, version, encoding or padding */
    UI_ATLAS_DECODE_FOREIGN = 4, /* another seed, level or position */
    UI_ATLAS_DECODE_PAYLOAD = 5, /* the payload's checksum, or its size for its encoding */
    UI_ATLAS_DECODE_RUNS = 6,    /* runs that do not cover the tile exactly, or a run of 0 */
} ui_atlas_decode_t;

/* Reads a file made by ui_atlas_tile_encode back into a tile, checking every field. Anything but OK leaves out unknown,
 * and the caller treats the tile as never saved. Pure. */
ui_atlas_decode_t ui_atlas_tile_decode(const uint8_t *bytes, uint64_t length, uint64_t seed, uint32_t level,
                                       int32_t tile_x, int32_t tile_y, uint32_t *out);
const char *ui_atlas_decode_name(ui_atlas_decode_t status);

/* "L<level>_<tile_x>_<tile_y>.tile", signed decimal, into out (UI_ATLAS_FILE_NAME_BYTES). */
void ui_atlas_tile_file_name(uint32_t level, int32_t tile_x, int32_t tile_y, char *out);

#endif
