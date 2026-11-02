/*
 * Checks a textures/ tree against the manifest, with no pack and no network. See
 * compile/src/texture_pack/verify/README.md. This is what proves the committed output is whole.
 */
#ifndef TEXTURE_PACK_VERIFY_INDEX_H
#define TEXTURE_PACK_VERIFY_INDEX_H

#include "texture_pack/manifest/index.h"
#include "texture_pack/support/index.h"

#include <stdint.h>

/* What tp_verify_tree counted. */
typedef struct tp_verify_counts_t {
    uint32_t imported; /* 0: directories that came from the pack */
    uint32_t other;    /* 4: directories with any other source, such as Stage 06.3's recipes */
} tp_verify_counts_t;
static_assert(sizeof(tp_verify_counts_t) == 8);

/* Prints every problem it finds, not just the first. True if there were none. */
bool tp_verify_tree(tp_arena_t *arena, const char *textures, const tp_manifest_t *manifest, tp_verify_counts_t *out);

#endif
