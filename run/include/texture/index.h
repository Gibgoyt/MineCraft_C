/* See run/src/texture/README.md. */
#ifndef MC_RUN_TEXTURE_INDEX_H
#define MC_RUN_TEXTURE_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "texture/blob/index.h"
#include "texture/registry/index.h"

#include <stddef.h>
#include <stdint.h>

/* An id stays good for the life of the process, and in the next build only if the set of textures does not change. */
typedef struct texture_t {
    arena_t arena_tables;        /* 0: over owned_region */
    heap_region_t owned_region;  /* 64 */
    texture_blob_view_t blob;    /* 80 */
    texture_registry_t registry; /* 120 */
    uint8_t _pad0[40];           /* 152 */
} texture_t;
static_assert(sizeof(texture_t) == 192);
static_assert(alignof(texture_t) == 64);
static_assert(offsetof(texture_t, owned_region) == 64);
static_assert(offsetof(texture_t, blob) == 80);
static_assert(offsetof(texture_t, registry) == 120);
static_assert(offsetof(texture_t, _pad0) == 152);

/* What is known of one texture. Both last as long as the blob. */
typedef struct texture_info_t {
    const char *borrowed_name;                  /* 0: such as "blocks/dirt" */
    const texture_blob_entry_t *borrowed_entry; /* 8 */
} texture_info_t;
static_assert(sizeof(texture_info_t) == 16);

/* One frame of one mip level: RGBA8, not premultiplied, sRGB, top row first, with no gap between rows. 4-byte aligned. */
typedef struct texture_mip_t {
    const uint8_t *borrowed_pixels; /* 0 */
    uint64_t bytes;                 /* 8 */
    uint32_t width;                 /* 16 */
    uint32_t height;                /* 20 */
} texture_mip_t;
static_assert(sizeof(texture_mip_t) == 24);

/* The vector data of one frame, at full size only, with count = width * height. It has no mips: see README.md. */
typedef struct texture_vector_t {
    const uint8_t *borrowed_palette;  /* 0: palette_count * 4 bytes, RGBA8, and the same for every frame */
    const uint8_t *borrowed_indices;  /* 8: count bytes, into the palette */
    const uint8_t *borrowed_height;   /* 16: count bytes, 0 to 255 */
    const int8_t *borrowed_normal;    /* 24: count * 2: x then y, each scaled by 127. z is sqrt(1 - x * x - y * y) */
    const uint8_t *borrowed_emissive; /* 32: count bytes, a light level from 0 to 15 */
    uint64_t count;                   /* 40 */
} texture_vector_t;
static_assert(sizeof(texture_vector_t) == 48);

/* Checks the blob linked into this program and builds its lookup table. On failure logs why, leaves nothing behind and
 * returns false: a program whose own textures are damaged must not start. */
bool texture_init(texture_t *texture);

/* The same over any blob, which must outlive the texture_t. For tests. */
bool texture_init_from(texture_t *texture, const uint8_t *blob, uint64_t length);

/* Releases what init reserved. Safe on a zeroed texture_t. */
void texture_deinit(texture_t *texture);

/* name is "<group>/<name>", such as "blocks/dirt". False if there is no such texture. The rest take an id from here, and
 * return false for an id, level or frame that does not exist. Everything below only reads, so any thread may call it once
 * init has returned, and none of it allocates. */
bool texture_find(const texture_t *texture, const char *name, uint32_t *out_id);

/* Ids run from 0 to this less 1, in the names' order. */
uint32_t texture_count(const texture_t *texture);

bool texture_info_get(const texture_t *texture, uint32_t id, texture_info_t *out);
bool texture_mip_get(const texture_t *texture, uint32_t id, uint32_t level, uint32_t frame, texture_mip_t *out);
bool texture_vector_get(const texture_t *texture, uint32_t id, uint32_t frame, texture_vector_t *out);

/* All pixels of all textures, as one run of bytes on a 64-byte boundary, for one GPU upload. A texture's entry says where it
 * lies: pixels_offset, then level_offset. */
void texture_pixels_get(const texture_t *texture, const uint8_t **out_pixels, uint64_t *out_bytes);

#endif
