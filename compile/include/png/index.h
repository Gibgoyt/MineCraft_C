/*
 * Our own bounds-checked PNG decoder. See compile/src/png/README.md. Packs are untrusted input, so a file is either
 * decoded completely and correctly or refused with a clear error; no file can make it touch memory outside the buffers
 * it was given. It never allocates: call png_probe to learn what a file needs, then png_decode with that much memory.
 */
#ifndef PNG_INDEX_H
#define PNG_INDEX_H

#include <stdint.h>

/* The widest or tallest image accepted. It keeps every size sum well inside 64 bits. PNG itself allows 2^31 - 1. */
#define PNG_MAX_DIMENSION 32768u
#define PNG_ERROR_MESSAGE_BYTES 112u

typedef enum png_status_t : uint8_t {
    PNG_OK = 0,
    PNG_BAD_SIGNATURE = 1,
    PNG_TRUNCATED = 2,         /* the file ends inside a chunk, or before IEND */
    PNG_BAD_CHUNK = 3,         /* an impossible chunk length or type */
    PNG_BAD_CRC = 4,           /* any chunk, ancillary or not */
    PNG_BAD_HEADER = 5,        /* IHDR: its size, zero dimensions, or a field or combination PNG does not define */
    PNG_TOO_LARGE = 6,         /* over PNG_MAX_DIMENSION */
    PNG_BAD_ORDER = 7,         /* chunks missing, repeated, or out of order, or anything after IEND */
    PNG_UNKNOWN_CRITICAL = 8,  /* a critical chunk this decoder does not know */
    PNG_BAD_PALETTE = 9,       /* PLTE: its size, missing when needed, or present when forbidden */
    PNG_BAD_TRANSPARENCY = 10, /* tRNS: its size, its values, or present when forbidden */
    PNG_BAD_DATA = 11,         /* the IDAT stream does not inflate to exactly the image's bytes */
    PNG_BAD_FILTER = 12,       /* a row with a filter type above 4 */
    PNG_BAD_INDEX = 13,        /* a pixel with an index past the end of the palette */
    PNG_NO_ROOM = 14,          /* the buffers given to png_decode are smaller than png_probe said */
} png_status_t;

typedef enum png_color_type_t : uint8_t {
    PNG_COLOR_GRAY = 0,
    PNG_COLOR_RGB = 2,
    PNG_COLOR_PALETTE = 3,
    PNG_COLOR_GRAY_ALPHA = 4,
    PNG_COLOR_RGBA = 6,
} png_color_type_t;

typedef struct png_error_t {
    uint64_t offset;                       /* 0: file offset of the chunk at fault; the first IDAT for image data */
    uint8_t status;                        /* 8: a png_status_t; PNG_OK means no error */
    uint8_t _pad0[7];                      /* 9 */
    char message[PNG_ERROR_MESSAGE_BYTES]; /* 16: NUL-terminated */
} png_error_t;
static_assert(sizeof(png_error_t) == 128);

typedef struct png_info_t {
    uint64_t pixel_bytes; /* 0: what png_decode's pixels must hold: width * height * 4 */
    uint64_t work_bytes;  /* 8: what png_decode's work must hold: raw_bytes + idat_bytes */
    uint64_t raw_bytes;   /* 16: the size of the inflated image data, filter bytes included */
    uint64_t idat_bytes;  /* 24: the total payload of the IDAT chunks */
    uint32_t width;       /* 32 */
    uint32_t height;      /* 36 */
    uint8_t bit_depth;    /* 40: 1, 2, 4, 8 or 16 */
    uint8_t color_type;   /* 41: a png_color_type_t */
    uint8_t interlaced;   /* 42: 1 for Adam7 */
    uint8_t has_alpha;    /* 43: 1 if the file has an alpha channel or a tRNS chunk */
    uint8_t _pad0[4];     /* 44 */
} png_info_t;
static_assert(sizeof(png_info_t) == 48);

/*
 * Checks a file's whole chunk structure and every CRC, and fills info. It inflates nothing, so it is cheap, and it runs
 * before a caller commits memory: a file whose IDAT chunks could not possibly expand to its claimed size is refused here
 * with PNG_BAD_DATA, so a tiny file cannot ask for gigabytes. A caller still decides its own limit on info->pixel_bytes.
 * On failure returns the status, sets error, and zeroes info. A success also clears error.
 */
png_status_t png_probe(const uint8_t *file, uint64_t length, png_info_t *info, png_error_t *error);

/*
 * Decodes a file to 8-bit RGBA, not premultiplied, top row first, with no gap between rows: pixels[(y * width + x) * 4].
 * 16-bit samples are scaled to 8 bits with rounding; lower depths are scaled so that their maximum becomes 255. tRNS
 * becomes alpha. The stored values are returned as they are: gAMA, sRGB, cHRM and iCCP are ignored, as other ancillary
 * chunks are, apart from their CRC.
 *
 * pixels and work are written, must not overlap each other or file, and need the sizes png_probe reports; pixels needs no
 * alignment. A failure returns the status, sets error, and zeroes whatever pixels it was given, so half an image cannot
 * be used by mistake. info may be nullptr.
 */
png_status_t png_decode(const uint8_t *file, uint64_t length, uint8_t *pixels, uint64_t pixel_capacity, uint8_t *work,
                        uint64_t work_capacity, png_info_t *info, png_error_t *error);

/* One sentence about a status. Never nullptr, for any value. */
const char *png_status_text(png_status_t status);

#endif
