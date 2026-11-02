/*
 * PNG decoding. See README.md. Written from the PNG specification (ISO/IEC 15948).
 *
 * The steps, each of which proves what the next one relies on:
 * 1. png_scan walks every chunk, comparing each length against what is left of the file, and checks every CRC and rule.
 *    Afterwards the chunk walk is known to stay inside the file, and the image's sizes are known.
 * 2. png_decode checks the caller's buffers against those sizes, then joins the IDAT payloads.
 * 3. inflate_zlib must produce exactly raw_bytes, with its own bounds checks.
 * 4. png_decode_pass undoes the filters and writes pixels. Each pass's rows were counted into raw_bytes, so they lie
 *    inside the work buffer, and the Adam7 tables keep every target inside width * height.
 */
#include "png/index.h"

#include "inflate/index.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    PNG_SIGNATURE_BYTES = 8u,
    PNG_CHUNK_OVERHEAD = 12u, /* length, type and CRC */
    PNG_CHUNK_MAX_LENGTH = 0x7FFFFFFFu,
    PNG_IHDR_LENGTH = 13u,
    PNG_MAX_PALETTE_ENTRIES = 256u,
    PNG_RGBA_BYTES = 4u,
    PNG_ADAM7_PASSES = 7u,
    PNG_DEFLATE_MAX_RATIO = 1032u, /* a length-258 match in the shortest possible two bits: 258 * 8 / 2 */
};

#define PNG_CHUNK_TYPE(a, b, c, d)                                                                                     \
    (((uint32_t)(a) << 24u) | ((uint32_t)(b) << 16u) | ((uint32_t)(c) << 8u) | (uint32_t)(d))
#define PNG_TYPE_IHDR PNG_CHUNK_TYPE('I', 'H', 'D', 'R')
#define PNG_TYPE_PLTE PNG_CHUNK_TYPE('P', 'L', 'T', 'E')
#define PNG_TYPE_IDAT PNG_CHUNK_TYPE('I', 'D', 'A', 'T')
#define PNG_TYPE_IEND PNG_CHUNK_TYPE('I', 'E', 'N', 'D')
#define PNG_TYPE_TRNS PNG_CHUNK_TYPE('t', 'R', 'N', 'S')

typedef struct png_crc_table_t {
    uint32_t entries[256];
} png_crc_table_t;
static_assert(sizeof(png_crc_table_t) == 1024);

/* One chunk, with its length already proven to fit the file. */
typedef struct png_chunk_t {
    const uint8_t *borrowed_data; /* 0 */
    uint64_t offset;              /* 8: of the chunk's length field */
    uint64_t next_offset;         /* 16 */
    uint32_t length;              /* 24 */
    uint32_t type;                /* 28 */
} png_chunk_t;
static_assert(sizeof(png_chunk_t) == 32);

/* What png_scan learned that the decoding needs. Both pointers borrow from the file. */
typedef struct png_scan_t {
    png_info_t info;                 /* 0 */
    const uint8_t *borrowed_palette; /* 48: triples, or nullptr */
    const uint8_t *borrowed_trns;    /* 56: or nullptr */
    uint64_t first_idat_offset;      /* 64: where errors inside the compressed image data are reported */
    uint32_t palette_entries;        /* 72 */
    uint32_t trns_length;            /* 76 */
} png_scan_t;
static_assert(sizeof(png_scan_t) == 80);

/* One Adam7 pass, or the whole image when not interlaced. */
typedef struct png_pass_t {
    uint8_t start_x; /* 0 */
    uint8_t start_y; /* 1 */
    uint8_t step_x;  /* 2 */
    uint8_t step_y;  /* 3 */
} png_pass_t;
static_assert(sizeof(png_pass_t) == 4);

static const png_pass_t PNG_ADAM7[PNG_ADAM7_PASSES] = {
    {0u, 0u, 8u, 8u}, {4u, 0u, 8u, 8u}, {0u, 4u, 4u, 8u}, {2u, 0u, 4u, 4u},
    {0u, 2u, 2u, 4u}, {1u, 0u, 2u, 2u}, {0u, 1u, 1u, 2u},
};
static const png_pass_t PNG_WHOLE_IMAGE = {0u, 0u, 1u, 1u};

