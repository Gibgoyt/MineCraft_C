/* The data of asm/src/embed_assets/index.asm. See its README.md. No kernels here, only bytes linked into the program. */
#ifndef MC_RUN_SIMD_EMBED_ASSETS_INDEX_H
#define MC_RUN_SIMD_EMBED_ASSETS_INDEX_H

#include <stdint.h>

/* The texture blob: see types/texture_blob/index.h. On a 64-byte boundary, read-only, and it lasts as long as the process. */
extern const uint8_t embed_assets_texture_blob[];
extern const uint64_t embed_assets_texture_blob_bytes;

/* The sound blob: see types/sound_blob/index.h. The same promises. */
extern const uint8_t embed_assets_sound_blob[];
extern const uint64_t embed_assets_sound_blob_bytes;

#endif
