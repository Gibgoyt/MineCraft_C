/*
 * DEFLATE and zlib decoding. See README.md. Written from RFC 1950 and RFC 1951.
 *
 * The bounds rules, since the input is untrusted:
 * - Input is only read through inflate_bits_refill and the stored-block copy, and both compare against input_length.
 * - Output is only written in inflate_block_codes and the stored-block copy, and both compare against the capacity
 *   before writing, in a form that cannot overflow.
 * - A match's source is only read after checking distance <= bytes written, so it stays inside what was written.
 * - A symbol from a table is only used as an index after a range check.
 */
#include "inflate/index.h"

#include <string.h>

enum : uint32_t {
    INFLATE_MAX_CODE_BITS = 15u,
    INFLATE_FAST_BITS = 9u,
    INFLATE_FAST_ENTRIES = 1u << INFLATE_FAST_BITS,
    INFLATE_LITLEN_SYMBOLS = 288u, /* the alphabet, though 286 and 287 are never valid */
    INFLATE_DIST_SYMBOLS = 32u,    /* the alphabet, though 30 and 31 are never valid */
    INFLATE_MAX_HLIT = 286u,       /* RFC 1951 3.2.7 */
    INFLATE_MAX_HDIST = 30u,       /* RFC 1951 3.2.7 */
    INFLATE_CODE_LENGTH_SYMBOLS = 19u,
    INFLATE_END_OF_BLOCK = 256u,
    INFLATE_FIRST_LENGTH_SYMBOL = 257u,
    INFLATE_LENGTH_SYMBOLS = 29u,
    INFLATE_VALID_DIST_SYMBOLS = 30u,
    INFLATE_ADLER_BASE = 65521u,
    INFLATE_ADLER_RUN = 5552u, /* the most bytes before the sums can overflow 32 bits */
    INFLATE_ZLIB_HEADER_BYTES = 2u,
    INFLATE_ZLIB_TRAILER_BYTES = 4u,
};

/* Canonical Huffman decoding tables. An entry of fast is symbol << 4 | code length, or 0 for a code longer than
 * INFLATE_FAST_BITS or no code at all, which the slow walk in inflate_huffman_decode tells apart. */
typedef struct inflate_huffman_t {
    uint16_t count[INFLATE_MAX_CODE_BITS + 1u]; /* how many codes have each length; count[0] is unused */
    uint16_t symbol[INFLATE_LITLEN_SYMBOLS];    /* symbols with codes, ordered by code */
    uint16_t fast[INFLATE_FAST_ENTRIES];        /* indexed by the next bits of input */
} inflate_huffman_t;
static_assert(sizeof(inflate_huffman_t) == 32u + 576u + 1024u);

typedef struct inflate_bits_t {
    const uint8_t *borrowed_input; /* 0 */
    uint64_t length;               /* 8 */
    uint64_t position;             /* 16: the next byte to load into buffer */
    uint64_t buffer;               /* 24: loaded and not used, lowest bit next */
    uint32_t count;                /* 32: how many bits of buffer are real */
    uint8_t _pad0[4];              /* 36 */
} inflate_bits_t;
static_assert(sizeof(inflate_bits_t) == 40);

typedef struct inflate_output_t {
    uint8_t *borrowed_data; /* 0 */
    uint64_t capacity;      /* 8 */
    uint64_t used;          /* 16 */
} inflate_output_t;
static_assert(sizeof(inflate_output_t) == 24);

static const uint16_t INFLATE_LENGTH_BASE[INFLATE_LENGTH_SYMBOLS] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t INFLATE_LENGTH_EXTRA[INFLATE_LENGTH_SYMBOLS] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t INFLATE_DIST_BASE[INFLATE_VALID_DIST_SYMBOLS] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t INFLATE_DIST_EXTRA[INFLATE_VALID_DIST_SYMBOLS] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
/* RFC 1951 3.2.7: the order code length code lengths are stored in. */
static const uint8_t INFLATE_CODE_LENGTH_ORDER[INFLATE_CODE_LENGTH_SYMBOLS] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                                               11, 4,  12, 3, 13, 2, 14, 1, 15};