[[gnu::format(printf, 4, 5)]]
static png_status_t png_fail(png_error_t *error, png_status_t status, uint64_t offset, const char *format, ...) {
    error->offset = offset;
    error->status = (uint8_t)status;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(error->message, sizeof(error->message), format, arguments);
    va_end(arguments);
    return status;
}

static void png_crc_table_fill(png_crc_table_t *table) {
    for (uint32_t index = 0u; index < 256u; index += 1u) {
        uint32_t value = index;
        for (uint32_t bit = 0u; bit < 8u; bit += 1u) {
            value = (value & 1u) != 0u ? 0xEDB88320u ^ (value >> 1u) : value >> 1u;
        }
        table->entries[index] = value;
    }
}

static uint32_t png_crc(const png_crc_table_t *table, const uint8_t *data, uint64_t length) {
    uint32_t value = 0xFFFFFFFFu;
    for (uint64_t index = 0u; index < length; index += 1u) {
        value = table->entries[(value ^ data[index]) & 0xFFu] ^ (value >> 8u);
    }
    return value ^ 0xFFFFFFFFu;
}

static uint32_t png_read_u32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24u) | ((uint32_t)data[1] << 16u) | ((uint32_t)data[2] << 8u) | (uint32_t)data[3];
}

static uint32_t png_read_u16(const uint8_t *data) {
    return ((uint32_t)data[0] << 8u) | (uint32_t)data[1];
}

static bool png_is_letter(uint8_t byte) {
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
}

/* Text of a chunk type for messages. It has been checked to be letters by the time a message names it. */
static void png_type_text(uint32_t type, char text[5]) {
    text[0] = (char)(uint8_t)(type >> 24u);
    text[1] = (char)(uint8_t)(type >> 16u);
    text[2] = (char)(uint8_t)(type >> 8u);
    text[3] = (char)(uint8_t)type;
    text[4] = '\0';
}

/* Proves one chunk lies inside the file and that its type and CRC are good. */
static png_status_t png_chunk_read(const png_crc_table_t *table, const uint8_t *file, uint64_t length, uint64_t offset,
                                   png_chunk_t *chunk, png_error_t *error) {
    const uint64_t left = length - offset;
    if (left < PNG_CHUNK_OVERHEAD) {
        return png_fail(error, PNG_TRUNCATED, offset, "the file ends at byte %" PRIu64 " before an IEND chunk", length);
    }
    chunk->offset = offset;
    chunk->length = png_read_u32(file + offset);
    const uint8_t *type = file + offset + 4u;
    chunk->type = png_read_u32(type);
    if (chunk->length > PNG_CHUNK_MAX_LENGTH) {
        return png_fail(error, PNG_BAD_CHUNK, offset, "a chunk claims a length of %" PRIu32 " bytes", chunk->length);
    }
    if (!png_is_letter(type[0]) || !png_is_letter(type[1]) || !png_is_letter(type[2]) || !png_is_letter(type[3])) {
        return png_fail(error, PNG_BAD_CHUNK, offset, "a chunk type is not four letters");
    }
    char name[5];
    png_type_text(chunk->type, name);
    if ((uint64_t)chunk->length > left - PNG_CHUNK_OVERHEAD) {
        return png_fail(error, PNG_TRUNCATED, offset, "%s claims %" PRIu32 " bytes and the file has %" PRIu64, name,
                        chunk->length, left - PNG_CHUNK_OVERHEAD);
    }
    chunk->borrowed_data = type + 4u;
    const uint32_t stored = png_read_u32(chunk->borrowed_data + chunk->length);
    const uint32_t computed = png_crc(table, type, (uint64_t)chunk->length + 4u);
    if (stored != computed) {
        return png_fail(error, PNG_BAD_CRC, offset, "%s has CRC %08" PRIx32 " and its bytes give %08" PRIx32, name,
                        stored, computed);
    }
    chunk->next_offset = offset + PNG_CHUNK_OVERHEAD + chunk->length;
    return PNG_OK;
}

/* How many samples each pixel of a color type has. Zero for a type PNG does not define. */
static uint32_t png_channels(uint32_t color_type) {
    switch (color_type) {
    case PNG_COLOR_GRAY:
    case PNG_COLOR_PALETTE:
        return 1u;
    case PNG_COLOR_GRAY_ALPHA:
        return 2u;
    case PNG_COLOR_RGB:
        return 3u;
    case PNG_COLOR_RGBA:
        return 4u;
    default:
        return 0u;
    }
}

