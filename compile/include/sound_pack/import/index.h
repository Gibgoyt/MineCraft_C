/* One recording, already converted to raw PCM -> one sound in sounds/. See compile/src/sound_pack/import/README.md. */
#ifndef SOUND_PACK_IMPORT_INDEX_H
#define SOUND_PACK_IMPORT_INDEX_H

#include "texture_pack/support/index.h"

/* What a person names on the command line. All of it is untrusted until sound_pack_import has checked it. */
typedef struct sound_import_t {
    const char *raw;     /* 0: mono s16le at 48000 Hz, as ffmpeg writes it */
    const char *sounds;  /* 8 */
    const char *family;  /* 16 */
    const char *name;    /* 24: such as hit_1 */
    const char *origin;  /* 32: the pack, as sounds/ATTRIBUTION.md credits it */
    const char *file;    /* 40: which file of the pack */
    const char *url;     /* 48: where the pack and its licence are */
    const char *licence; /* 56: must be CC0-1.0 */
} sound_import_t;
static_assert(sizeof(sound_import_t) == 64);

/* Checks everything first, so a refused recording changes nothing. Replaces whatever that sound was. */
bool sound_pack_import(tp_arena_t *arena, const sound_import_t *import);

#endif
