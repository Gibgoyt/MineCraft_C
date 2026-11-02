/* config/biomes/ -> the biome tables. See compile/src/game_tables/biomes/README.md. */
#ifndef GAME_TABLES_BIOMES_INDEX_H
#define GAME_TABLES_BIOMES_INDEX_H

#include "texture_pack/support/index.h"

/*
 * Reads <config>/biomes/<biome>/index.yaml for every biome, checks each block they name against <config>/blocks/, and writes
 * <out>/biomes/index.h and index.c. Every problem found is printed as path:line:column: message, and false is returned with
 * nothing written. Files holding the same bytes are left alone, and the same input gives the same bytes. A config with no
 * biomes directory at all has no biome tables to make, and returns true having written nothing.
 */
bool game_tables_biomes_build(tp_arena_t *arena, const char *config, const char *out);

#endif