static bool png_depth_allowed(uint32_t color_type, uint32_t depth) {
    switch (color_type) {
    case PNG_COLOR_GRAY:
        return depth == 1u || depth == 2u || depth == 4u || depth == 8u || depth == 16u;
    case PNG_COLOR_PALETTE:
        return depth == 1u || depth == 2u || depth == 4u || depth == 8u;
    case PNG_COLOR_RGB:
    case PNG_COLOR_GRAY_ALPHA:
    case PNG_COLOR_RGBA:
        return depth == 8u || depth == 16u;
    default:
        return false;
    }
}

/* The pixel and row counts of one pass. Either may be zero, and such a pass has no bytes at all in the stream. */
static uint32_t png_pass_extent(uint32_t size, uint32_t start, uint32_t step) {
    return (size + step - start - 1u) / step;
}

/* The bytes of one row of data, without its filter byte. */
static uint64_t png_row_bytes(uint32_t pass_width, uint32_t pixel_bits) {
    return ((uint64_t)pass_width * pixel_bits + 7u) / 8u;
}

static uint64_t png_pass_bytes(const png_info_t *info, const png_pass_t *pass) {
    const uint32_t pass_width = png_pass_extent(info->width, pass->start_x, pass->step_x);
    const uint32_t pass_height = png_pass_extent(info->height, pass->start_y, pass->step_y);
    if (pass_width == 0u || pass_height == 0u) {
        return 0u;
    }
    const uint32_t pixel_bits = png_channels(info->color_type) * info->bit_depth;
    return (uint64_t)pass_height * (1u + png_row_bytes(pass_width, pixel_bits));
}

static png_status_t png_read_header(const png_chunk_t *chunk, png_info_t *info, png_error_t *error) {
    if (chunk->type != PNG_TYPE_IHDR) {
        char name[5];
        png_type_text(chunk->type, name);
        return png_fail(error, PNG_BAD_ORDER, chunk->offset, "the first chunk is %s, and must be IHDR", name);
    }
    if (chunk->length != PNG_IHDR_LENGTH) {
        return png_fail(error, PNG_BAD_HEADER, chunk->offset, "IHDR holds %" PRIu32 " bytes, and must hold 13",
                        chunk->length);
    }
    const uint8_t *data = chunk->borrowed_data;
    const uint32_t width = png_read_u32(data);
    const uint32_t height = png_read_u32(data + 4u);
    const uint32_t depth = data[8];
    const uint32_t color_type = data[9];
    if (width == 0u || height == 0u) {
        return png_fail(error, PNG_BAD_HEADER, chunk->offset,
                        "the image is %" PRIu32 "x%" PRIu32 ", and zero is not allowed", width, height);
    }
    if (width > PNG_MAX_DIMENSION || height > PNG_MAX_DIMENSION) {
        return png_fail(error, PNG_TOO_LARGE, chunk->offset,
                        "the image is %" PRIu32 "x%" PRIu32 ", and the limit is %u", width, height, PNG_MAX_DIMENSION);
    }
    if (!png_depth_allowed(color_type, depth)) {
        return png_fail(error, PNG_BAD_HEADER, chunk->offset,
                        "bit depth %" PRIu32 " with color type %" PRIu32 " is not defined", depth, color_type);
    }
    if (data[10] != 0u) {
        return png_fail(error, PNG_BAD_HEADER, chunk->offset, "compression method %u is not defined",
                        (uint32_t)data[10]);
    }
    if (data[11] != 0u) {
        return png_fail(error, PNG_BAD_HEADER, chunk->offset, "filter method %u is not defined", (uint32_t)data[11]);
    }
    if (data[12] > 1u) {
        return png_fail(error, PNG_BAD_HEADER, chunk->offset, "interlace method %u is not defined", (uint32_t)data[12]);
    }
    info->width = width;
    info->height = height;
    info->bit_depth = (uint8_t)depth;
    info->color_type = (uint8_t)color_type;
    info->interlaced = data[12];
    info->has_alpha = color_type == PNG_COLOR_GRAY_ALPHA || color_type == PNG_COLOR_RGBA ? 1u : 0u;

    info->raw_bytes = 0u;
    if (info->interlaced != 0u) {
        for (uint32_t index = 0u; index < PNG_ADAM7_PASSES; index += 1u) {
            info->raw_bytes += png_pass_bytes(info, &PNG_ADAM7[index]);
        }
    } else {
        info->raw_bytes = png_pass_bytes(info, &PNG_WHOLE_IMAGE);
    }
    info->pixel_bytes = (uint64_t)width * height * PNG_RGBA_BYTES;
    return PNG_OK;
}

