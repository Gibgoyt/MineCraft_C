/* A loaded sounds/ tree -> the sound blob. See compile/src/sound_pack/blob/README.md, and the format in types/sound_blob/. */
#ifndef SOUND_PACK_BLOB_INDEX_H
#define SOUND_PACK_BLOB_INDEX_H

#include "sound_pack/tree/index.h"

/* Writes the blob to path, and leaves a file holding the same bytes alone. The same tree gives the same bytes. */
bool sound_pack_blob_write(tp_arena_t *arena, const sound_tree_t *tree, const char *path);

#endif
