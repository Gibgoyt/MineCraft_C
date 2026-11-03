/* config/mobs/ -> the mob tables. See compile/src/game_tables/mobs/README.md. */
#ifndef GAME_TABLES_MOBS_INDEX_H
#define GAME_TABLES_MOBS_INDEX_H

#include "texture_pack/support/index.h"

/*
 * Reads <config>/mobs/<mob>/index.yaml for every mob, checks them against <config>/items/, <config>/blocks/ and <textures>/, and
 * writes <out>/mobs/index.h and index.c. Every problem found is printed as path:line:column: message, and false is returned with
 * nothing written. Files holding the same bytes are left alone, and the same input gives the same bytes. A tree with no
 * <config>/mobs at all has no tables of them; the real tree's outputs are asked for by the build.
 */
bool game_tables_mobs_build(tp_arena_t *arena, const char *config, const char *textures, const char *out);

#endif
