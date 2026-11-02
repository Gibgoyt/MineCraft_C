/*
 * The texture blob's format: what compile/src/texture_blob writes and run/src/texture/blob reads. It is here because
 * plans/REPO_STRUCTURE.md section 11 lets compile/ include from run/ only under types/, and one definition cannot drift. See
 * run/include/types/texture_blob/README.md.
 *
 * In file order, little-endian, starting on a 64-byte boundary, with zeros in every gap:
 *   header    one texture_blob_header_t
 *   entries   texture_count * texture_blob_entry_t, strictly ascending by name, so that an id is a position
 *   names     NUL-terminated text, such as "blocks/dirt"
 *   pixels    on a 64-byte boundary. Per texture, on a 32-byte boundary: level 0's frames, then level 1's, and so on. RGBA8,
 *             not premultiplied, sRGB, each frame top row first, with no gap between rows. Alpha obeys alpha_class, below
 *   vectors   on a 32-byte boundary. Per texture, on a 32-byte boundary, what .mcvec holds, N = width * height * frame_count:
 *             palette (palette_count * 4), indices (N), height (N), normal (N * 2), emissive (N)
 */
#ifndef MC_RUN_TYPES_TEXTURE_BLOB_INDEX_H
#define MC_RUN_TYPES_TEXTURE_BLOB_INDEX_H

#include <stddef.h>
#include <stdint.h>

#define TEXTURE_BLOB_MAGIC 0x4254434Du /* "MCTB" in file order */
#define TEXTURE_BLOB_VERSION 1u
#define TEXTURE_BLOB_ALIGNMENT 64u
#define TEXTURE_BLOB_TEXTURE_ALIGNMENT 32u
#define TEXTURE_BLOB_MAX_DIMENSION 4096u
#define TEXTURE_BLOB_MAX_FRAMES 256u
#define TEXTURE_BLOB_MAX_LEVELS 13u /* 4096 halves 12 times to reach 1 */
#define TEXTURE_BLOB_MAX_TEXTURES 65535u
#define TEXTURE_BLOB_MAX_PALETTE 256u
#define TEXTURE_BLOB_MAX_EMISSIVE 15u

/* The promise each class makes about the alpha of every level, so a shader may test alpha == 0 and never need a threshold. */
#define TEXTURE_BLOB_ALPHA_OPAQUE 0u      /* always 255 */
#define TEXTURE_BLOB_ALPHA_CUTOUT 1u      /* always 0 or 255: rays pass or stop, with no blending */
#define TEXTURE_BLOB_ALPHA_TRANSLUCENT 2u /* any value, and it blends */
#define TEXTURE_BLOB_ALPHA_CLASS_COUNT 3u
/* Level 0 of a cutout texture: at or above this is solid. The pack's leaves are 5 where a leaf is absent. */
#define TEXTURE_BLOB_CUTOUT_THRESHOLD 128u

#define TEXTURE_BLOB_EMISSIVE_NONE 0u
#define TEXTURE_BLOB_EMISSIVE_PARTIAL 1u
#define TEXTURE_BLOB_EMISSIVE_FULL 2u
#define TEXTURE_BLOB_EMISSIVE_CLASS_COUNT 3u

typedef struct texture_blob_header_t {
    uint32_t magic;          /* 0 */
    uint32_t version;        /* 4 */
    uint32_t texture_count;  /* 8 */
    uint32_t name_bytes;     /* 12: of the names section */
    uint64_t total_bytes;    /* 16: of the whole blob */
    uint64_t payload_hash;   /* 24: FNV-1a 64 of everything after this header */
    uint64_t entries_offset; /* 32: this and the next three are from the start of the blob */
    uint64_t names_offset;   /* 40 */
    uint64_t pixels_offset;  /* 48 */
    uint64_t vectors_offset; /* 56 */
    uint64_t pixels_bytes;   /* 64: everything a GPU upload needs is this one run of bytes */
    uint64_t vectors_bytes;  /* 72 */
} texture_blob_header_t;
static_assert(sizeof(texture_blob_header_t) == 80);
static_assert(alignof(texture_blob_header_t) == 8);
static_assert(offsetof(texture_blob_header_t, magic) == 0);
static_assert(offsetof(texture_blob_header_t, version) == 4);
static_assert(offsetof(texture_blob_header_t, texture_count) == 8);
static_assert(offsetof(texture_blob_header_t, name_bytes) == 12);
static_assert(offsetof(texture_blob_header_t, total_bytes) == 16);
static_assert(offsetof(texture_blob_header_t, payload_hash) == 24);
static_assert(offsetof(texture_blob_header_t, entries_offset) == 32);
static_assert(offsetof(texture_blob_header_t, names_offset) == 40);
static_assert(offsetof(texture_blob_header_t, pixels_offset) == 48);
static_assert(offsetof(texture_blob_header_t, vectors_offset) == 56);
static_assert(offsetof(texture_blob_header_t, pixels_bytes) == 64);
static_assert(offsetof(texture_blob_header_t, vectors_bytes) == 72);

