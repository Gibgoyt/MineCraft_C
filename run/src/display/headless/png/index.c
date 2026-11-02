/* See README.md. The whole format is written out here, with nothing compressed: signature, IHDR, one IDAT holding a zlib
 * stream of stored blocks, IEND. */
#include "display/headless/png/index.h"

#include <string.h>

enum : uint32_t {
    PNG_CRC_POLYNOMIAL = 0xedb88320u, /* reflected, as PNG specifies */
    PNG_ADLER_MODULUS = 65521u,
    /* The most bytes adler can take before its 32-bit sums could overflow: the figure from RFC 1950's own code. */
    PNG_ADLER_RUN = 5552u,
    PNG_STORED_BLOCK_MAX = 65535u,
    PNG_STORED_HEADER_BYTES = 5u,
    PNG_SIGNATURE_BYTES = 8u,
    PNG_CHUNK_OVERHEAD_BYTES = 12u, /* length, type, CRC */
    PNG_IHDR_BYTES = 13u,
    PNG_ZLIB_HEADER_BYTES = 2u,
    PNG_ZLIB_TRAILER_BYTES = 4u,
    PNG_RGB_BYTES = 3u,
    PNG_XRGB_BYTES = 4u,
};

static const uint8_t png_signature[PNG_SIGNATURE_BYTES] = {0x89u, 'P', 'N', 'G', '\r', '\n', 0x1au, '\n'};

uint32_t display_png_crc32(const uint8_t *bytes, uint64_t length) {
    uint32_t table[256];
    for (uint32_t i = 0; i < 256u; i++) {
        uint32_t value = i;
        for (uint32_t bit = 0; bit < 8u; bit++) {
            value = (value & 1u) != 0u ? PNG_CRC_POLYNOMIAL ^ (value >> 1u) : value >> 1u;
        }
        table[i] = value;
    }
    uint32_t crc = 0xffffffffu;
    for (uint64_t i = 0; i < length; i++) {
        crc = table[(crc ^ bytes[i]) & 0xffu] ^ (crc >> 8u);
    }
    return crc ^ 0xffffffffu;
}

uint32_t display_png_adler32(const uint8_t *bytes, uint64_t length) {
    uint32_t low = 1u;
    uint32_t high = 0u;
    uint64_t at = 0;
    while (at < length) {
        const uint64_t left = length - at;
        const uint64_t run = left < PNG_ADLER_RUN ? left : PNG_ADLER_RUN;
        for (uint64_t i = 0; i < run; i++) {
            low += bytes[at + i];
            high += low;
        }
        low %= PNG_ADLER_MODULUS;
        high %= PNG_ADLER_MODULUS;
        at += run;
    }
    return high << 16u | low;
}

static uint64_t png_raw_bytes(uint32_t width, uint32_t height) {
    return ((uint64_t)width * PNG_RGB_BYTES + 1u) * height; /* each row starts with its filter byte */
}

static uint64_t png_zlib_bytes(uint64_t raw_bytes) {
    const uint64_t blocks = (raw_bytes + PNG_STORED_BLOCK_MAX - 1u) / PNG_STORED_BLOCK_MAX;
    return PNG_ZLIB_HEADER_BYTES + blocks * PNG_STORED_HEADER_BYTES + raw_bytes + PNG_ZLIB_TRAILER_BYTES;
}

uint64_t display_png_bytes_needed(uint32_t width, uint32_t height) {
    if (width == 0u || height == 0u || width > DISPLAY_PNG_MAX_DIMENSION || height > DISPLAY_PNG_MAX_DIMENSION) {
        return 0u;
    }
    return PNG_SIGNATURE_BYTES + PNG_CHUNK_OVERHEAD_BYTES + PNG_IHDR_BYTES + PNG_CHUNK_OVERHEAD_BYTES +
           png_zlib_bytes(png_raw_bytes(width, height)) + PNG_CHUNK_OVERHEAD_BYTES;
}

static void png_put_u32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value >> 24u);
    out[1] = (uint8_t)(value >> 16u);
    out[2] = (uint8_t)(value >> 8u);
    out[3] = (uint8_t)value;
}

/* Writes a chunk's length and type around a payload the caller has already put in place, and its CRC after it. */
static uint64_t png_chunk_close(uint8_t *chunk, const char *type, uint64_t payload_bytes) {
    png_put_u32(chunk, (uint32_t)payload_bytes);
    memcpy(chunk + 4u, type, 4u);
    png_put_u32(chunk + 8u + payload_bytes, display_png_crc32(chunk + 4u, 4u + payload_bytes));
    return PNG_CHUNK_OVERHEAD_BYTES + payload_bytes;
}

