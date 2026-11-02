/*
 * textures/vector.yaml: what pixels cannot say (which textures give light, and which must blend), and every texture the game
 * needs. See compile/src/texture_vector/materials/README.md.
 */
#ifndef TEXTURE_VECTOR_MATERIALS_INDEX_H
#define TEXTURE_VECTOR_MATERIALS_INDEX_H

#include "texture_pack/support/index.h"
#include "texture_vector/analyze/index.h"

#include <stdint.h>

typedef struct tv_material_row_t {
    const char *texture;    /* 0: group/name */
    tv_material_t material; /* 8 */
    uint32_t line;          /* 12: in the file */
} tv_material_row_t;
static_assert(sizeof(tv_material_row_t) == 16);

typedef struct tv_materials_t {
    const char *path;        /* 0 */
    tv_material_row_t *rows; /* 8 */
    const char **needs;      /* 16: group/name */
    uint32_t row_count;      /* 24 */
    uint32_t need_count;     /* 28 */
} tv_materials_t;
static_assert(sizeof(tv_materials_t) == 32);

/* Reads and checks the file, with everything taken from arena. On failure prints the line and the reason. */
bool tv_materials_load(tp_arena_t *arena, const char *path, tv_materials_t *out);

/* What the table says of group/name. A texture it does not name gets all zero, with alpha derived from its pixels. */
tv_material_t tv_materials_find(const tv_materials_t *materials, const char *texture);

#endif