static png_status_t png_read_palette(const png_chunk_t *chunk, png_scan_t *scan, png_error_t *error) {
    const uint32_t color_type = scan->info.color_type;
    if (color_type == PNG_COLOR_GRAY || color_type == PNG_COLOR_GRAY_ALPHA) {
        return png_fail(error, PNG_BAD_PALETTE, chunk->offset, "a grayscale image must not have PLTE");
    }
    if (chunk->length == 0u || chunk->length % 3u != 0u || chunk->length / 3u > PNG_MAX_PALETTE_ENTRIES) {
        return png_fail(error, PNG_BAD_PALETTE, chunk->offset,
                        "PLTE holds %" PRIu32 " bytes, and must hold 3 to 768 in threes", chunk->length);
    }
    const uint32_t entries = chunk->length / 3u;
    if (color_type == PNG_COLOR_PALETTE && entries > (1u << scan->info.bit_depth)) {
        return png_fail(error, PNG_BAD_PALETTE, chunk->offset, "PLTE has %" PRIu32 " entries at bit depth %u", entries,
                        (uint32_t)scan->info.bit_depth);
    }
    scan->borrowed_palette = chunk->borrowed_data;
    scan->palette_entries = entries;
    return PNG_OK;
}

static png_status_t png_read_transparency(const png_chunk_t *chunk, png_scan_t *scan, png_error_t *error) {
    const uint32_t color_type = scan->info.color_type;
    const uint32_t depth = scan->info.bit_depth;
    uint32_t samples = 0u;
    if (color_type == PNG_COLOR_PALETTE) {
        if (scan->borrowed_palette == nullptr) {
            return png_fail(error, PNG_BAD_ORDER, chunk->offset, "tRNS comes before PLTE");
        }
        if (chunk->length == 0u || chunk->length > scan->palette_entries) {
            return png_fail(error, PNG_BAD_TRANSPARENCY, chunk->offset,
                            "tRNS holds %" PRIu32 " bytes for %" PRIu32 " palette entries", chunk->length,
                            scan->palette_entries);
        }
    } else if (color_type == PNG_COLOR_GRAY) {
        samples = 1u;
    } else if (color_type == PNG_COLOR_RGB) {
        samples = 3u;
    } else {
        return png_fail(error, PNG_BAD_TRANSPARENCY, chunk->offset,
                        "an image with an alpha channel must not have tRNS");
    }
    if (samples != 0u) {
        if (chunk->length != samples * 2u) {
            return png_fail(error, PNG_BAD_TRANSPARENCY, chunk->offset,
                            "tRNS holds %" PRIu32 " bytes, and must hold %" PRIu32, chunk->length, samples * 2u);
        }
        for (uint32_t sample = 0u; sample < samples; sample += 1u) {
            const uint32_t value = png_read_u16(chunk->borrowed_data + sample * 2u);
            if ((value >> depth) != 0u) {
                return png_fail(error, PNG_BAD_TRANSPARENCY, chunk->offset,
                                "tRNS value %" PRIu32 " does not fit bit depth %" PRIu32, value, depth);
            }
        }
    }
    scan->borrowed_trns = chunk->borrowed_data;
    scan->trns_length = chunk->length;
    scan->info.has_alpha = 1u;
    return PNG_OK;
}

