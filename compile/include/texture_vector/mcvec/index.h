/*
 * The .mcvec format: the vector data of one texture, written to build/<preset>/generated/textures/ and never committed. See
 * compile/src/texture_vector/mcvec/README.md. Stage 06.4 reads it through mcvec_parse.
 *
 * A 64-byte header, then, with no gaps and N = width * height * frame_count:
 *   palette   palette_count * 4 bytes, RGBA8
 *   indices   N bytes, into the palette
 *   height    N bytes, 0 to 255
 *   normal    N * 2 bytes, int8_t x then y, each scaled by 127. z is positive, and is sqrt(1 - x * x - y * y)
 *   emissive  N bytes, a light level from 0 to 15
 * Frame 0 first, each frame top row first, as in .mctex. +x is right on the texture and +y is down it.
 */
#ifndef TEXTURE_VECTOR_MCVEC_INDEX_H
#define TEXTURE_VECTOR_MCVEC_INDEX_H

#include <stddef.h>
#include <stdint.h>

#define MCVEC_MAGIC 0x5654434Du /* "MCTV" in file order */
#define MCVEC_VERSION 1u
#define MCVEC_MAX_PALETTE 256u

typedef enum mcvec_alpha_class_t : uint8_t {
    MCVEC_ALPHA_OPAQUE = 0,      /* every pixel is alpha 255 */
    MCVEC_ALPHA_CUTOUT = 1,      /* every pixel is alpha 0 or 255: rays pass or stop, with no blending */
    MCVEC_ALPHA_TRANSLUCENT = 2, /* some pixel lies between, so it blends */
    MCVEC_ALPHA_CLASS_COUNT = 3,
} mcvec_alpha_class_t;

typedef enum mcvec_emissive_class_t : uint8_t {
    MCVEC_EMISSIVE_NONE = 0,    /* no pixel gives light */
    MCVEC_EMISSIVE_PARTIAL = 1, /* some visible pixels do, such as a torch's flame and not its stick */
    MCVEC_EMISSIVE_FULL = 2,    /* every visible pixel does */
    MCVEC_EMISSIVE_CLASS_COUNT = 3,
} mcvec_emissive_class_t;

typedef struct mcvec_header_t {
    uint32_t magic;         /* 0 */
    uint32_t version;       /* 4 */
    uint32_t width;         /* 8: of one frame */
    uint32_t height;        /* 12: of one frame */
    uint32_t frame_count;   /* 16 */
    uint32_t palette_count; /* 20: 1 to 256 */
    uint64_t source_hash;   /* 24: the pixel_hash of the .mctex this was made from */
    uint64_t payload_hash;  /* 32: FNV-1a 64 of everything after this header */
    uint8_t
        mean_rgba[4]; /* 40: for LOD. Mean in linear light weighted by alpha, back in sRGB; alpha is the plain mean */
    uint8_t alpha_class;      /* 44: an mcvec_alpha_class_t */
    uint8_t emissive_class;   /* 45: an mcvec_emissive_class_t */
    uint8_t emissive_level;   /* 46: the level of the pixels that give light, 0 to 15 */
    uint8_t palette_exact;    /* 47: 1 if every pixel is exactly its palette entry */
    uint8_t luma_min;         /* 48: of visible pixels, before height is stretched */
    uint8_t luma_max;         /* 49 */
    uint8_t luma_mean;        /* 50 */
    uint8_t _pad0;            /* 51: zero */
    uint32_t visible_pixels;  /* 52: alpha above 0, in all frames */
    uint32_t emissive_pixels; /* 56: emissive above 0, in all frames */
    uint32_t _pad1;           /* 60: zero */
} mcvec_header_t;
static_assert(sizeof(mcvec_header_t) == 64);
static_assert(alignof(mcvec_header_t) == 8);
static_assert(offsetof(mcvec_header_t, magic) == 0);
static_assert(offsetof(mcvec_header_t, version) == 4);
static_assert(offsetof(mcvec_header_t, width) == 8);
static_assert(offsetof(mcvec_header_t, height) == 12);
static_assert(offsetof(mcvec_header_t, frame_count) == 16);
static_assert(offsetof(mcvec_header_t, palette_count) == 20);
static_assert(offsetof(mcvec_header_t, source_hash) == 24);
static_assert(offsetof(mcvec_header_t, payload_hash) == 32);
static_assert(offsetof(mcvec_header_t, mean_rgba) == 40);
static_assert(offsetof(mcvec_header_t, alpha_class) == 44);
static_assert(offsetof(mcvec_header_t, emissive_class) == 45);
static_assert(offsetof(mcvec_header_t, emissive_level) == 46);
static_assert(offsetof(mcvec_header_t, palette_exact) == 47);
static_assert(offsetof(mcvec_header_t, luma_min) == 48);
static_assert(offsetof(mcvec_header_t, luma_max) == 49);
static_assert(offsetof(mcvec_header_t, luma_mean) == 50);
static_assert(offsetof(mcvec_header_t, _pad0) == 51);
static_assert(offsetof(mcvec_header_t, visible_pixels) == 52);
static_assert(offsetof(mcvec_header_t, emissive_pixels) == 56);
static_assert(offsetof(mcvec_header_t, _pad1) == 60);