/* Loads whole bytes while they fit and the input lasts. Only this function reads the input in a Huffman block. */
static void inflate_bits_refill(inflate_bits_t *bits) {
    while (bits->count <= 56u && bits->position < bits->length) {
        bits->buffer |= (uint64_t)bits->borrowed_input[bits->position] << bits->count;
        bits->position += 1u;
        bits->count += 8u;
    }
}

/* Takes count (at most 16) bits, lowest first. False if the input ends first; *value is then not written. */
static bool inflate_bits_take(inflate_bits_t *bits, uint32_t count, uint32_t *value) {
    if (bits->count < count) {
        inflate_bits_refill(bits);
        if (bits->count < count) {
            return false;
        }
    }
    *value = (uint32_t)(bits->buffer & ((1ull << count) - 1ull));
    bits->buffer >>= count;
    bits->count -= count;
    return true;
}

/* Drops the rest of a partly used byte and returns unused whole bytes to the input, for byte-aligned reading. */
static void inflate_bits_align(inflate_bits_t *bits) {
    bits->position -= bits->count / 8u;
    bits->buffer = 0u;
    bits->count = 0u;
}

static uint32_t inflate_reverse_bits(uint32_t code, uint32_t length) {
    uint32_t reversed = 0u;
    for (uint32_t bit = 0u; bit < length; bit += 1u) {
        reversed = (reversed << 1u) | ((code >> bit) & 1u);
    }
    return reversed;
}

/*
 * Builds tables from one code length per symbol. RFC 1951 3.2.2 defines the codes, and it says nothing on bad sets, so
 * this follows the rule zlib has made the de facto one. Over-subscribed is always refused. An incomplete set is refused
 * too, except that allow_single accepts a single code of one bit: the RFC itself (3.2.7) permits "one distance code of
 * one bit", and encoders do write it. It also accepts no codes at all, which then fails only if a symbol is decoded.
 */
static inflate_status_t inflate_huffman_build(inflate_huffman_t *huffman, const uint8_t *lengths, uint32_t symbols,
                                              bool allow_single) {
    memset(huffman, 0, sizeof(*huffman));
    for (uint32_t symbol = 0u; symbol < symbols; symbol += 1u) {
        huffman->count[lengths[symbol]] = (uint16_t)(huffman->count[lengths[symbol]] + 1u);
    }
    huffman->count[0] = 0u;

    int32_t left = 1;
    uint32_t total = 0u;
    for (uint32_t length = 1u; length <= INFLATE_MAX_CODE_BITS; length += 1u) {
        left = left * 2 - (int32_t)huffman->count[length];
        if (left < 0) {
            return INFLATE_BAD_HUFFMAN_SET;
        }
        total += huffman->count[length];
    }
    if (left > 0) {
        const bool single_of_one_bit = total == 1u && huffman->count[1] == 1u;
        if (!allow_single || (total != 0u && !single_of_one_bit)) {
            return INFLATE_BAD_HUFFMAN_SET;
        }
    }

    uint16_t offsets[INFLATE_MAX_CODE_BITS + 2u];
    uint32_t next_code[INFLATE_MAX_CODE_BITS + 2u];
    offsets[1] = 0u;
    next_code[1] = 0u;
    for (uint32_t length = 1u; length <= INFLATE_MAX_CODE_BITS; length += 1u) {
        offsets[length + 1u] = (uint16_t)(offsets[length] + huffman->count[length]);
        next_code[length + 1u] = (next_code[length] + huffman->count[length]) << 1u;
    }

    for (uint32_t symbol = 0u; symbol < symbols; symbol += 1u) {
        const uint32_t length = lengths[symbol];
        if (length == 0u) {
            continue;
        }
        huffman->symbol[offsets[length]] = (uint16_t)symbol;
        offsets[length] = (uint16_t)(offsets[length] + 1u);
        const uint32_t code = next_code[length];
        next_code[length] += 1u;
        if (length <= INFLATE_FAST_BITS) {
            const uint16_t entry = (uint16_t)((symbol << 4u) | length);
            for (uint32_t index = inflate_reverse_bits(code, length); index < INFLATE_FAST_ENTRIES;
                 index += 1u << length) {
                huffman->fast[index] = entry;
            }
        }
    }
    return INFLATE_OK;
}