/* Step 1. On success every chunk is good, and the file ends exactly at the end of IEND. */
static png_status_t png_scan(const png_crc_table_t *table, const uint8_t *file, uint64_t length, png_scan_t *scan,
                             png_error_t *error) {
    static const uint8_t signature[PNG_SIGNATURE_BYTES] = {0x89u, 'P', 'N', 'G', '\r', '\n', 0x1Au, '\n'};
    memset(scan, 0, sizeof(*scan));
    if (length < PNG_SIGNATURE_BYTES) {
        return png_fail(error, PNG_TRUNCATED, length, "the file is only %" PRIu64 " bytes, too short for a PNG",
                        length);
    }
    if (memcmp(file, signature, PNG_SIGNATURE_BYTES) != 0) {
        return png_fail(error, PNG_BAD_SIGNATURE, 0u, "the file does not start with the PNG signature");
    }

    png_chunk_t chunk;
    png_status_t status = png_chunk_read(table, file, length, PNG_SIGNATURE_BYTES, &chunk, error);
    if (status != PNG_OK) {
        return status;
    }
    status = png_read_header(&chunk, &scan->info, error);
    if (status != PNG_OK) {
        return status;
    }

    bool idat_seen = false;
    bool idat_ended = false;
    uint64_t offset = chunk.next_offset;
    for (;;) {
        status = png_chunk_read(table, file, length, offset, &chunk, error);
        if (status != PNG_OK) {
            return status;
        }
        offset = chunk.next_offset;
        const uint32_t type = chunk.type;
        if (type == PNG_TYPE_IEND) {
            break;
        }
        if (type == PNG_TYPE_IDAT) {
            if (idat_ended) {
                return png_fail(error, PNG_BAD_ORDER, chunk.offset, "the IDAT chunks are not consecutive");
            }
            if (scan->info.color_type == PNG_COLOR_PALETTE && scan->borrowed_palette == nullptr) {
                return png_fail(error, PNG_BAD_PALETTE, chunk.offset, "a palette image has no PLTE before its IDAT");
            }
            if (!idat_seen) {
                scan->first_idat_offset = chunk.offset;
            }
            idat_seen = true;
            scan->info.idat_bytes += chunk.length;
            continue;
        }
        idat_ended = idat_seen;

        if (type == PNG_TYPE_IHDR) {
            return png_fail(error, PNG_BAD_ORDER, chunk.offset, "a second IHDR");
        }
        if (type == PNG_TYPE_PLTE) {
            if (idat_seen) {
                return png_fail(error, PNG_BAD_ORDER, chunk.offset, "PLTE comes after IDAT");
            }
            if (scan->borrowed_palette != nullptr) {
                return png_fail(error, PNG_BAD_ORDER, chunk.offset, "a second PLTE");
            }
            if (scan->borrowed_trns != nullptr) {
                return png_fail(error, PNG_BAD_ORDER, chunk.offset, "PLTE comes after tRNS");
            }
            status = png_read_palette(&chunk, scan, error);
        } else if (type == PNG_TYPE_TRNS) {
            if (idat_seen) {
                return png_fail(error, PNG_BAD_ORDER, chunk.offset, "tRNS comes after IDAT");
            }
            if (scan->borrowed_trns != nullptr) {
                return png_fail(error, PNG_BAD_ORDER, chunk.offset, "a second tRNS");
            }
            status = png_read_transparency(&chunk, scan, error);
        } else if (((type >> 24u) & 0x20u) == 0u) {
            /* Bit 5 of the first letter is clear: critical, so skipping it could give a wrong image. */
            char name[5];
            png_type_text(type, name);
            return png_fail(error, PNG_UNKNOWN_CRITICAL, chunk.offset, "unknown critical chunk %s", name);
        }
        if (status != PNG_OK) {
            return status;
        }
    }

    if (chunk.length != 0u) {
        return png_fail(error, PNG_BAD_ORDER, chunk.offset, "IEND holds %" PRIu32 " bytes, and must be empty",
                        chunk.length);
    }
    if (!idat_seen) {
        return png_fail(error, PNG_BAD_ORDER, chunk.offset, "the file has no IDAT chunk");
    }
    if (offset != length) {
        return png_fail(error, PNG_BAD_ORDER, offset, "%" PRIu64 " bytes follow IEND", length - offset);
    }
    if (scan->info.raw_bytes / PNG_DEFLATE_MAX_RATIO > scan->info.idat_bytes) {
        return png_fail(error, PNG_BAD_DATA, scan->first_idat_offset,
                        "%" PRIu64 " IDAT bytes cannot expand to %" PRIu64, scan->info.idat_bytes,
                        scan->info.raw_bytes);
    }
    scan->info.work_bytes = scan->info.raw_bytes + scan->info.idat_bytes;
    return PNG_OK;
}