/* A parsed file. Borrows the file's bytes, and must not outlive them. Every pointer has no alignment promise. */
typedef struct mcvec_view_t {
    mcvec_header_t header;            /* 0: a copy */
    const uint8_t *borrowed_palette;  /* 64 */
    const uint8_t *borrowed_indices;  /* 72 */
    const uint8_t *borrowed_height;   /* 80 */
    const uint8_t *borrowed_normal;   /* 88 */
    const uint8_t *borrowed_emissive; /* 96 */
    uint64_t pixel_count;             /* 104: N */
} mcvec_view_t;
static_assert(sizeof(mcvec_view_t) == 112);

typedef enum mcvec_status_t : uint8_t {
    MCVEC_OK = 0,
    MCVEC_TRUNCATED = 1, /* shorter than a header, or than its payload */
    MCVEC_BAD_MAGIC = 2,
    MCVEC_BAD_VERSION = 3,
    MCVEC_BAD_SIZE = 4, /* a dimension, frame count or palette count out of range */
    MCVEC_BAD_PADDING = 5,
    MCVEC_BAD_CLASS = 6, /* an unknown class, or an emissive level over 15, or one that disagrees with its class */
    MCVEC_TRAILING = 7,  /* bytes after the payload */
    MCVEC_BAD_HASH = 8,
    MCVEC_BAD_INDEX = 9, /* a pixel names a palette entry that does not exist */
    MCVEC_NO_ROOM = 10,  /* mcvec_encode's buffer is too small */
} mcvec_status_t;

/* The whole file's size for these counts, or false if they are out of range. */
bool mcvec_file_bytes(uint32_t width, uint32_t height, uint32_t frame_count, uint32_t palette_count,
                      uint64_t *out_bytes);

/*
 * Writes a whole file to out. Fills in the header's magic, version and payload_hash, and zeroes its padding, over what the
 * caller gave. The sections are copied in file order from the given pointers. Takes no alignment.
 */
mcvec_status_t mcvec_encode(uint8_t *out, uint64_t capacity, const mcvec_header_t *header, const uint8_t *palette,
                            const uint8_t *indices, const uint8_t *height, const uint8_t *normal,
                            const uint8_t *emissive);

/* Checks every field, the hash, and each index against the palette. On failure returns the status and empties view. */
mcvec_status_t mcvec_parse(const uint8_t *file, uint64_t length, mcvec_view_t *view);

/* One sentence about a status. Never nullptr, for any value. */
const char *mcvec_status_text(mcvec_status_t status);

#endif
