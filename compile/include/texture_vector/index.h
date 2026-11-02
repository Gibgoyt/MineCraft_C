/*
 * The tool itself: procedural recipes -> textures/, and textures/ -> vector data per texture. Only the command line calls
 * it, so this header holds its exit statuses and the one type its parts share. See compile/src/texture_vector/README.md.
 */
#ifndef TEXTURE_VECTOR_INDEX_H
#define TEXTURE_VECTOR_INDEX_H

#include <stdint.h>

#define TEXTURE_VECTOR_EXIT_OK 0
#define TEXTURE_VECTOR_EXIT_REFUSED 1 /* a recipe, a table or the tree is wrong */
#define TEXTURE_VECTOR_EXIT_USAGE 2

/* Pixels to read: RGBA8, not premultiplied, frame 0 first, each frame top row first, as in compile/src/texture_pack/mctex/. */
typedef struct tv_image_t {
    const uint8_t *borrowed_pixels; /* 0 */
    uint64_t pixel_bytes;           /* 8: width * height * frames * 4 */
    uint32_t width;                 /* 16: of one frame */
    uint32_t height;                /* 20: of one frame */
    uint32_t frames;                /* 24 */
    uint8_t _pad0[4];               /* 28 */
} tv_image_t;
static_assert(sizeof(tv_image_t) == 32);

#endif
