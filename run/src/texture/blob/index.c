/* See README.md. Everything past the hash guards against a wrong writer: the hash alone would bless whatever it wrote. */
#include "texture/blob/index.h"

#include <string.h>

uint64_t texture_blob_hash(const uint8_t *bytes, uint64_t length) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint64_t i = 0; i < length; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

static texture_blob_status_t texture_blob_check_header(const texture_blob_header_t *header, uint64_t length) {
    if (header->magic != TEXTURE_BLOB_MAGIC) {
        return TEXTURE_BLOB_BAD_MAGIC;
    }
    if (header->version != TEXTURE_BLOB_VERSION) {
        return TEXTURE_BLOB_BAD_VERSION;
    }
    if (header->total_bytes > length) {
        return TEXTURE_BLOB_TRUNCATED;
    }
    if (header->total_bytes < length) {
        return TEXTURE_BLOB_TRAILING;
    }
    return TEXTURE_BLOB_OK;
}

/* texture_count and name_bytes are 32 bits, and each offset is compared before it is added to, so no sum here can wrap. */
static texture_blob_status_t texture_blob_check_layout(const texture_blob_header_t *header, uint64_t length) {
    if (header->texture_count == 0u || header->texture_count > TEXTURE_BLOB_MAX_TEXTURES) {
        return TEXTURE_BLOB_BAD_LAYOUT;
    }
    const uint64_t names_offset = sizeof(*header) + (uint64_t)header->texture_count * sizeof(texture_blob_entry_t);
    if (header->entries_offset != sizeof(*header) || header->names_offset != names_offset) {
        return TEXTURE_BLOB_BAD_LAYOUT;
    }
    const uint64_t pixels_offset = texture_blob_align_up(names_offset + header->name_bytes, TEXTURE_BLOB_ALIGNMENT);
    if (header->pixels_offset != pixels_offset || pixels_offset > length ||
        header->pixels_bytes > length - pixels_offset) {
        return TEXTURE_BLOB_BAD_LAYOUT;
    }
    const uint64_t vectors_offset =
        texture_blob_align_up(pixels_offset + header->pixels_bytes, TEXTURE_BLOB_TEXTURE_ALIGNMENT);
    if (header->vectors_offset != vectors_offset || vectors_offset > length ||
        header->vectors_bytes != length - vectors_offset) {
        return TEXTURE_BLOB_BAD_LAYOUT;
    }
    return TEXTURE_BLOB_OK;
}

static texture_blob_status_t texture_blob_check_name(const texture_blob_view_t *view, const texture_blob_entry_t *entry,
                                                     const texture_blob_entry_t *previous) {
    const uint64_t name_bytes = view->borrowed_header->name_bytes;
    if (entry->name_length == 0u || (uint64_t)entry->name_offset + entry->name_length + 1u > name_bytes) {
        return TEXTURE_BLOB_BAD_NAME;
    }
    const uint8_t *name = view->borrowed_names + entry->name_offset;
    if (name[entry->name_length] != 0u || memchr(name, 0, entry->name_length) != nullptr ||
        texture_blob_hash(name, entry->name_length) != entry->name_hash) {
        return TEXTURE_BLOB_BAD_NAME;
    }
    /* Both are NUL-terminated now, and an id is a position, so ids must follow the names' order. */
    if (previous != nullptr &&
        strcmp((const char *)(view->borrowed_names + previous->name_offset), (const char *)name) >= 0) {
        return TEXTURE_BLOB_BAD_NAME;
    }
    return TEXTURE_BLOB_OK;
}

/* Reworks the whole chain's offsets from the dimensions, in the one way the format allows. */
static texture_blob_status_t texture_blob_check_levels(const texture_blob_entry_t *entry) {
    if (entry->width == 0u || entry->height == 0u || entry->width > TEXTURE_BLOB_MAX_DIMENSION ||
        entry->height > TEXTURE_BLOB_MAX_DIMENSION || entry->frame_count == 0u ||
        entry->frame_count > TEXTURE_BLOB_MAX_FRAMES ||
        entry->level_count != texture_blob_level_count(entry->width, entry->height) || entry->palette_count == 0u ||
        entry->palette_count > TEXTURE_BLOB_MAX_PALETTE) {
        return TEXTURE_BLOB_BAD_SIZE;
    }
    uint64_t offset = 0;
    for (uint32_t level = 0; level < TEXTURE_BLOB_MAX_LEVELS; level++) {
        if (level >= entry->level_count) {
            if (entry->level_offset[level] != 0u) {
                return TEXTURE_BLOB_BAD_OFFSET;
            }
            continue;
        }
        if (entry->level_offset[level] != offset) {
            return TEXTURE_BLOB_BAD_OFFSET;
        }
        offset += texture_blob_level_frame_bytes(entry->width, entry->height, level) * entry->frame_count;
    }
    return entry->pixels_bytes == offset ? TEXTURE_BLOB_OK : TEXTURE_BLOB_BAD_OFFSET;
}