/* The stored-block stream, a byte at a time: rows and block boundaries fall in different places. */
typedef struct png_stored_t {
    uint8_t *out;
    uint64_t at;
    uint64_t raw_left;
    uint32_t block_left;
    uint32_t adler_low;
    uint32_t adler_high;
    uint32_t adler_run;
} png_stored_t;
static_assert(sizeof(png_stored_t) == 40);

static void png_stored_put(png_stored_t *stored, uint8_t byte) {
    if (stored->block_left == 0u) {
        const uint32_t block =
            stored->raw_left < PNG_STORED_BLOCK_MAX ? (uint32_t)stored->raw_left : PNG_STORED_BLOCK_MAX;
        uint8_t *const header = stored->out + stored->at;
        header[0] = stored->raw_left <= PNG_STORED_BLOCK_MAX ? 1u : 0u; /* BFINAL, and BTYPE 00 */
        header[1] = (uint8_t)block;
        header[2] = (uint8_t)(block >> 8u);
        header[3] = (uint8_t)~block;
        header[4] = (uint8_t)(~block >> 8u);
        stored->at += PNG_STORED_HEADER_BYTES;
        stored->block_left = block;
    }
    stored->out[stored->at] = byte;
    stored->at++;
    stored->block_left--;
    stored->raw_left--;
    stored->adler_low += byte;
    stored->adler_high += stored->adler_low;
    stored->adler_run++;
    if (stored->adler_run == PNG_ADLER_RUN) {
        stored->adler_low %= PNG_ADLER_MODULUS;
        stored->adler_high %= PNG_ADLER_MODULUS;
        stored->adler_run = 0u;
    }
}

bool display_png_encode(const uint8_t *pixels, uint32_t pitch, uint32_t width, uint32_t height, uint8_t *out,
                        uint64_t capacity, uint64_t *out_bytes) {
    const uint64_t needed = display_png_bytes_needed(width, height);
    if (needed == 0u || capacity < needed || (uint64_t)pitch < (uint64_t)width * PNG_XRGB_BYTES) {
        return false;
    }
    memcpy(out, png_signature, PNG_SIGNATURE_BYTES);
    uint64_t at = PNG_SIGNATURE_BYTES;

    uint8_t *const ihdr = out + at + 8u;
    png_put_u32(ihdr, width);
    png_put_u32(ihdr + 4u, height);
    ihdr[8] = 8u;  /* bit depth */
    ihdr[9] = 2u;  /* colour type: RGB */
    ihdr[10] = 0u; /* compression: DEFLATE, the only one there is */
    ihdr[11] = 0u; /* filter method: the only one there is */
    ihdr[12] = 0u; /* not interlaced */
    at += png_chunk_close(out + at, "IHDR", PNG_IHDR_BYTES);

    uint8_t *const idat = out + at;
    uint8_t *const zlib = idat + 8u;
    zlib[0] = 0x78u; /* DEFLATE with a 32 KiB window */
    zlib[1] = 0x01u; /* no dictionary, "fastest", and the check bits that make the pair divide by 31 */
    png_stored_t stored = {.out = zlib,
                           .at = PNG_ZLIB_HEADER_BYTES,
                           .raw_left = png_raw_bytes(width, height),
                           .block_left = 0u,
                           .adler_low = 1u,
                           .adler_high = 0u,
                           .adler_run = 0u};
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *const row = pixels + (uint64_t)y * pitch;
        png_stored_put(&stored, 0u); /* filter: none */
        for (uint32_t x = 0; x < width; x++) {
            const uint8_t *const pixel = row + (uint64_t)x * PNG_XRGB_BYTES;
            png_stored_put(&stored, pixel[2]); /* XRGB8888 is B, G, R, X in memory on this CPU */
            png_stored_put(&stored, pixel[1]);
            png_stored_put(&stored, pixel[0]);
        }
    }
    const uint32_t adler = (stored.adler_high % PNG_ADLER_MODULUS) << 16u | (stored.adler_low % PNG_ADLER_MODULUS);
    png_put_u32(zlib + stored.at, adler);
    at += png_chunk_close(idat, "IDAT", stored.at + PNG_ZLIB_TRAILER_BYTES);
    at += png_chunk_close(out + at, "IEND", 0u);
    *out_bytes = at;
    return true;
}