png_status_t png_probe(const uint8_t *file, uint64_t length, png_info_t *info, png_error_t *error) {
    memset(error, 0, sizeof(*error));
    png_crc_table_t table;
    png_crc_table_fill(&table);
    png_scan_t scan;
    const png_status_t status = png_scan(&table, file, length, &scan, error);
    *info = scan.info;
    if (status != PNG_OK) {
        memset(info, 0, sizeof(*info));
    }
    return status;
}

static uint32_t png_paeth(uint32_t left, uint32_t up, uint32_t up_left) {
    const int32_t estimate = (int32_t)left + (int32_t)up - (int32_t)up_left;
    const int32_t to_left = estimate > (int32_t)left ? estimate - (int32_t)left : (int32_t)left - estimate;
    const int32_t to_up = estimate > (int32_t)up ? estimate - (int32_t)up : (int32_t)up - estimate;
    const int32_t to_up_left = estimate > (int32_t)up_left ? estimate - (int32_t)up_left : (int32_t)up_left - estimate;
    if (to_left <= to_up && to_left <= to_up_left) {
        return left;
    }
    return to_up <= to_up_left ? up : up_left;
}

/* Undoes one row's filter in place. previous is nullptr on the first row of a pass, where every byte above reads as 0. */
static void png_unfilter(uint8_t *row, const uint8_t *previous, uint64_t row_bytes, uint32_t pixel_bytes,
                         uint32_t filter) {
    for (uint64_t index = 0u; index < row_bytes; index += 1u) {
        const uint32_t left = index >= pixel_bytes ? row[index - pixel_bytes] : 0u;
        const uint32_t up = previous != nullptr ? previous[index] : 0u;
        const uint32_t up_left = previous != nullptr && index >= pixel_bytes ? previous[index - pixel_bytes] : 0u;
        uint32_t predicted = 0u;
        if (filter == 1u) {
            predicted = left;
        } else if (filter == 2u) {
            predicted = up;
        } else if (filter == 3u) {
            predicted = (left + up) / 2u;
        } else if (filter == 4u) {
            predicted = png_paeth(left, up, up_left);
        }
        row[index] = (uint8_t)(row[index] + predicted);
    }
}

/* One sample of a row, however wide. position counts samples, not pixels, and row_bytes has been proven to hold it. */
static uint32_t png_sample(const uint8_t *row, uint64_t position, uint32_t depth) {
    if (depth == 16u) {
        return png_read_u16(row + position * 2u);
    }
    if (depth == 8u) {
        return row[position];
    }
    const uint64_t bit = position * depth;
    const uint32_t shift = 8u - depth - (uint32_t)(bit % 8u);
    return ((uint32_t)row[bit / 8u] >> shift) & ((1u << depth) - 1u);
}

static uint8_t png_to_8_bits(uint32_t sample, uint32_t depth) {
    switch (depth) {
    case 1u:
        return (uint8_t)(sample * 255u);
    case 2u:
        return (uint8_t)(sample * 85u);
    case 4u:
        return (uint8_t)(sample * 17u);
    case 8u:
        return (uint8_t)sample;
    default:
        return (uint8_t)((sample * 255u + 32767u) / 65535u);
    }
}

