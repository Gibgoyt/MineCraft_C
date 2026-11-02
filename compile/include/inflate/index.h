/*
 * Our own DEFLATE (RFC 1951) and zlib (RFC 1950) decoder. See compile/src/inflate/README.md. The input is untrusted:
 * every read and write is bounds-checked, and no input can make it touch memory outside the two buffers it was given.
 * It never allocates.
 */
#ifndef INFLATE_INDEX_H
#define INFLATE_INDEX_H

#include <stdint.h>

typedef enum inflate_status_t : uint8_t {
    INFLATE_OK = 0,
    INFLATE_TRUNCATED = 1,         /* the input ended inside the stream */
    INFLATE_NO_ROOM = 2,           /* the stream decodes to more than output_capacity bytes */
    INFLATE_BAD_BLOCK_TYPE = 3,    /* BTYPE 3, which is reserved */
    INFLATE_BAD_STORED_LENGTH = 4, /* LEN and NLEN of a stored block disagree */
    INFLATE_BAD_CODE_LENGTHS = 5,  /* a dynamic header with too many codes, or a repeat it cannot do */
    INFLATE_BAD_HUFFMAN_SET = 6,   /* code lengths that over-subscribe, or leave a code incomplete */
    INFLATE_MISSING_END_CODE = 7,  /* a dynamic block that gives symbol 256 no code */
    INFLATE_BAD_SYMBOL = 8,        /* a bit pattern with no symbol, or length 286-287, or distance 30-31 */
    INFLATE_BAD_DISTANCE = 9,      /* a match reaching back before the start of the output */
    INFLATE_BAD_ZLIB_HEADER = 10,  /* wrong method, window, check bits, or a preset dictionary */
    INFLATE_BAD_CHECKSUM = 11,     /* the Adler-32 does not match what was decoded */
    INFLATE_TRAILING_DATA = 12,    /* bytes left after the end of a zlib stream */
} inflate_status_t;

/* What happened, valid on success and on failure: on failure, how far it got. */
typedef struct inflate_result_t {
    uint64_t input_used;  /* 0: whole bytes read; a last, partly used byte counts */
    uint64_t output_used; /* 8: bytes written */
} inflate_result_t;
static_assert(sizeof(inflate_result_t) == 16);

/*
 * Decodes one raw DEFLATE stream into output. Bytes after the final block are left alone, since a raw stream does not
 * say what follows it; result->input_used says where they begin. output may be nullptr when output_capacity is 0, and
 * input may be nullptr when input_length is 0. On failure the contents of output are not meaningful.
 */
inflate_status_t inflate_raw(const uint8_t *input, uint64_t input_length, uint8_t *output, uint64_t output_capacity,
                             inflate_result_t *result);

/* The same for a zlib stream: checks the header and the Adler-32, and refuses bytes after them. */
inflate_status_t inflate_zlib(const uint8_t *input, uint64_t input_length, uint8_t *output, uint64_t output_capacity,
                              inflate_result_t *result);

/* One sentence about a status, for error messages. Never nullptr, for any value. */
const char *inflate_status_text(inflate_status_t status);

#endif
