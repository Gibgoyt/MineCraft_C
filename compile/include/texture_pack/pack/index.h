/*
 * Everything that looks at the untrusted pack: its pinned commit, its licence, whether the manifest accounts for every file
 * in it, and decoding one texture. See compile/src/texture_pack/pack/README.md.
 */
#ifndef TEXTURE_PACK_PACK_INDEX_H
#define TEXTURE_PACK_PACK_INDEX_H

#include "texture_pack/manifest/index.h"
#include "texture_pack/support/index.h"

#include <stdint.h>

/* A decoded texture, from the arena it was loaded with. Frames are stacked: height is one frame's. */
typedef struct tp_image_t {
    uint8_t *borrowed_pixels; /* 0: RGBA8 */
    uint64_t pixel_bytes;     /* 8 */
    uint32_t width;           /* 16 */
    uint32_t height;          /* 20: of one frame */
    uint32_t frames;          /* 24 */
    uint8_t _pad0[4];         /* 28 */
} tp_image_t;
static_assert(sizeof(tp_image_t) == 32);

/* What tp_pack_check_coverage counted. */
typedef struct tp_coverage_t {
    uint32_t files;    /* 0: every file in the pack but git's own */
    uint32_t imported; /* 4 */
    uint32_t skipped;  /* 8 */
    uint32_t evidence; /* 12 */
} tp_coverage_t;
static_assert(sizeof(tp_coverage_t) == 16);

/* Reads the clone's own .git/HEAD, through refs or packed-refs, and requires exactly the manifest's commit. */
bool tp_pack_check_commit(tp_arena_t *arena, const char *pack, const tp_manifest_t *manifest);

/* Requires every evidence text in the pack, and that texture_pack.conf, if it states a licence, states this one. */
bool tp_pack_check_licence(tp_arena_t *arena, const char *pack, const tp_manifest_t *manifest);

/*
 * Walks the whole pack. Fails on any file the manifest does not name, on any listed file that is gone, on any file name
 * with bytes we would not repeat, and on a licence-like file no one has reviewed.
 */
bool tp_pack_check_coverage(tp_arena_t *arena, const char *pack, const tp_manifest_t *manifest, tp_coverage_t *out);

/* One PNG from the pack, decoded to RGBA8 as a single frame, under the same limits as an entry. From arena. */
bool tp_pack_decode_image(tp_arena_t *arena, const char *pack, const char *file, tp_image_t *out);
/* top over a fully opaque base of the same size, in place, rounded the same way on any machine. */
void tp_pack_composite_over_opaque(uint8_t *top, const uint8_t *base, uint64_t pixel_bytes);
/* Reads, decodes, splits and composites one entry. Everything comes from arena. */
bool tp_pack_load_entry(tp_arena_t *arena, const char *pack, const tp_entry_t *entry, tp_image_t *out);

#endif
