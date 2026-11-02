/* config/items/ -> the item tables. See compile/src/game_tables/items/README.md. */
#ifndef GAME_TABLES_ITEMS_INDEX_H
#define GAME_TABLES_ITEMS_INDEX_H

#include "texture_pack/support/index.h"

/*
 * Reads <config>/items/<item>/index.yaml for every item, checks them against <config>/blocks/ and <textures>/, and writes
 * <out>/items/index.h and index.c. Every problem found is printed as path:line:column: message, and false is returned with
 * nothing written. Files holding the same bytes are left alone, and the same input gives the same bytes.
 */
bool game_tables_items_build(tp_arena_t *arena, const char *config, const char *textures, const char *out);

#endif
