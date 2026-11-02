/* See README.md. */
#include "texture/registry/index.h"

#include <string.h>

enum : uint32_t {
    /* At most half full, so that a miss ends soon: a name that is not there reads a short run of slots. */
    TEXTURE_REGISTRY_SLOTS_PER_TEXTURE = 2u,
};

void texture_registry_init(texture_registry_t *registry, arena_t *arena, const texture_blob_view_t *blob) {
    const uint32_t count = blob->borrowed_header->texture_count;
    uint32_t slot_count = 1u;
    while (slot_count < count * TEXTURE_REGISTRY_SLOTS_PER_TEXTURE) {
        slot_count <<= 1;
    }
    uint32_t *slots = arena_alloc_zeroed(arena, (uint64_t)slot_count * sizeof(uint32_t), alignof(uint32_t));
    const uint32_t mask = slot_count - 1u;
    for (uint32_t id = 0; id < count; id++) {
        uint32_t slot = (uint32_t)blob->borrowed_entries[id].name_hash & mask;
        while (slots[slot] != 0u) {
            slot = (slot + 1u) & mask;
        }
        slots[slot] = id + 1u;
    }
    *registry = (texture_registry_t){.borrowed_slots = slots,
                                     .borrowed_entries = blob->borrowed_entries,
                                     .borrowed_names = blob->borrowed_names,
                                     .slot_mask = mask,
                                     .texture_count = count};
}

void texture_registry_deinit(texture_registry_t *registry) {
    memset(registry, 0, sizeof(*registry));
}

bool texture_registry_find(const texture_registry_t *registry, const char *name, uint32_t *out_id) {
    const uint64_t length = strlen(name);
    if (registry->texture_count == 0u || length > UINT16_MAX) {
        return false;
    }
    const uint64_t hash = texture_blob_hash((const uint8_t *)name, length);
    uint32_t slot = (uint32_t)hash & registry->slot_mask;
    while (registry->borrowed_slots[slot] != 0u) {
        const uint32_t id = registry->borrowed_slots[slot] - 1u;
        const texture_blob_entry_t *entry = &registry->borrowed_entries[id];
        if (entry->name_hash == hash && entry->name_length == length &&
            memcmp(registry->borrowed_names + entry->name_offset, name, length) == 0) {
            *out_id = id;
            return true;
        }
        slot = (slot + 1u) & registry->slot_mask;
    }
    return false;
}