/* One row of a pass to RGBA. The start and step come from the pass, so every write lands inside the image's row. */
static png_status_t png_convert_row(const png_scan_t *scan, const png_pass_t *pass, const uint8_t *row,
                                    uint32_t pass_width, uint8_t *target_row, png_error_t *error) {
    const uint32_t depth = scan->info.bit_depth;
    const uint32_t color_type = scan->info.color_type;
    const uint32_t channels = png_channels(color_type);
    for (uint32_t pixel = 0u; pixel < pass_width; pixel += 1u) {
        uint8_t *target = target_row + ((uint64_t)pass->start_x + (uint64_t)pixel * pass->step_x) * PNG_RGBA_BYTES;
        const uint64_t first = (uint64_t)pixel * channels;
        const uint32_t sample0 = png_sample(row, first, depth);
        if (color_type == PNG_COLOR_PALETTE) {
            if (sample0 >= scan->palette_entries) {
                return png_fail(error, PNG_BAD_INDEX, scan->first_idat_offset,
                                "a pixel uses palette index %" PRIu32 " of %" PRIu32 " entries", sample0,
                                scan->palette_entries);
            }
            const uint8_t *entry = scan->borrowed_palette + (uint64_t)sample0 * 3u;
            target[0] = entry[0];
            target[1] = entry[1];
            target[2] = entry[2];
            target[3] = sample0 < scan->trns_length ? scan->borrowed_trns[sample0] : 255u;
        } else if (color_type == PNG_COLOR_GRAY || color_type == PNG_COLOR_GRAY_ALPHA) {
            const uint8_t gray = png_to_8_bits(sample0, depth);
            target[0] = gray;
            target[1] = gray;
            target[2] = gray;
            if (color_type == PNG_COLOR_GRAY_ALPHA) {
                target[3] = png_to_8_bits(png_sample(row, first + 1u, depth), depth);
            } else {
                const bool is_key = scan->borrowed_trns != nullptr && sample0 == png_read_u16(scan->borrowed_trns);
                target[3] = is_key ? 0u : 255u;
            }
        } else {
            const uint32_t sample1 = png_sample(row, first + 1u, depth);
            const uint32_t sample2 = png_sample(row, first + 2u, depth);
            target[0] = png_to_8_bits(sample0, depth);
            target[1] = png_to_8_bits(sample1, depth);
            target[2] = png_to_8_bits(sample2, depth);
            if (color_type == PNG_COLOR_RGBA) {
                target[3] = png_to_8_bits(png_sample(row, first + 3u, depth), depth);
            } else {
                const bool is_key = scan->borrowed_trns != nullptr && sample0 == png_read_u16(scan->borrowed_trns) &&
                                    sample1 == png_read_u16(scan->borrowed_trns + 2u) &&
                                    sample2 == png_read_u16(scan->borrowed_trns + 4u);
                target[3] = is_key ? 0u : 255u;
            }
        }
    }
    return PNG_OK;
}

/* Step 4 for one pass. *raw is where the pass begins in the inflated data, and is moved past it. */
static png_status_t png_decode_pass(const png_scan_t *scan, const png_pass_t *pass, uint8_t **raw, uint8_t *pixels,
                                    png_error_t *error) {
    const png_info_t *info = &scan->info;
    const uint32_t pass_width = png_pass_extent(info->width, pass->start_x, pass->step_x);
    const uint32_t pass_height = png_pass_extent(info->height, pass->start_y, pass->step_y);
    if (pass_width == 0u || pass_height == 0u) {
        return PNG_OK;
    }
    const uint32_t pixel_bits = png_channels(info->color_type) * info->bit_depth;
    const uint32_t pixel_bytes = pixel_bits < 8u ? 1u : pixel_bits / 8u;
    const uint64_t row_bytes = png_row_bytes(pass_width, pixel_bits);
    const uint64_t stride = (uint64_t)info->width * PNG_RGBA_BYTES;

    uint8_t *cursor = *raw;
    const uint8_t *previous = nullptr;
    for (uint32_t row = 0u; row < pass_height; row += 1u) {
        const uint32_t filter = cursor[0];
        uint8_t *data = cursor + 1u;
        if (filter > 4u) {
            return png_fail(error, PNG_BAD_FILTER, scan->first_idat_offset,
                            "image row %" PRIu32 " has filter type %" PRIu32,
                            (uint32_t)pass->start_y + row * pass->step_y, filter);
        }
        png_unfilter(data, previous, row_bytes, pixel_bytes, filter);
        const uint64_t target_y = (uint64_t)pass->start_y + (uint64_t)row * pass->step_y;
        const png_status_t status = png_convert_row(scan, pass, data, pass_width, pixels + target_y * stride, error);
        if (status != PNG_OK) {
            return status;
        }
        previous = data;
        cursor = data + row_bytes;
    }
    *raw = cursor;
    return PNG_OK;
}

