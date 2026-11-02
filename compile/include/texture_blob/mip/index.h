/* Mip chains for the texture blob. See compile/src/texture_blob/mip/README.md. Integer arithmetic only, so the same bytes on any machine. */
#ifndef TEXTURE_BLOB_MIP_INDEX_H
#define TEXTURE_BLOB_MIP_INDEX_H

#include "types/texture_blob/index.h"

#include <stdint.h>

/* One texture's level 0. */
typedef struct tb_mip_source_t {
    const uint8_t *borrowed_pixels; /* 0: RGBA8, not premultiplied, sRGB, frame 0 first */
    uint32_t width;                 /* 8: of one frame */
    uint32_t height;                /* 12 */
    uint32_t frame_count;           /* 16 */
    uint8_t alpha_class;            /* 20: TEXTURE_BLOB_ALPHA_* */
    uint8_t hole_rgb[3];            /* 21: the colour of a texel with nothing visible under it */
} tb_mip_source_t;
static_assert(sizeof(tb_mip_source_t) == 24);

/* The bytes of every level of every frame, and each level's offset if out_level_offset is not nullptr. */
uint64_t tb_mip_chain_bytes(uint32_t width, uint32_t height, uint32_t frame_count,
                            uint32_t out_level_offset[TEXTURE_BLOB_MAX_LEVELS]);

/* Writes the chain, tb_mip_chain_bytes long, in the blob's order. */
void tb_mip_build(const tb_mip_source_t *source, uint8_t *out_chain);

#endif