typedef struct texture_blob_entry_t {
    uint64_t name_hash;     /* 0: FNV-1a 64 of the name, without its NUL */
    uint64_t source_hash;   /* 8: the pixel_hash of the .mctex this was made from */
    uint64_t pixels_offset; /* 16: from the start of the pixels section */
    uint64_t pixels_bytes;  /* 24: every level of every frame */
    uint64_t vector_offset; /* 32: from the start of the vectors section */
    uint64_t vector_bytes;  /* 40 */
    uint32_t
        level_offset[TEXTURE_BLOB_MAX_LEVELS]; /* 48: from pixels_offset to each level's frame 0; 0 past level_count */
    uint32_t name_offset;                      /* 100: into the names section */
    uint32_t width;                            /* 104: of level 0 */
    uint32_t height;                           /* 108 */
    uint32_t frame_count;                      /* 112 */
    uint32_t palette_count;                    /* 116: 1 to 256 */
    uint32_t visible_pixels;                   /* 120: alpha above 0, in all frames, before alpha_class is applied */
    uint32_t emissive_pixels;                  /* 124: emissive above 0, in all frames */
    uint16_t name_length;                      /* 128: without its NUL */
    uint8_t level_count;                       /* 130 */
    uint8_t alpha_class;                       /* 131: TEXTURE_BLOB_ALPHA_* */
    uint8_t emissive_class;                    /* 132: TEXTURE_BLOB_EMISSIVE_* */
    uint8_t emissive_level;                    /* 133: 0 to 15 */
    uint8_t palette_exact;                     /* 134: 1 if every pixel is exactly its palette entry */
    uint8_t luma_min;                          /* 135: of visible pixels */
    uint8_t luma_max;                          /* 136 */
    uint8_t luma_mean;                         /* 137 */
    uint8_t mean_rgba[4];                      /* 138: for LOD, from .mcvec */
    uint8_t _pad0[2];                          /* 142: zero */
} texture_blob_entry_t;
static_assert(sizeof(texture_blob_entry_t) == 144);
static_assert(alignof(texture_blob_entry_t) == 8);
static_assert(offsetof(texture_blob_entry_t, name_hash) == 0);
static_assert(offsetof(texture_blob_entry_t, source_hash) == 8);
static_assert(offsetof(texture_blob_entry_t, pixels_offset) == 16);
static_assert(offsetof(texture_blob_entry_t, pixels_bytes) == 24);
static_assert(offsetof(texture_blob_entry_t, vector_offset) == 32);
static_assert(offsetof(texture_blob_entry_t, vector_bytes) == 40);
static_assert(offsetof(texture_blob_entry_t, level_offset) == 48);
static_assert(offsetof(texture_blob_entry_t, name_offset) == 100);
static_assert(offsetof(texture_blob_entry_t, width) == 104);
static_assert(offsetof(texture_blob_entry_t, height) == 108);
static_assert(offsetof(texture_blob_entry_t, frame_count) == 112);
static_assert(offsetof(texture_blob_entry_t, palette_count) == 116);
static_assert(offsetof(texture_blob_entry_t, visible_pixels) == 120);
static_assert(offsetof(texture_blob_entry_t, emissive_pixels) == 124);
static_assert(offsetof(texture_blob_entry_t, name_length) == 128);
static_assert(offsetof(texture_blob_entry_t, level_count) == 130);
static_assert(offsetof(texture_blob_entry_t, alpha_class) == 131);
static_assert(offsetof(texture_blob_entry_t, emissive_class) == 132);
static_assert(offsetof(texture_blob_entry_t, emissive_level) == 133);
static_assert(offsetof(texture_blob_entry_t, palette_exact) == 134);
static_assert(offsetof(texture_blob_entry_t, luma_min) == 135);
static_assert(offsetof(texture_blob_entry_t, luma_max) == 136);
static_assert(offsetof(texture_blob_entry_t, luma_mean) == 137);
static_assert(offsetof(texture_blob_entry_t, mean_rgba) == 138);
static_assert(offsetof(texture_blob_entry_t, _pad0) == 142);

/* Each level halves, rounding down, and stops at 1: 165 by 170 is 82 by 85, then 41 by 42, ... The writer and the reader
 * both use these, so that they cannot disagree on where a level starts. */
static inline uint32_t texture_blob_level_dimension(uint32_t dimension, uint32_t level) {
    const uint32_t reduced = dimension >> level;
    return reduced == 0u ? 1u : reduced;
}

static inline uint32_t texture_blob_level_count(uint32_t width, uint32_t height) {
    uint32_t largest = width > height ? width : height;
    uint32_t count = 1u;
    while (largest > 1u) {
        largest >>= 1;
        count++;
    }
    return count;
}

/* One frame of one level. Width and height are at most 4096, so this cannot overflow. */
static inline uint64_t texture_blob_level_frame_bytes(uint32_t width, uint32_t height, uint32_t level) {
    return (uint64_t)texture_blob_level_dimension(width, level) * texture_blob_level_dimension(height, level) * 4u;
}

static inline uint64_t texture_blob_align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

#endif