/* Decodes one symbol. Missing bits read as zero in the peek, so a table hit still has to prove it had enough bits. */
static inflate_status_t inflate_huffman_decode(inflate_bits_t *bits, const inflate_huffman_t *huffman,
                                               uint32_t *symbol) {
    if (bits->count < INFLATE_MAX_CODE_BITS) {
        inflate_bits_refill(bits);
    }
    const uint32_t entry = huffman->fast[bits->buffer & (INFLATE_FAST_ENTRIES - 1u)];
    if (entry != 0u) {
        const uint32_t length = entry & 15u;
        if (length > bits->count) {
            return INFLATE_TRUNCATED;
        }
        bits->buffer >>= length;
        bits->count -= length;
        *symbol = entry >> 4u;
        return INFLATE_OK;
    }

    /* One bit at a time: the code so far, the first code of this length, and where this length's symbols begin. */
    int32_t code = 0;
    int32_t first = 0;
    int32_t index = 0;
    for (uint32_t length = 1u; length <= INFLATE_MAX_CODE_BITS; length += 1u) {
        if (length > bits->count) {
            return INFLATE_TRUNCATED;
        }
        code |= (int32_t)((bits->buffer >> (length - 1u)) & 1u);
        const int32_t count = (int32_t)huffman->count[length];
        if (code - count < first) {
            bits->buffer >>= length;
            bits->count -= length;
            *symbol = huffman->symbol[index + (code - first)];
            return INFLATE_OK;
        }
        index += count;
        first = (first + count) * 2;
        code *= 2;
    }
    return INFLATE_BAD_SYMBOL;
}

/* A stored block. The output's bounds are checked before the copy, and the copy uses the checked length. */
static inflate_status_t inflate_block_stored(inflate_bits_t *bits, inflate_output_t *output) {
    inflate_bits_align(bits);
    if (bits->length - bits->position < 4u) {
        bits->position = bits->length;
        return INFLATE_TRUNCATED;
    }
    const uint8_t *header = bits->borrowed_input + bits->position;
    const uint32_t length = (uint32_t)header[0] | ((uint32_t)header[1] << 8u);
    const uint32_t inverse = (uint32_t)header[2] | ((uint32_t)header[3] << 8u);
    if ((length ^ inverse) != 0xFFFFu) {
        return INFLATE_BAD_STORED_LENGTH;
    }
    bits->position += 4u;
    if (length > bits->length - bits->position) {
        bits->position = bits->length;
        return INFLATE_TRUNCATED;
    }
    if (length > output->capacity - output->used) {
        return INFLATE_NO_ROOM;
    }
    if (length != 0u) {
        memcpy(output->borrowed_data + output->used, bits->borrowed_input + bits->position, length);
    }
    bits->position += length;
    output->used += length;
    return INFLATE_OK;
}

