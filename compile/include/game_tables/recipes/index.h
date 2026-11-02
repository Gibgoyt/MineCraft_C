/* config/recipes/ -> the recipe tables. See compile/src/game_tables/recipes/README.md. */
#ifndef GAME_TABLES_RECIPES_INDEX_H
#define GAME_TABLES_RECIPES_INDEX_H

#include "texture_pack/support/index.h"

/*
 * Reads <config>/recipes/<recipe>/index.yaml for every recipe, checks them against <config>/items/ and one another, and writes
 * <out>/recipes/index.h and index.c. Every problem found is printed as path:line:column: message, and false is returned with nothing
 * written. A tree with no recipes directory writes nothing and is no problem. The same input gives the same bytes.
 */
bool game_tables_recipes_build(tp_arena_t *arena, const char *config, const char *out);

#endif
