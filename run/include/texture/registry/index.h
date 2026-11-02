/* See run/src/texture/registry/README.md. */
#ifndef MC_RUN_TEXTURE_REGISTRY_INDEX_H
#define MC_RUN_TEXTURE_REGISTRY_INDEX_H

#include "arena/index.h"
#include "texture/blob/index.h"

#include <stdint.h>

/* From a name to an id, with open addressing. A slot holds id + 1, and 0 means empty. */
typedef struct texture_registry_t {
    uint32_t *borrowed_slots;                     /* 0: from the arena its Owner gave */
    const texture_blob_entry_t *borrowed_entries; /* 8 */
    const uint8_t *borrowed_names;                /* 16 */
    uint32_t slot_mask;                           /* 24: slot count less 1; the count is a power of two */
    uint32_t texture_count;                       /* 28 */
} texture_registry_t;
static_assert(sizeof(texture_registry_t) == 32);

/* Builds the table from arena. Two textures of one name cannot reach here: the blob's parser wants names strictly ascending. */
void texture_registry_init(texture_registry_t *registry, arena_t *arena, const texture_blob_view_t *blob);

/* Empties the handle. The memory stays with the arena. */
void texture_registry_deinit(texture_registry_t *registry);

/* name is "<group>/<name>", such as "blocks/dirt". Reads only, so any thread may call it once init has returned. An empty
 * handle finds nothing. */
bool texture_registry_find(const texture_registry_t *registry, const char *name, uint32_t *out_id);

#endif