/* The body of a fixed or dynamic block: literals and matches until the end-of-block symbol. */
static inflate_status_t inflate_block_codes(inflate_bits_t *bits, inflate_output_t *output,
                                            const inflate_huffman_t *litlen, const inflate_huffman_t *dist) {
    for (;;) {
        uint32_t symbol = 0u;
        inflate_status_t status = inflate_huffman_decode(bits, litlen, &symbol);
        if (status != INFLATE_OK) {
            return status;
        }
        if (symbol < INFLATE_END_OF_BLOCK) {
            if (output->used >= output->capacity) {
                return INFLATE_NO_ROOM;
            }
            output->borrowed_data[output->used] = (uint8_t)symbol;
            output->used += 1u;
            continue;
        }
        if (symbol == INFLATE_END_OF_BLOCK) {
            return INFLATE_OK;
        }

        const uint32_t length_symbol = symbol - INFLATE_FIRST_LENGTH_SYMBOL;
        if (length_symbol >= INFLATE_LENGTH_SYMBOLS) {
            return INFLATE_BAD_SYMBOL;
        }
        uint32_t extra = 0u;
        if (!inflate_bits_take(bits, INFLATE_LENGTH_EXTRA[length_symbol], &extra)) {
            return INFLATE_TRUNCATED;
        }
        const uint64_t length = (uint64_t)INFLATE_LENGTH_BASE[length_symbol] + extra;

        uint32_t dist_symbol = 0u;
        status = inflate_huffman_decode(bits, dist, &dist_symbol);
        if (status != INFLATE_OK) {
            return status;
        }
        if (dist_symbol >= INFLATE_VALID_DIST_SYMBOLS) {
            return INFLATE_BAD_SYMBOL;
        }
        if (!inflate_bits_take(bits, INFLATE_DIST_EXTRA[dist_symbol], &extra)) {
            return INFLATE_TRUNCATED;
        }
        const uint64_t distance = (uint64_t)INFLATE_DIST_BASE[dist_symbol] + extra;

        if (distance > output->used) {
            return INFLATE_BAD_DISTANCE;
        }
        if (length > output->capacity - output->used) {
            return INFLATE_NO_ROOM;
        }
        /* A byte at a time and forwards: a match may overlap itself, and that is how runs are written. */
        uint8_t *target = output->borrowed_data + output->used;
        const uint8_t *source = target - distance;
        for (uint64_t index = 0u; index < length; index += 1u) {
            target[index] = source[index];
        }
        output->used += length;
    }
}

/* RFC 1951 3.2.6. */
static inflate_status_t inflate_block_fixed(inflate_bits_t *bits, inflate_output_t *output) {
    uint8_t lengths[INFLATE_LITLEN_SYMBOLS];
    for (uint32_t symbol = 0u; symbol < INFLATE_LITLEN_SYMBOLS; symbol += 1u) {
        lengths[symbol] = (uint8_t)(symbol < 144u ? 8u : symbol < 256u ? 9u : symbol < 280u ? 7u : 8u);
    }
    inflate_huffman_t litlen;
    inflate_status_t status = inflate_huffman_build(&litlen, lengths, INFLATE_LITLEN_SYMBOLS, false);
    if (status != INFLATE_OK) {
        return status;
    }
    memset(lengths, 5, INFLATE_DIST_SYMBOLS);
    inflate_huffman_t dist;
    status = inflate_huffman_build(&dist, lengths, INFLATE_DIST_SYMBOLS, false);
    if (status != INFLATE_OK) {
        return status;
    }
    return inflate_block_codes(bits, output, &litlen, &dist);
}

