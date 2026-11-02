/*
 * Writes what goes into textures/: each texture's three files, each group's README, and ATTRIBUTION.md. See
 * compile/src/texture_pack/output/README.md. The manifest's text and decoded pixels are the only inputs; no other byte of
 * the pack reaches a file.
 */
#ifndef TEXTURE_PACK_OUTPUT_INDEX_H
#define TEXTURE_PACK_OUTPUT_INDEX_H

#include "texture_pack/manifest/index.h"
#include "texture_pack/pack/index.h"
#include "texture_pack/support/index.h"

#include <stdint.h>

/* textures/<group>/<name>/{index.mctex,index.yaml,README.md}. Adds to out_written for each file that changed. */
bool tp_output_texture(tp_arena_t *arena, const char *textures, const tp_manifest_t *manifest, const tp_entry_t *entry,
                       const tp_image_t *image, uint32_t *out_written);

/* textures/<group>/README.md for every group, whether or not anything is imported into it. */
bool tp_output_groups(tp_arena_t *arena, const char *textures, uint32_t *out_written);

/* textures/ATTRIBUTION.md. */
bool tp_output_attribution(tp_arena_t *arena, const char *textures, const tp_manifest_t *manifest,
                           const tp_coverage_t *coverage, uint32_t *out_written);

/* The names of the groups, in order, for whoever walks the tree. */
uint32_t tp_output_group_count(void);
const char *tp_output_group_at(uint32_t position);

#endif
