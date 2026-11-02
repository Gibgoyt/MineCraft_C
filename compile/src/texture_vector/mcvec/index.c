/* See README.md. */
#include "texture_vector/mcvec/index.h"

#include "texture_pack/mctex/index.h"

#include <string.h>

bool mcvec_file_bytes(uint32_t width, uint32_t height, uint32_t frame_count, uint32_t palette_count,
                      uint64_t *out_bytes) {
    uint64_t pixel_bytes = 0;
    if (!mctex_pixel_bytes(width, height, frame_count, &pixel_bytes) || palette_count == 0 ||
        palette_count > MCVEC_MAX_PALETTE) {
        return false;
    }
    /* Each pixel has 1 index, 1 height, 2 normal and 1 emissive byte. */
    *out_bytes = sizeof(mcvec_header_t) + (uint64_t)palette_count * 4u + pixel_bytes / 4u * 5u;
    return true;
}

mcvec_status_t mcvec_encode(uint8_t *out, uint64_t capacity, const mcvec_header_t *header, const uint8_t *palette,
                            const uint8_t *indices, const uint8_t *height, const uint8_t *normal,
                            const uint8_t *emissive) {
    uint64_t bytes = 0;
    if (!mcvec_file_bytes(header->width, header->height, header->frame_count, header->palette_count, &bytes) ||
        bytes > capacity) {
        return MCVEC_NO_ROOM;
    }
    const uint64_t pixels = (uint64_t)header->width * header->height * header->frame_count;
    uint8_t *at = out + sizeof(mcvec_header_t);
    memcpy(at, palette, (uint64_t)header->palette_count * 4u);
    at += (uint64_t)header->palette_count * 4u;
    memcpy(at, indices, pixels);
    at += pixels;
    memcpy(at, height, pixels);
    at += pixels;
    memcpy(at, normal, pixels * 2u);
    at += pixels * 2u;
    memcpy(at, emissive, pixels);

    mcvec_header_t written = *header;
    written.magic = MCVEC_MAGIC;
    written.version = MCVEC_VERSION;
    written._pad0 = 0;
    written._pad1 = 0;
    written.payload_hash = mctex_hash(out + sizeof(mcvec_header_t), bytes - sizeof(mcvec_header_t));
    memcpy(out, &written, sizeof(written));
    return MCVEC_OK;
}

static mcvec_status_t mcvec_check_header(const mcvec_header_t *header, uint64_t length) {
    if (header->magic != MCVEC_MAGIC) {
        return MCVEC_BAD_MAGIC;
    }
    if (header->version != MCVEC_VERSION) {
        return MCVEC_BAD_VERSION;
    }
    uint64_t bytes = 0;
    if (!mcvec_file_bytes(header->width, header->height, header->frame_count, header->palette_count, &bytes)) {
        return MCVEC_BAD_SIZE;
    }
    if (header->_pad0 != 0 || header->_pad1 != 0) {
        return MCVEC_BAD_PADDING;
    }
    const bool lit = header->emissive_class != MCVEC_EMISSIVE_NONE;
    if (header->alpha_class >= MCVEC_ALPHA_CLASS_COUNT || header->emissive_class >= MCVEC_EMISSIVE_CLASS_COUNT ||
        header->emissive_level > 15u || lit != (header->emissive_level != 0) || lit != (header->emissive_pixels != 0)) {
        return MCVEC_BAD_CLASS;
    }
    if (length < bytes) {
        return MCVEC_TRUNCATED;
    }
    return length > bytes ? MCVEC_TRAILING : MCVEC_OK;
}

mcvec_status_t mcvec_parse(const uint8_t *file, uint64_t length, mcvec_view_t *view) {
    memset(view, 0, sizeof(*view));
    if (length < sizeof(mcvec_header_t)) {
        return MCVEC_TRUNCATED;
    }
    mcvec_header_t header;
    memcpy(&header, file, sizeof(header));
    const mcvec_status_t checked = mcvec_check_header(&header, length);
    if (checked != MCVEC_OK) {
        return checked;
    }
    if (mctex_hash(file + sizeof(header), length - sizeof(header)) != header.payload_hash) {
        return MCVEC_BAD_HASH;
    }
    const uint64_t pixels = (uint64_t)header.width * header.height * header.frame_count;
    const uint8_t *at = file + sizeof(header);
    const uint8_t *palette = at;
    at += (uint64_t)header.palette_count * 4u;
    const uint8_t *indices = at;
    for (uint64_t i = 0; i < pixels; i++) {
        if (indices[i] >= header.palette_count) {
            return MCVEC_BAD_INDEX;
        }
    }
    view->header = header;
    view->borrowed_palette = palette;
    view->borrowed_indices = indices;
    view->borrowed_height = indices + pixels;
    view->borrowed_normal = indices + pixels * 2u;
    view->borrowed_emissive = indices + pixels * 4u;
    view->pixel_count = pixels;
    return MCVEC_OK;
}

const char *mcvec_status_text(mcvec_status_t status) {
    switch (status) {
    case MCVEC_OK:
        return "fine";
    case MCVEC_TRUNCATED:
        return "shorter than its header says";
    case MCVEC_BAD_MAGIC:
        return "not an .mcvec file";
    case MCVEC_BAD_VERSION:
        return "an .mcvec version this tool does not know";
    case MCVEC_BAD_SIZE:
        return "a size or palette count out of range";
    case MCVEC_BAD_PADDING:
        return "padding that is not zero";
    case MCVEC_BAD_CLASS:
        return "an alpha or emissive class that is unknown, or does not agree with the rest of the header";
    case MCVEC_TRAILING:
        return "bytes after the end of its data";
    case MCVEC_BAD_HASH:
        return "data that does not match its hash";
    case MCVEC_BAD_INDEX:
        return "a pixel naming a palette entry that does not exist";
    case MCVEC_NO_ROOM:
        return "too large for the buffer given";
    }
    return "an unknown status";
}