/* RFC 1951 3.2.7. */
static inflate_status_t inflate_block_dynamic(inflate_bits_t *bits, inflate_output_t *output) {
    uint32_t hlit = 0u;
    uint32_t hdist = 0u;
    uint32_t hclen = 0u;
    if (!inflate_bits_take(bits, 5u, &hlit) || !inflate_bits_take(bits, 5u, &hdist) ||
        !inflate_bits_take(bits, 4u, &hclen)) {
        return INFLATE_TRUNCATED;
    }
    hlit += 257u;
    hdist += 1u;
    hclen += 4u;
    if (hlit > INFLATE_MAX_HLIT || hdist > INFLATE_MAX_HDIST) {
        return INFLATE_BAD_CODE_LENGTHS;
    }

    uint8_t lengths[INFLATE_LITLEN_SYMBOLS + INFLATE_DIST_SYMBOLS];
    memset(lengths, 0, INFLATE_CODE_LENGTH_SYMBOLS);
    for (uint32_t index = 0u; index < hclen; index += 1u) {
        uint32_t value = 0u;
        if (!inflate_bits_take(bits, 3u, &value)) {
            return INFLATE_TRUNCATED;
        }
        lengths[INFLATE_CODE_LENGTH_ORDER[index]] = (uint8_t)value;
    }
    inflate_huffman_t code_lengths;
    inflate_status_t status = inflate_huffman_build(&code_lengths, lengths, INFLATE_CODE_LENGTH_SYMBOLS, false);
    if (status != INFLATE_OK) {
        return status;
    }

    const uint32_t total = hlit + hdist;
    uint32_t filled = 0u;
    while (filled < total) {
        uint32_t symbol = 0u;
        status = inflate_huffman_decode(bits, &code_lengths, &symbol);
        if (status != INFLATE_OK) {
            return status;
        }
        if (symbol < 16u) {
            lengths[filled] = (uint8_t)symbol;
            filled += 1u;
            continue;
        }
        uint8_t repeated = 0u;
        uint32_t repeat = 0u;
        uint32_t extra = 0u;
        if (symbol == 16u) {
            if (filled == 0u) {
                return INFLATE_BAD_CODE_LENGTHS; /* nothing to repeat */
            }
            repeated = lengths[filled - 1u];
            if (!inflate_bits_take(bits, 2u, &extra)) {
                return INFLATE_TRUNCATED;
            }
            repeat = 3u + extra;
        } else if (symbol == 17u) {
            if (!inflate_bits_take(bits, 3u, &extra)) {
                return INFLATE_TRUNCATED;
            }
            repeat = 3u + extra;
        } else {
            if (!inflate_bits_take(bits, 7u, &extra)) {
                return INFLATE_TRUNCATED;
            }
            repeat = 11u + extra;
        }
        if (repeat > total - filled) {
            return INFLATE_BAD_CODE_LENGTHS; /* runs past the end, so it could write past the array */
        }
        memset(lengths + filled, repeated, repeat);
        filled += repeat;
    }

    if (lengths[INFLATE_END_OF_BLOCK] == 0u) {
        return INFLATE_MISSING_END_CODE;
    }
    inflate_huffman_t litlen;
    status = inflate_huffman_build(&litlen, lengths, hlit, true);
    if (status != INFLATE_OK) {
        return status;
    }
    inflate_huffman_t dist;
    status = inflate_huffman_build(&dist, lengths + hlit, hdist, true);
    if (status != INFLATE_OK) {
        return status;
    }
    return inflate_block_codes(bits, output, &litlen, &dist);
}

static inflate_status_t inflate_stream(inflate_bits_t *bits, inflate_output_t *output) {
    uint32_t is_final = 0u;
    while (is_final == 0u) {
        uint32_t block_type = 0u;
        if (!inflate_bits_take(bits, 1u, &is_final) || !inflate_bits_take(bits, 2u, &block_type)) {
            return INFLATE_TRUNCATED;
        }
        inflate_status_t status = INFLATE_BAD_BLOCK_TYPE;
        if (block_type == 0u) {
            status = inflate_block_stored(bits, output);
        } else if (block_type == 1u) {
            status = inflate_block_fixed(bits, output);
        } else if (block_type == 2u) {
            status = inflate_block_dynamic(bits, output);
        }
        if (status != INFLATE_OK) {
            return status;
        }
    }
    return INFLATE_OK;
}

inflate_status_t inflate_raw(const uint8_t *input, uint64_t input_length, uint8_t *output, uint64_t output_capacity,
                             inflate_result_t *result) {
    inflate_bits_t bits = {.borrowed_input = input, .length = input_length};
    inflate_output_t out = {.borrowed_data = output, .capacity = output_capacity};
    const inflate_status_t status = inflate_stream(&bits, &out);
    /* Whole bytes still in the buffer were never used, so they go back. A partly used byte counts as read. */
    result->input_used = bits.position - bits.count / 8u;
    result->output_used = out.used;
    return status;
}

