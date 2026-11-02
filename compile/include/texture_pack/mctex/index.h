/*
 * The .mctex format: one translated texture, as textures/<GROUP>/<NAME>/index.mctex. See
 * compile/src/texture_pack/mctex/README.md. Stage 06.3's texture_vector reads it through mctex_parse.
 *
 * A 32-byte header, then width * height * frame_count * 4 bytes of RGBA8: not premultiplied, frame 0 first, each frame top
 * row first, with no gap between rows. Little-endian, as x86-64 is. Nothing else, and no bytes after the pixels.
 */
#ifndef TEXTURE_PACK_MCTEX_INDEX_H
#define TEXTURE_PACK_MCTEX_INDEX_H

#include <stddef.h>
#include <stdint.h>

#define MCTEX_MAGIC 0x5854434Du /* "MCTX" in file order */
#define MCTEX_VERSION 1u
#define MCTEX_MAX_DIMENSION 4096u
#define MCTEX_MAX_FRAMES 256u
#define MCTEX_MAX_PIXEL_BYTES (64ull * 1024ull * 1024ull)

typedef struct mctex_header_t {
    uint32_t magic;       /* 0 */
    uint32_t version;     /* 4 */
    uint32_t width;       /* 8: of one frame */
    uint32_t height;      /* 12: of one frame */
    uint32_t frame_count; /* 16 */
    uint32_t _pad0;       /* 20: zero */
    uint64_t pixel_hash;  /* 24: FNV-1a 64 of the pixel bytes */
} mctex_header_t;
static_assert(sizeof(mctex_header_t) == 32);
static_assert(alignof(mctex_header_t) == 8);
static_assert(offsetof(mctex_header_t, magic) == 0);
static_assert(offsetof(mctex_header_t, version) == 4);
static_assert(offsetof(mctex_header_t, width) == 8);
static_assert(offsetof(mctex_header_t, height) == 12);
static_assert(offsetof(mctex_header_t, frame_count) == 16);
static_assert(offsetof(mctex_header_t, _pad0) == 20);
static_assert(offsetof(mctex_header_t, pixel_hash) == 24);

/* A parsed file. Borrows the file's bytes, and must not outlive them. borrowed_pixels has no alignment promise. */
typedef struct mctex_view_t {
    const uint8_t *borrowed_pixels; /* 0 */
    uint64_t pixel_bytes;           /* 8 */
    uint32_t width;                 /* 16 */
    uint32_t height;                /* 20 */
    uint32_t frame_count;           /* 24 */
    uint8_t _pad0[4];               /* 28 */
} mctex_view_t;
static_assert(sizeof(mctex_view_t) == 32);

typedef enum mctex_status_t : uint8_t {
    MCTEX_OK = 0,
    MCTEX_TRUNCATED = 1, /* shorter than a header, or than its pixels */
    MCTEX_BAD_MAGIC = 2,
    MCTEX_BAD_VERSION = 3,
    MCTEX_BAD_SIZE = 4, /* a zero dimension or frame count, or one over the limits above */
    MCTEX_BAD_PADDING = 5,
    MCTEX_TRAILING = 6, /* bytes after the pixels */
    MCTEX_BAD_HASH = 7, /* the pixels are not what was written */
    MCTEX_NO_ROOM = 8,  /* mctex_encode's buffer is too small, or its pixel length is wrong */
} mctex_status_t;

/* FNV-1a 64. Written out here because a build-time format must not depend on a library's choice. */
uint64_t mctex_hash(const uint8_t *bytes, uint64_t length);

/* The pixel bytes of these dimensions, or false if they are zero or over the limits. */
bool mctex_pixel_bytes(uint32_t width, uint32_t height, uint32_t frame_count, uint64_t *out_bytes);

/* Writes a whole file to out, which needs sizeof(mctex_header_t) + pixel bytes. Takes no alignment. */
mctex_status_t mctex_encode(uint8_t *out, uint64_t capacity, uint32_t width, uint32_t height, uint32_t frame_count,
                            const uint8_t *pixels, uint64_t pixel_bytes);

/* Checks every field and the hash. On failure returns the status and empties view. */
mctex_status_t mctex_parse(const uint8_t *file, uint64_t length, mctex_view_t *view);

/* One sentence about a status. Never nullptr, for any value. */
const char *mctex_status_text(mctex_status_t status);

#endif
