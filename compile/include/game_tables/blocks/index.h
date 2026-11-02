/* config/blocks/ -> the block tables. See compile/src/game_tables/blocks/README.md. */
#ifndef GAME_TABLES_BLOCKS_INDEX_H
#define GAME_TABLES_BLOCKS_INDEX_H

#include "texture_pack/support/index.h"

/*
 * Reads <config>/blocks/<block>/index.yaml for every block, checks them against <config>/items/ and <textures>/, and writes
 * <out>/blocks/index.h and index.c. Every problem found is printed as path:line:column: message, and false is returned with
 * nothing written. Files holding the same bytes are left alone, and the same input gives the same bytes.
 */
bool game_tables_blocks_build(tp_arena_t *arena, const char *config, const char *textures, const char *out);

#endif
