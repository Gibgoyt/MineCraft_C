/*
 * Minecraft's default textures, imported into a git-ignored copy of textures/ for use on this machine only. See
 * compile/src/texture_pack/default_pack/README.md. The map, textures/import_minecraft_default.yaml, holds names and no
 * art, and is the only part of this that is committed.
 */
#ifndef TEXTURE_PACK_DEFAULT_PACK_INDEX_H
#define TEXTURE_PACK_DEFAULT_PACK_INDEX_H
#include "texture_pack/support/index.h"
#include <stdint.h>
/*
 * pack: the directory tools/fetch_minecraft_default.sh unpacked, holding mc_c_pin.txt and assets/minecraft/textures/.
 * map: textures/import_minecraft_default.yaml. textures: a copy of textures/, such as textures_local/, rewritten in place:
 * every texture the map takes from the pack gets a new index.mctex, index.yaml and README.md, and every other one is
 * left exactly as it was. Everything is decoded and checked before the first file is written, so a refusal changes nothing.
 */
bool tp_default_import(tp_arena_t *arena, const char *pack, const char *map, const char *textures);
#endif