/* RFC 1950 8.2. */
static uint32_t inflate_adler32(const uint8_t *data, uint64_t length) {
    uint32_t low = 1u;
    uint32_t high = 0u;
    uint64_t position = 0u;
    while (position < length) {
        const uint64_t left = length - position;
        const uint64_t run = left < INFLATE_ADLER_RUN ? left : INFLATE_ADLER_RUN;
        for (uint64_t index = 0u; index < run; index += 1u) {
            low += data[position + index];
            high += low;
        }
        low %= INFLATE_ADLER_BASE;
        high %= INFLATE_ADLER_BASE;
        position += run;
    }
    return (high << 16u) | low;
}

inflate_status_t inflate_zlib(const uint8_t *input, uint64_t input_length, uint8_t *output, uint64_t output_capacity,
                              inflate_result_t *result) {
    result->input_used = input_length < INFLATE_ZLIB_HEADER_BYTES ? input_length : INFLATE_ZLIB_HEADER_BYTES;
    result->output_used = 0u;
    if (input_length < INFLATE_ZLIB_HEADER_BYTES) {
        return INFLATE_TRUNCATED;
    }
    const uint32_t method = input[0];
    const uint32_t flags = input[1];
    const bool is_deflate = (method & 15u) == 8u;
    const bool window_fits = (method >> 4u) <= 7u;
    const bool check_holds = (method * 256u + flags) % 31u == 0u;
    const bool no_dictionary = (flags & 0x20u) == 0u;
    if (!is_deflate || !window_fits || !check_holds || !no_dictionary) {
        return INFLATE_BAD_ZLIB_HEADER;
    }

    const inflate_status_t status = inflate_raw(
        input + INFLATE_ZLIB_HEADER_BYTES, input_length - INFLATE_ZLIB_HEADER_BYTES, output, output_capacity, result);
    result->input_used += INFLATE_ZLIB_HEADER_BYTES;
    if (status != INFLATE_OK) {
        return status;
    }

    const uint64_t left = input_length - result->input_used;
    if (left < INFLATE_ZLIB_TRAILER_BYTES) {
        result->input_used = input_length;
        return INFLATE_TRUNCATED;
    }
    const uint8_t *trailer = input + result->input_used;
    const uint32_t expected = ((uint32_t)trailer[0] << 24u) | ((uint32_t)trailer[1] << 16u) |
                              ((uint32_t)trailer[2] << 8u) | (uint32_t)trailer[3];
    result->input_used += INFLATE_ZLIB_TRAILER_BYTES;
    if (inflate_adler32(output, result->output_used) != expected) {
        return INFLATE_BAD_CHECKSUM;
    }
    if (left != INFLATE_ZLIB_TRAILER_BYTES) {
        return INFLATE_TRAILING_DATA;
    }
    return INFLATE_OK;
}

const char *inflate_status_text(inflate_status_t status) {
    switch (status) {
    case INFLATE_OK:
        return "no error";
    case INFLATE_TRUNCATED:
        return "the input ends inside the stream";
    case INFLATE_NO_ROOM:
        return "the stream decodes to more bytes than expected";
    case INFLATE_BAD_BLOCK_TYPE:
        return "a block has the reserved type 3";
    case INFLATE_BAD_STORED_LENGTH:
        return "a stored block's LEN and NLEN disagree";
    case INFLATE_BAD_CODE_LENGTHS:
        return "a dynamic block's code lengths are malformed";
    case INFLATE_BAD_HUFFMAN_SET:
        return "a Huffman code is over-subscribed or incomplete";
    case INFLATE_MISSING_END_CODE:
        return "a dynamic block has no end-of-block code";
    case INFLATE_BAD_SYMBOL:
        return "the stream holds an invalid symbol";
    case INFLATE_BAD_DISTANCE:
        return "a match reaches back before the start of the output";
    case INFLATE_BAD_ZLIB_HEADER:
        return "the zlib header is invalid or asks for a preset dictionary";
    case INFLATE_BAD_CHECKSUM:
        return "the Adler-32 checksum does not match";
    case INFLATE_TRAILING_DATA:
        return "bytes follow the end of the zlib stream";
    }
    return "unknown inflate status";
}
