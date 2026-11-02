/*
 * Derives one texture's vector data from its pixels: palette, mean colour, height, normal, emissive and alpha class. See
 * compile/src/texture_vector/analyze/README.md. Integer arithmetic only, so it gives the same bytes on any machine.
 */
#ifndef TEXTURE_VECTOR_ANALYZE_INDEX_H
#define TEXTURE_VECTOR_ANALYZE_INDEX_H

#include "texture_pack/support/index.h"
#include "texture_vector/index.h"
#include "texture_vector/mcvec/index.h"
#include "texture_vector/palette/index.h"

#include <stdint.h>

#define TV_ALPHA_FROM_PIXELS 0xFFu
/* How steep a luma difference looks in the normal map. 1 means a difference of 255 per pixel is 45 degrees. */
#define TV_NORMAL_STRENGTH 3
/* A texture whose visible luma spans less than this is flat: stretching it would only amplify a few levels of noise. */
#define TV_HEIGHT_MIN_SPAN 8u
#define TV_HEIGHT_FLAT 128u

/* What pixels cannot say about themselves, from textures/vector.yaml. All zero, with alpha_class as above, is "nothing". */
typedef struct tv_material_t {
    uint8_t emissive_level; /* 0: 0 to 15 */
    uint8_t min_luma;       /* 1: only pixels at least this bright give light */
    uint8_t warm;           /* 2: and, if 1, only pixels clearly redder than they are blue, as flames are */
    uint8_t alpha_class;    /* 3: an mcvec_alpha_class_t, or TV_ALPHA_FROM_PIXELS to derive it */
} tv_material_t;
static_assert(sizeof(tv_material_t) == 4);

typedef struct tv_vector_t {
    tv_palette_t palette;     /* 0 */
    uint8_t *indices;         /* 1032: the rest are from the arena, one entry per pixel of every frame */
    uint8_t *height;          /* 1040 */
    uint8_t *normal;          /* 1048: two bytes per pixel */
    uint8_t *emissive;        /* 1056 */
    uint64_t pixel_count;     /* 1064 */
    uint32_t visible_pixels;  /* 1072 */
    uint32_t emissive_pixels; /* 1076 */
    uint8_t mean_rgba[4];     /* 1080 */
    uint8_t alpha_class;      /* 1084 */
    uint8_t emissive_class;   /* 1085 */
    uint8_t emissive_level;   /* 1086 */
    uint8_t luma_min;         /* 1087 */
    uint8_t luma_max;         /* 1088 */
    uint8_t luma_mean;        /* 1089 */
    uint8_t _pad0[6];         /* 1090 */
} tv_vector_t;
static_assert(sizeof(tv_vector_t) == 1096);

/* Rec. 709 luma of sRGB bytes, 0 to 255. */
uint8_t tv_luma(uint8_t red, uint8_t green, uint8_t blue);
/* An sRGB byte in linear light, 0 to 65535. */
uint16_t tv_linear_from_srgb(uint8_t value);
/* The sRGB byte whose linear value is nearest. */
uint8_t tv_srgb_from_linear(uint16_t value);

/* Fills out. Everything it points to comes from arena, and lasts until the caller resets it. */
void tv_analyze(tp_arena_t *arena, const tv_image_t *image, tv_material_t material, tv_vector_t *out);

#endif
