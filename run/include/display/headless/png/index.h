/* See run/src/display/headless/png/README.md. */
#ifndef MC_RUN_DISPLAY_HEADLESS_PNG_INDEX_H
#define MC_RUN_DISPLAY_HEADLESS_PNG_INDEX_H

#include <stdint.h>

enum : uint32_t {
    /* The widest or tallest image written. It keeps the one IDAT chunk under PNG's limit of 2^31 - 1 bytes. */
    DISPLAY_PNG_MAX_DIMENSION = 16384u,
};

/* The checksums PNG and zlib call for, with their usual starting values already applied. */
uint32_t display_png_crc32(const uint8_t *bytes, uint64_t length);
uint32_t display_png_adler32(const uint8_t *bytes, uint64_t length);

/* The exact size of the file display_png_encode writes, or 0 for a size it will not write: a dimension of 0 or past
 * DISPLAY_PNG_MAX_DIMENSION. */
uint64_t display_png_bytes_needed(uint32_t width, uint32_t height);

/* Writes an 8-bit RGB PNG of XRGB8888 pixels into out: filter 0 on every row and stored DEFLATE blocks, so the file is
 * big and quick to make, and the same pixels always make the same bytes. Rows are pitch bytes apart and X is dropped. On
 * false (see display_png_bytes_needed, or capacity is too small, or pitch cannot hold a row) nothing was written. */
bool display_png_encode(const uint8_t *pixels, uint32_t pitch, uint32_t width, uint32_t height, uint8_t *out,
                        uint64_t capacity, uint64_t *out_bytes);

#endif
