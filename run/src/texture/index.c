/* See README.md. The Owner: it holds the one region, and lends its arena to the parts. */
#include "texture/index.h"

#include "log/index.h"
#include "simd/embed_assets/index.h"

#include <inttypes.h>
#include <string.h>

enum : uint64_t {
    /* The smallest region there is. At 65535 textures the table is 128 Ki slots of 4 bytes, a quarter of this. */
    TEXTURE_TABLE_BYTES = HEAP_REGION_ALIGNMENT,
};
static_assert(TEXTURE_BLOB_MAX_TEXTURES * 2ull * 2ull * sizeof(uint32_t) <= TEXTURE_TABLE_BYTES);

bool texture_init(texture_t *texture) {
    return texture_init_from(texture, embed_assets_texture_blob, embed_assets_texture_blob_bytes);
}

bool texture_init_from(texture_t *texture, const uint8_t *blob, uint64_t length) {
    memset(texture, 0, sizeof(*texture));
    const texture_blob_status_t status = texture_blob_parse(blob, length, &texture->blob);
    if (status != TEXTURE_BLOB_OK) {
        LOG_ERROR("texture: the blob of %" PRIu64 " bytes is refused: %s", length, texture_blob_status_text(status));
        return false;
    }
    if (!heap_reserve(TEXTURE_TABLE_BYTES, &texture->owned_region)) {
        memset(texture, 0, sizeof(*texture));
        return false; /* it has said why */
    }
    arena_init(&texture->arena_tables, "texture_tables", texture->owned_region.owned_base, texture->owned_region.bytes);
    texture_registry_init(&texture->registry, &texture->arena_tables, &texture->blob);
    LOG_INFO("texture: %" PRIu32 " textures, %" PRIu64 " bytes embedded", texture->blob.borrowed_header->texture_count,
             length);
    return true;
}

void texture_deinit(texture_t *texture) {
    texture_registry_deinit(&texture->registry);
    arena_deinit(&texture->arena_tables);
    heap_release(&texture->owned_region);
    memset(texture, 0, sizeof(*texture));
}

bool texture_find(const texture_t *texture, const char *name, uint32_t *out_id) {
    return texture_registry_find(&texture->registry, name, out_id);
}

uint32_t texture_count(const texture_t *texture) {
    return texture->registry.texture_count;
}

bool texture_info_get(const texture_t *texture, uint32_t id, texture_info_t *out) {
    if (id >= texture_count(texture)) {
        return false;
    }
    const texture_blob_entry_t *entry = &texture->blob.borrowed_entries[id];
    out->borrowed_name = (const char *)(texture->blob.borrowed_names + entry->name_offset);
    out->borrowed_entry = entry;
    return true;
}

bool texture_mip_get(const texture_t *texture, uint32_t id, uint32_t level, uint32_t frame, texture_mip_t *out) {
    if (id >= texture_count(texture)) {
        return false;
    }
    const texture_blob_entry_t *entry = &texture->blob.borrowed_entries[id];
    if (level >= entry->level_count || frame >= entry->frame_count) {
        return false;
    }
    const uint64_t frame_bytes = texture_blob_level_frame_bytes(entry->width, entry->height, level);
    out->borrowed_pixels =
        texture->blob.borrowed_pixels + entry->pixels_offset + entry->level_offset[level] + frame_bytes * frame;
    out->bytes = frame_bytes;
    out->width = texture_blob_level_dimension(entry->width, level);
    out->height = texture_blob_level_dimension(entry->height, level);
    return true;
}

bool texture_vector_get(const texture_t *texture, uint32_t id, uint32_t frame, texture_vector_t *out) {
    if (id >= texture_count(texture)) {
        return false;
    }
    const texture_blob_entry_t *entry = &texture->blob.borrowed_entries[id];
    if (frame >= entry->frame_count) {
        return false;
    }
    const uint64_t count = (uint64_t)entry->width * entry->height;
    const uint64_t all_frames = count * entry->frame_count;
    const uint8_t *palette = texture->blob.borrowed_vectors + entry->vector_offset;
    const uint8_t *indices = palette + entry->palette_count * 4ull;
    const uint8_t *height = indices + all_frames;
    const uint8_t *normal = height + all_frames;
    const uint8_t *emissive = normal + all_frames * 2u;
    out->borrowed_palette = palette;
    out->borrowed_indices = indices + count * frame;
    out->borrowed_height = height + count * frame;
    out->borrowed_normal = (const int8_t *)(const void *)(normal + count * frame * 2u);
    out->borrowed_emissive = emissive + count * frame;
    out->count = count;
    return true;
}

void texture_pixels_get(const texture_t *texture, const uint8_t **out_pixels, uint64_t *out_bytes) {
    *out_pixels = texture->blob.borrowed_pixels;
    *out_bytes = texture->blob.borrowed_header != nullptr ? texture->blob.borrowed_header->pixels_bytes : 0u;
}