static texture_blob_status_t texture_blob_check_classes(const texture_blob_entry_t *entry) {
    if (entry->alpha_class >= TEXTURE_BLOB_ALPHA_CLASS_COUNT ||
        entry->emissive_class >= TEXTURE_BLOB_EMISSIVE_CLASS_COUNT ||
        entry->emissive_level > TEXTURE_BLOB_MAX_EMISSIVE || entry->palette_exact > 1u || entry->_pad0[0] != 0u ||
        entry->_pad0[1] != 0u) {
        return TEXTURE_BLOB_BAD_CLASS;
    }
    return TEXTURE_BLOB_OK;
}

/* Textures lie end to end in id order in both sections, so a running offset proves that none overlaps or runs past the end. */
static texture_blob_status_t texture_blob_check_entries(const texture_blob_view_t *view) {
    const texture_blob_header_t *header = view->borrowed_header;
    uint64_t pixels_end = 0;
    uint64_t vectors_end = 0;
    const texture_blob_entry_t *previous = nullptr;
    for (uint32_t id = 0; id < header->texture_count; id++) {
        const texture_blob_entry_t *entry = &view->borrowed_entries[id];
        texture_blob_status_t status = texture_blob_check_name(view, entry, previous);
        status = status != TEXTURE_BLOB_OK ? status : texture_blob_check_levels(entry);
        status = status != TEXTURE_BLOB_OK ? status : texture_blob_check_classes(entry);
        if (status != TEXTURE_BLOB_OK) {
            return status;
        }
        const uint64_t pixel_count = (uint64_t)entry->width * entry->height * entry->frame_count;
        if (entry->pixels_offset != texture_blob_align_up(pixels_end, TEXTURE_BLOB_TEXTURE_ALIGNMENT) ||
            entry->vector_offset != texture_blob_align_up(vectors_end, TEXTURE_BLOB_TEXTURE_ALIGNMENT) ||
            entry->vector_bytes != entry->palette_count * 4ull + pixel_count * 5u) {
            return TEXTURE_BLOB_BAD_OFFSET;
        }
        pixels_end = entry->pixels_offset + entry->pixels_bytes;
        vectors_end = entry->vector_offset + entry->vector_bytes;
        if (pixels_end > header->pixels_bytes || vectors_end > header->vectors_bytes) {
            return TEXTURE_BLOB_BAD_OFFSET;
        }
        previous = entry;
    }
    return pixels_end == header->pixels_bytes && vectors_end == header->vectors_bytes ? TEXTURE_BLOB_OK
                                                                                      : TEXTURE_BLOB_BAD_OFFSET;
}

texture_blob_status_t texture_blob_parse(const uint8_t *bytes, uint64_t length, texture_blob_view_t *view) {
    memset(view, 0, sizeof(*view));
    if (length < sizeof(texture_blob_header_t)) {
        return TEXTURE_BLOB_TRUNCATED;
    }
    if ((uintptr_t)bytes % TEXTURE_BLOB_ALIGNMENT != 0u) {
        return TEXTURE_BLOB_BAD_ALIGNMENT;
    }
    const texture_blob_header_t *header = (const texture_blob_header_t *)(const void *)bytes;
    texture_blob_status_t status = texture_blob_check_header(header, length);
    if (status != TEXTURE_BLOB_OK) {
        return status;
    }
    if (texture_blob_hash(bytes + sizeof(*header), length - sizeof(*header)) != header->payload_hash) {
        return TEXTURE_BLOB_BAD_HASH;
    }
    status = texture_blob_check_layout(header, length);
    if (status != TEXTURE_BLOB_OK) {
        return status;
    }
    const texture_blob_view_t checked = {
        .borrowed_header = header,
        .borrowed_entries = (const texture_blob_entry_t *)(const void *)(bytes + header->entries_offset),
        .borrowed_names = bytes + header->names_offset,
        .borrowed_pixels = bytes + header->pixels_offset,
        .borrowed_vectors = bytes + header->vectors_offset,
    };
    status = texture_blob_check_entries(&checked);
    if (status != TEXTURE_BLOB_OK) {
        return status;
    }
    *view = checked;
    return TEXTURE_BLOB_OK;
}

const char *texture_blob_status_text(texture_blob_status_t status) {
    switch (status) {
    case TEXTURE_BLOB_OK:
        return "the blob is whole";
    case TEXTURE_BLOB_TRUNCATED:
        return "shorter than its header says";
    case TEXTURE_BLOB_BAD_ALIGNMENT:
        return "not on a 64-byte boundary";
    case TEXTURE_BLOB_BAD_MAGIC:
        return "not a texture blob";
    case TEXTURE_BLOB_BAD_VERSION:
        return "a version this program does not read";
    case TEXTURE_BLOB_TRAILING:
        return "bytes after what its header says";
    case TEXTURE_BLOB_BAD_HASH:
        return "its bytes are not what was written";
    case TEXTURE_BLOB_BAD_LAYOUT:
        return "a section is not where the format puts it";
    case TEXTURE_BLOB_BAD_NAME:
        return "a texture's name is out of bounds, out of order, or not what its hash says";
    case TEXTURE_BLOB_BAD_SIZE:
        return "a texture's size is out of range";
    case TEXTURE_BLOB_BAD_OFFSET:
        return "a texture's data is not where it must be";
    case TEXTURE_BLOB_BAD_CLASS:
        return "a texture's class or padding is wrong";
    }
    return "an unknown status";
}
