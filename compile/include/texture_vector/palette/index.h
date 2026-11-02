/*
 * The palette of one texture, and each pixel's index into it. See compile/src/texture_vector/palette/README.md. Exact when a
 * texture has at most TV_PALETTE_MAX colours, and a median cut when it has more.
 */
#ifndef TEXTURE_VECTOR_PALETTE_INDEX_H
#define TEXTURE_VECTOR_PALETTE_INDEX_H

#include "texture_pack/support/index.h"
#include "texture_vector/index.h"

#include <stdint.h>

#define TV_PALETTE_MAX 256u

typedef struct tv_palette_t {
    uint8_t rgba[TV_PALETTE_MAX * 4u]; /* 0: most used first, ties by colour value */
    uint32_t count;                    /* 1024: 1 to TV_PALETTE_MAX */
    uint8_t exact;                     /* 1028: 1 if each pixel is exactly its entry */
    uint8_t _pad0[3];                  /* 1029 */
} tv_palette_t;
static_assert(sizeof(tv_palette_t) == 1032);

/*
 * Fills out, and out_indices, which holds one byte per pixel of every frame. A pixel with alpha 0 counts as #00000000,
 * since its colour can never show. Scratch memory comes from arena and is given back. The same pixels always give the same
 * result: nothing depends on qsort's order for equal keys, because no two keys are equal.
 */
void tv_palette_build(tp_arena_t *arena, const tv_image_t *image, tv_palette_t *out, uint8_t *out_indices);

#endif
