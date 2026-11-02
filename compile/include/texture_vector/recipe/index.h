/*
 * Procedural recipes: the source: mapping of a textures/<GROUP>/<NAME>/index.yaml whose kind is procedural. See
 * compile/src/texture_vector/recipe/README.md, which lists every operation. Integer arithmetic and our own generator, so a
 * recipe gives the same bytes on any machine, and reads nothing but its own yaml: no pack pixels, ever.
 */
#ifndef TEXTURE_VECTOR_RECIPE_INDEX_H
#define TEXTURE_VECTOR_RECIPE_INDEX_H

#include "texture_pack/support/index.h"
#include "yaml/index.h"

#include <stdint.h>

/*
 * Runs the recipe in source once per frame, into out_pixels, which holds width * height * frames * 4 bytes and is cleared
 * to transparent first. Scratch memory comes from arena and is given back. A bad recipe is a line-numbered error. error must
 * start zeroed, as yaml_error_t's always must.
 */
bool tv_recipe_run(tp_arena_t *arena, const yaml_document_t *document, const yaml_node_t *source, uint32_t width,
                   uint32_t height, uint32_t frames, uint8_t *out_pixels, yaml_error_t *error);

#endif
