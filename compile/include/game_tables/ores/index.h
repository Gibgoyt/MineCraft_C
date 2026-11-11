/* config/ores/ -> the ore table. See compile/src/game_tables/ores/README.md. */
#ifndef GAME_TABLES_ORES_INDEX_H
#define GAME_TABLES_ORES_INDEX_H

#include "texture_pack/support/index.h"

/*
 * Reads <config>/ores/<ore>/index.yaml for every ore, checks the blocks and the item each names against <config>/blocks/ and
 * <config>/items/, and writes <out>/ores/index.h and index.c: every vein row, in the order each row's `order` gives. Every
 * problem found is printed as path:line:column: message, and false is returned with nothing written. Files holding the same
 * bytes are left alone, and the same input gives the same bytes. A config with no ores directory at all has no ore table to
 * make, and returns true having written nothing.
 */
bool game_tables_ores_build(tp_arena_t *arena, const char *config, const char *out);

#endif