/* Steps 2 to 4, once png_scan has proven the file and png_decode the buffers. */
static png_status_t png_decode_checked(const png_crc_table_t *table, const uint8_t *file, uint64_t length,
                                       const png_scan_t *scan, uint8_t *pixels, uint8_t *work, png_error_t *error) {
    const png_info_t *info = &scan->info;
    uint8_t *joined = work + info->raw_bytes;
    uint64_t joined_used = 0u;
    uint64_t offset = PNG_SIGNATURE_BYTES;
    while (offset < length) {
        png_chunk_t chunk;
        const png_status_t chunk_status = png_chunk_read(table, file, length, offset, &chunk, error);
        if (chunk_status != PNG_OK) {
            return chunk_status;
        }
        if (chunk.type == PNG_TYPE_IDAT && chunk.length != 0u) {
            memcpy(joined + joined_used, chunk.borrowed_data, chunk.length);
            joined_used += chunk.length;
        }
        offset = chunk.next_offset;
    }

    inflate_result_t result;
    const inflate_status_t inflated = inflate_zlib(joined, joined_used, work, info->raw_bytes, &result);
    if (inflated != INFLATE_OK) {
        return png_fail(error, PNG_BAD_DATA, scan->first_idat_offset, "IDAT: %s", inflate_status_text(inflated));
    }
    if (result.output_used != info->raw_bytes) {
        return png_fail(error, PNG_BAD_DATA, scan->first_idat_offset,
                        "IDAT holds %" PRIu64 " bytes of image data, and %" PRIu64 " are needed", result.output_used,
                        info->raw_bytes);
    }

    uint8_t *raw = work;
    if (info->interlaced != 0u) {
        for (uint32_t index = 0u; index < PNG_ADAM7_PASSES; index += 1u) {
            const png_status_t status = png_decode_pass(scan, &PNG_ADAM7[index], &raw, pixels, error);
            if (status != PNG_OK) {
                return status;
            }
        }
        return PNG_OK;
    }
    return png_decode_pass(scan, &PNG_WHOLE_IMAGE, &raw, pixels, error);
}

png_status_t png_decode(const uint8_t *file, uint64_t length, uint8_t *pixels, uint64_t pixel_capacity, uint8_t *work,
                        uint64_t work_capacity, png_info_t *info, png_error_t *error) {
    memset(error, 0, sizeof(*error));
    if (info != nullptr) {
        memset(info, 0, sizeof(*info));
    }
    png_crc_table_t table;
    png_crc_table_fill(&table);
    png_scan_t scan;
    png_status_t status = png_scan(&table, file, length, &scan, error);
    if (status == PNG_OK && pixel_capacity < scan.info.pixel_bytes) {
        status = png_fail(error, PNG_NO_ROOM, 0u, "the image needs %" PRIu64 " pixel bytes and was given %" PRIu64,
                          scan.info.pixel_bytes, pixel_capacity);
    }
    if (status == PNG_OK && work_capacity < scan.info.work_bytes) {
        status = png_fail(error, PNG_NO_ROOM, 0u, "the image needs %" PRIu64 " work bytes and was given %" PRIu64,
                          scan.info.work_bytes, work_capacity);
    }
    if (status == PNG_OK) {
        status = png_decode_checked(&table, file, length, &scan, pixels, work, error);
    }
    if (status != PNG_OK) {
        if (pixel_capacity != 0u) {
            memset(pixels, 0, pixel_capacity);
        }
        return status;
    }
    if (info != nullptr) {
        *info = scan.info;
    }
    return PNG_OK;
}

const char *png_status_text(png_status_t status) {
    switch (status) {
    case PNG_OK:
        return "no error";
    case PNG_BAD_SIGNATURE:
        return "not a PNG file";
    case PNG_TRUNCATED:
        return "the file is cut short";
    case PNG_BAD_CHUNK:
        return "a chunk has an impossible length or type";
    case PNG_BAD_CRC:
        return "a chunk's CRC does not match";
    case PNG_BAD_HEADER:
        return "IHDR is invalid";
    case PNG_TOO_LARGE:
        return "the image is too large";
    case PNG_BAD_ORDER:
        return "chunks are missing, repeated or out of order";
    case PNG_UNKNOWN_CRITICAL:
        return "an unknown critical chunk";
    case PNG_BAD_PALETTE:
        return "PLTE is invalid, missing or forbidden";
    case PNG_BAD_TRANSPARENCY:
        return "tRNS is invalid or forbidden";
    case PNG_BAD_DATA:
        return "the image data does not inflate to the right size";
    case PNG_BAD_FILTER:
        return "a row has an invalid filter type";
    case PNG_BAD_INDEX:
        return "a pixel's palette index is past the end of PLTE";
    case PNG_NO_ROOM:
        return "the buffers given are too small";
    }
    return "unknown PNG status";
}
