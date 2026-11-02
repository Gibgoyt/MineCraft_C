/* See README.md. */
#include "texture_pack/mctex/index.h"

#include <string.h>

#define MCTEX_FNV_OFFSET UINT64_C(0xCBF29CE484222325)
#define MCTEX_FNV_PRIME UINT64_C(0x100000001B3)

uint64_t mctex_hash(const uint8_t *bytes, uint64_t length) {
    uint64_t hash = MCTEX_FNV_OFFSET;
    for (uint64_t i = 0; i < length; i++) {
        hash ^= bytes[i];
        hash *= MCTEX_FNV_PRIME;
    }
    return hash;
}

bool mctex_pixel_bytes(uint32_t width, uint32_t height, uint32_t frame_count, uint64_t *out_bytes) {
    if (width == 0 || height == 0 || frame_count == 0 || width > MCTEX_MAX_DIMENSION || height > MCTEX_MAX_DIMENSION ||
        frame_count > MCTEX_MAX_FRAMES) {
        return false;
    }
    /* 4096 * 4096 * 256 * 4 is 2^34, so this cannot overflow. */
    const uint64_t bytes = (uint64_t)width * height * frame_count * 4u;
    if (bytes > MCTEX_MAX_PIXEL_BYTES) {
        return false;
    }
    *out_bytes = bytes;
    return true;
}

mctex_status_t mctex_encode(uint8_t *out, uint64_t capacity, uint32_t width, uint32_t height, uint32_t frame_count,
                            const uint8_t *pixels, uint64_t pixel_bytes) {
    uint64_t needed = 0;
    if (!mctex_pixel_bytes(width, height, frame_count, &needed)) {
        return MCTEX_BAD_SIZE;
    }
    if (pixel_bytes != needed || capacity < sizeof(mctex_header_t) + needed) {
        return MCTEX_NO_ROOM;
    }
    const mctex_header_t header = {.magic = MCTEX_MAGIC,
                                   .version = MCTEX_VERSION,
                                   .width = width,
                                   .height = height,
                                   .frame_count = frame_count,
                                   ._pad0 = 0,
                                   .pixel_hash = mctex_hash(pixels, needed)};
    memcpy(out, &header, sizeof(header));
    memcpy(out + sizeof(header), pixels, needed);
    return MCTEX_OK;
}

mctex_status_t mctex_parse(const uint8_t *file, uint64_t length, mctex_view_t *view) {
    memset(view, 0, sizeof(*view));
    mctex_header_t header;
    if (length < sizeof(header)) {
        return MCTEX_TRUNCATED;
    }
    memcpy(&header, file, sizeof(header));
    if (header.magic != MCTEX_MAGIC) {
        return MCTEX_BAD_MAGIC;
    }
    if (header.version != MCTEX_VERSION) {
        return MCTEX_BAD_VERSION;
    }
    if (header._pad0 != 0) {
        return MCTEX_BAD_PADDING;
    }
    uint64_t pixel_bytes = 0;
    if (!mctex_pixel_bytes(header.width, header.height, header.frame_count, &pixel_bytes)) {
        return MCTEX_BAD_SIZE;
    }
    const uint64_t have = length - sizeof(header);
    if (have < pixel_bytes) {
        return MCTEX_TRUNCATED;
    }
    if (have > pixel_bytes) {
        return MCTEX_TRAILING;
    }
    const uint8_t *pixels = file + sizeof(header);
    if (mctex_hash(pixels, pixel_bytes) != header.pixel_hash) {
        return MCTEX_BAD_HASH;
    }
    *view = (mctex_view_t){.borrowed_pixels = pixels,
                           .pixel_bytes = pixel_bytes,
                           .width = header.width,
                           .height = header.height,
                           .frame_count = header.frame_count,
                           ._pad0 = {0}};
    return MCTEX_OK;
}

const char *mctex_status_text(mctex_status_t status) {
    switch (status) {
    case MCTEX_OK:
        return "no error";
    case MCTEX_TRUNCATED:
        return "the file is shorter than its header says";
    case MCTEX_BAD_MAGIC:
        return "the file does not start with MCTX";
    case MCTEX_BAD_VERSION:
        return "the file has a format version this code does not know";
    case MCTEX_BAD_SIZE:
        return "a dimension or frame count is zero or over the limit";
    case MCTEX_BAD_PADDING:
        return "the header's padding is not zero";
    case MCTEX_TRAILING:
        return "there are bytes after the pixels";
    case MCTEX_BAD_HASH:
        return "the pixels do not match the header's hash";
    case MCTEX_NO_ROOM:
        return "the output buffer or pixel length is wrong for these dimensions";
    }
    return "an unknown status";
}
