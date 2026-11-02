/*
 * ctest compile_inflate. See README.md. The one argument, from test.cmake, is the fixtures directory.
 */
#include "harness/index.h"
#include "inflate/index.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum : uint32_t {
    ITEST_PAGE_BYTES = 4096u,
    ITEST_STREAM_BYTES = 1u << 20,
    ITEST_LITLEN_SYMBOLS = 288u,
    ITEST_CODE_LENGTH_SYMBOLS = 19u,
    ITEST_FIXTURES_EXPECTED = 56u,
    ITEST_MUTATION_ROUNDS = 40000u,
};

static uint64_t itest_fnv1a(const uint8_t *data, uint64_t length) {
    uint64_t value = 0xCBF29CE484222325ull;
    for (uint64_t index = 0u; index < length; index += 1u) {
        value = (value ^ data[index]) * 0x100000001B3ull;
    }
    return value;
}

/* Written independently of the decoder, from RFC 1950 8.2, so that a test stream checks its Adler-32. */
static uint32_t itest_adler32(const uint8_t *data, uint64_t length) {
    uint32_t low = 1u;
    uint32_t high = 0u;
    for (uint64_t index = 0u; index < length; index += 1u) {
        low = (low + data[index]) % 65521u;
        high = (high + low) % 65521u;
    }
    return (high << 16u) | low;
}

/* ---- Guarded memory: a byte read or written past the end of a buffer hits a page that faults, and the test dies. ---- */

typedef struct itest_guarded_t {
    uint8_t *block;    /* 0: page aligned */
    uint8_t *data;     /* 8: ends flush against the guard page */
    uint64_t capacity; /* 16 */
    uint64_t rounded;  /* 24: the bytes of block before the guard page */
} itest_guarded_t;
static_assert(sizeof(itest_guarded_t) == 32);

static itest_guarded_t itest_guarded_make(test_context_t *t, uint64_t capacity) {
    itest_guarded_t guarded;
    const uint64_t pages = (capacity + ITEST_PAGE_BYTES - 1u) / ITEST_PAGE_BYTES + 1u;
    guarded.rounded = pages * ITEST_PAGE_BYTES;
    uint8_t *raw = test_alloc(t, guarded.rounded + 2u * ITEST_PAGE_BYTES);
    guarded.block = raw + (ITEST_PAGE_BYTES - (uintptr_t)raw % ITEST_PAGE_BYTES) % ITEST_PAGE_BYTES;
    guarded.data = guarded.block + guarded.rounded - capacity;
    guarded.capacity = capacity;
    mprotect(guarded.block + guarded.rounded, ITEST_PAGE_BYTES, PROT_NONE);
    return guarded;
}

/* The harness clears its arena between tests, so the page has to be open again by then. */
static void itest_guarded_open(itest_guarded_t *guarded) {
    mprotect(guarded->block + guarded->rounded, ITEST_PAGE_BYTES, PROT_READ | PROT_WRITE);
}

/* ---- A bit writer, to build the exact streams a real encoder would never write. ---- */

typedef struct itest_writer_t {
    uint8_t *bytes;   /* 0 */
    uint64_t length;  /* 8: whole bytes finished */
    uint64_t buffer;  /* 16 */
    uint32_t count;   /* 24 */
    uint8_t _pad0[4]; /* 28 */
} itest_writer_t;
static_assert(sizeof(itest_writer_t) == 32);

/* Extra bits and headers go lowest bit first. */
static void itest_bits(itest_writer_t *writer, uint32_t value, uint32_t count) {
    writer->buffer |= (uint64_t)value << writer->count;
    writer->count += count;
    while (writer->count >= 8u) {
        writer->bytes[writer->length] = (uint8_t)writer->buffer;
        writer->length += 1u;
        writer->buffer >>= 8u;
        writer->count -= 8u;
    }
}

/* A Huffman code goes highest bit first. */
static void itest_code(itest_writer_t *writer, uint32_t code, uint32_t count) {
    for (uint32_t bit = count; bit > 0u; bit -= 1u) {
        itest_bits(writer, (code >> (bit - 1u)) & 1u, 1u);
    }
}

static void itest_flush(itest_writer_t *writer) {
    if (writer->count != 0u) {
        itest_bits(writer, 0u, 8u - writer->count);
    }
}

/* RFC 1951 3.2.6. */
static void itest_fixed_symbol(itest_writer_t *writer, uint32_t symbol) {
    if (symbol < 144u) {
        itest_code(writer, 0x30u + symbol, 8u);
    } else if (symbol < 256u) {
        itest_code(writer, 0x190u + symbol - 144u, 9u);
    } else if (symbol < 280u) {
        itest_code(writer, symbol - 256u, 7u);
    } else {
        itest_code(writer, 0xC0u + symbol - 280u, 8u);
    }
}

/* Length and distance by the RFC's description rather than a table, so a wrong table in the decoder cannot be copied. */
static void itest_length_code(uint32_t length, uint32_t *symbol, uint32_t *extra, uint32_t *extra_bits) {
    if (length == 258u) {
        *symbol = 285u;
        *extra = 0u;
        *extra_bits = 0u;
        return;
    }
    uint32_t base = 3u;
    uint32_t code = 257u;
    for (;;) {
        const uint32_t bits = code < 265u ? 0u : (code - 261u) / 4u;
        if (length < base + (1u << bits)) {
            *symbol = code;
            *extra = length - base;
            *extra_bits = bits;
            return;
        }
        base += 1u << bits;
        code += 1u;
    }
}

static void itest_distance_code(uint32_t distance, uint32_t *symbol, uint32_t *extra, uint32_t *extra_bits) {
    uint32_t base = 1u;
    uint32_t code = 0u;
    for (;;) {
        const uint32_t bits = code < 4u ? 0u : (code - 2u) / 2u;
        if (distance < base + (1u << bits)) {
            *symbol = code;
            *extra = distance - base;
            *extra_bits = bits;
            return;
        }
        base += 1u << bits;
        code += 1u;
    }
}

static void itest_fixed_match(itest_writer_t *writer, uint32_t length, uint32_t distance) {
    uint32_t symbol = 0u;
    uint32_t extra = 0u;
    uint32_t extra_bits = 0u;
    itest_length_code(length, &symbol, &extra, &extra_bits);
    itest_fixed_symbol(writer, symbol);
    itest_bits(writer, extra, extra_bits);
    itest_distance_code(distance, &symbol, &extra, &extra_bits);
    itest_code(writer, symbol, 5u);
    itest_bits(writer, extra, extra_bits);
}

/* Canonical codes, from RFC 1951 3.2.2. */
static void itest_canonical(const uint8_t *lengths, uint32_t symbols, uint32_t *codes) {
    uint32_t counts[16] = {0u};
    for (uint32_t symbol = 0u; symbol < symbols; symbol += 1u) {
        counts[lengths[symbol]] += 1u;
    }
    counts[0] = 0u;
    uint32_t next[16] = {0u};
    uint32_t code = 0u;
    for (uint32_t bits = 1u; bits < 16u; bits += 1u) {
        code = (code + counts[bits - 1u]) << 1u;
        next[bits] = code;
    }
    for (uint32_t symbol = 0u; symbol < symbols; symbol += 1u) {
        if (lengths[symbol] != 0u) {
            codes[symbol] = next[lengths[symbol]];
            next[lengths[symbol]] += 1u;
        }
    }
}

/* The order of RFC 1951 3.2.7. Here so tests can write a code length code. */
static const uint8_t ITEST_ORDER[ITEST_CODE_LENGTH_SYMBOLS] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                               11, 4,  12, 3, 13, 2, 14, 1, 15};

/* A complete code length code covering all 19 symbols, so a test can write any code length or repeat. */
typedef struct itest_lengths_code_t {
    uint8_t lengths[ITEST_CODE_LENGTH_SYMBOLS]; /* 0 */
    uint8_t _pad0[1];                           /* 19 */
    uint32_t codes[ITEST_CODE_LENGTH_SYMBOLS];  /* 20 */
} itest_lengths_code_t;
static_assert(sizeof(itest_lengths_code_t) == 96);

static void itest_lengths_code_make(itest_lengths_code_t *code) {
    memset(code, 0, sizeof(*code));
    for (uint32_t symbol = 0u; symbol < ITEST_CODE_LENGTH_SYMBOLS; symbol += 1u) {
        code->lengths[symbol] = symbol < 13u ? 4u : 5u; /* 13/16 + 6/32 = 1 */
    }
    itest_canonical(code->lengths, ITEST_CODE_LENGTH_SYMBOLS, code->codes);
}

/* A block header: BFINAL 1, BTYPE 2, the counts, and the code length code. */
static void itest_dynamic_open(itest_writer_t *writer, const itest_lengths_code_t *code, uint32_t hlit,
                               uint32_t hdist) {
    itest_bits(writer, 1u, 1u);
    itest_bits(writer, 2u, 2u);
    itest_bits(writer, hlit - 257u, 5u);
    itest_bits(writer, hdist - 1u, 5u);
    itest_bits(writer, ITEST_CODE_LENGTH_SYMBOLS - 4u, 4u);
    for (uint32_t index = 0u; index < ITEST_CODE_LENGTH_SYMBOLS; index += 1u) {
        itest_bits(writer, code->lengths[ITEST_ORDER[index]], 3u);
    }
}

static void itest_dynamic_length(itest_writer_t *writer, const itest_lengths_code_t *code, uint32_t symbol) {
    itest_code(writer, code->codes[symbol], code->lengths[symbol]);
}

/* A whole dynamic header, the code lengths written one by one. */
static void itest_dynamic_header(itest_writer_t *writer, const uint8_t *lit_lengths, uint32_t hlit,
                                 const uint8_t *dist_lengths, uint32_t hdist) {
    itest_lengths_code_t code;
    itest_lengths_code_make(&code);
    itest_dynamic_open(writer, &code, hlit, hdist);
    for (uint32_t index = 0u; index < hlit; index += 1u) {
        itest_dynamic_length(writer, &code, lit_lengths[index]);
    }
    for (uint32_t index = 0u; index < hdist; index += 1u) {
        itest_dynamic_length(writer, &code, dist_lengths[index]);
    }
}

/* ---- Fixtures ---- */

static uint8_t *itest_read_file(test_context_t *t, const char *directory, const char *name, uint64_t *length) {
    char path[1024];
    const int32_t written = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (!CHECK(written > 0 && (uint32_t)written < sizeof(path))) {
        return nullptr;
    }
    const int32_t descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (!CHECK(descriptor >= 0)) {
        return nullptr;
    }
    struct stat status;
    if (!CHECK(fstat(descriptor, &status) == 0)) {
        close(descriptor);
        return nullptr;
    }
    const uint64_t size = (uint64_t)status.st_size;
    uint8_t *bytes = test_alloc(t, size + 1u);
    uint64_t done = 0u;
    while (done < size) {
        const int64_t got = read(descriptor, bytes + done, size - done);
        if (!CHECK(got > 0)) {
            close(descriptor);
            return nullptr;
        }
        done += (uint64_t)got;
    }
    close(descriptor);
    *length = size;
    return bytes;
}

typedef struct itest_fixture_t {
    uint64_t decoded_length; /* 0 */
    uint64_t decoded_hash;   /* 8 */
    uint8_t *bytes;          /* 16 */
    uint64_t length;         /* 24 */
    char name[64];           /* 32 */
    uint32_t block_type;     /* 96 */
    uint8_t is_zlib;         /* 100 */
    uint8_t _pad0[3];        /* 101 */
} itest_fixture_t;
static_assert(sizeof(itest_fixture_t) == 104);

/* Fills fixtures from the manifest, up to the expected count. A missing file or line fails the test rather than
 * quietly checking less. */
static uint32_t itest_load_fixtures(test_context_t *t, itest_fixture_t *fixtures) {
    if (!CHECK(t->arg_count == 1)) {
        return 0u;
    }
    const char *directory = t->borrowed_args[0];
    uint64_t manifest_length = 0u;
    uint8_t *manifest = itest_read_file(t, directory, "manifest.txt", &manifest_length);
    if (manifest == nullptr) {
        return 0u;
    }
    uint32_t count = 0u;
    const char *cursor = (const char *)manifest;
    while (*cursor != '\0' && count < ITEST_FIXTURES_EXPECTED) {
        itest_fixture_t *fixture = &fixtures[count];
        char wrapper[16];
        uint64_t decoded_length = 0u;
        uint64_t hash = 0u;
        uint32_t block_type = 0u;
        int32_t consumed = 0;
        const int32_t fields = sscanf(cursor, "%63s %15s %lu %lx %u%n", fixture->name, wrapper, &decoded_length, &hash,
                                      &block_type, &consumed);
        if (!CHECK(fields == 5)) {
            return count;
        }
        cursor += consumed;
        while (*cursor == '\n') {
            cursor += 1;
        }
        fixture->decoded_length = decoded_length;
        fixture->decoded_hash = hash;
        fixture->block_type = block_type;
        fixture->is_zlib = strcmp(wrapper, "zlib") == 0 ? 1u : 0u;
        fixture->bytes = itest_read_file(t, directory, fixture->name, &fixture->length);
        if (fixture->bytes == nullptr) {
            return count;
        }
        count += 1u;
    }
    return count;
}

static inflate_status_t itest_decode(const itest_fixture_t *fixture, const uint8_t *input, uint64_t length,
                                     uint8_t *output, uint64_t capacity, inflate_result_t *result) {
    return fixture->is_zlib != 0u ? inflate_zlib(input, length, output, capacity, result)
                                  : inflate_raw(input, length, output, capacity, result);
}

TEST(fixtures_decode_exactly) {
    itest_fixture_t *fixtures = test_alloc(t, sizeof(itest_fixture_t) * ITEST_FIXTURES_EXPECTED);
    const uint32_t count = itest_load_fixtures(t, fixtures);
    REQUIRE(count == ITEST_FIXTURES_EXPECTED);

    uint8_t *output = test_alloc(t, ITEST_STREAM_BYTES);
    uint32_t block_types_seen = 0u;
    for (uint32_t index = 0u; index < count; index += 1u) {
        const itest_fixture_t *fixture = &fixtures[index];
        REQUIRE(fixture->decoded_length <= ITEST_STREAM_BYTES);
        inflate_result_t result;
        const inflate_status_t status =
            itest_decode(fixture, fixture->bytes, fixture->length, output, fixture->decoded_length, &result);
        if (status != INFLATE_OK) {
            printf("%s: %s\n", fixture->name, inflate_status_text(status));
        }
        CHECK_EQ_U64(status, INFLATE_OK);
        CHECK_EQ_U64(result.output_used, fixture->decoded_length);
        CHECK_EQ_U64(result.input_used, fixture->length);
        CHECK_EQ_U64(itest_fnv1a(output, result.output_used), fixture->decoded_hash);
        block_types_seen |= 1u << fixture->block_type;

        if (fixture->decoded_length != 0u) {
            const inflate_status_t short_status =
                itest_decode(fixture, fixture->bytes, fixture->length, output, fixture->decoded_length - 1u, &result);
            CHECK_EQ_U64(short_status, INFLATE_NO_ROOM);
            CHECK(result.output_used <= fixture->decoded_length - 1u);
        }
    }
    /* All three block types came from the real encoder. */
    CHECK_EQ_U64(block_types_seen, 7u);
}

TEST(every_truncation_says_truncated) {
    itest_fixture_t *fixtures = test_alloc(t, sizeof(itest_fixture_t) * ITEST_FIXTURES_EXPECTED);
    const uint32_t count = itest_load_fixtures(t, fixtures);
    REQUIRE(count == ITEST_FIXTURES_EXPECTED);
    uint8_t *output = test_alloc(t, ITEST_STREAM_BYTES);
    for (uint32_t index = 0u; index < count; index += 1u) {
        const itest_fixture_t *fixture = &fixtures[index];
        const uint64_t stride = fixture->length / 2000u + 1u;
        for (uint64_t cut = 0u; cut < fixture->length; cut += stride) {
            inflate_result_t result;
            const inflate_status_t status =
                itest_decode(fixture, fixture->bytes, cut, output, fixture->decoded_length, &result);
            CHECK_EQ_U64(status, INFLATE_TRUNCATED);
            CHECK(result.input_used <= cut);
        }
        /* And the last byte, which the stride can step over. */
        if (fixture->length != 0u) {
            inflate_result_t result;
            CHECK_EQ_U64(
                itest_decode(fixture, fixture->bytes, fixture->length - 1u, output, fixture->decoded_length, &result),
                INFLATE_TRUNCATED);
        }
    }
}

/* ---- Streams by hand ---- */

TEST(stored_block) {
    static const uint8_t stream[] = {0x01u, 0x05u, 0x00u, 0xFAu, 0xFFu, 'h', 'e', 'l', 'l', 'o'};
    uint8_t output[5];
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, sizeof(stream), output, sizeof(output), &result), INFLATE_OK);
    CHECK_EQ_U64(result.output_used, 5u);
    CHECK_EQ_U64(result.input_used, sizeof(stream));
    CHECK_MEM_EQ(output, "hello", 5u);
}

TEST(stored_block_rejects_bad_nlen) {
    static const uint8_t stream[] = {0x01u, 0x05u, 0x00u, 0xFBu, 0xFFu, 'h', 'e', 'l', 'l', 'o'};
    uint8_t output[5];
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, sizeof(stream), output, sizeof(output), &result), INFLATE_BAD_STORED_LENGTH);
}

TEST(stored_block_length_past_input_is_truncated) {
    static const uint8_t stream[] = {0x01u, 0xFFu, 0xFFu, 0x00u, 0x00u, 'h'};
    uint8_t output[8];
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, sizeof(stream), output, sizeof(output), &result), INFLATE_TRUNCATED);
    CHECK_EQ_U64(result.output_used, 0u);
}

TEST(reserved_block_type) {
    static const uint8_t stream[] = {0x07u, 0x00u};
    uint8_t output[1];
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, sizeof(stream), output, sizeof(output), &result), INFLATE_BAD_BLOCK_TYPE);
}

TEST(null_buffers_and_empty_input) {
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(nullptr, 0u, nullptr, 0u, &result), INFLATE_TRUNCATED);
    CHECK_EQ_U64(result.input_used, 0u);
    CHECK_EQ_U64(result.output_used, 0u);
    CHECK_EQ_U64(inflate_zlib(nullptr, 0u, nullptr, 0u, &result), INFLATE_TRUNCATED);

    /* An empty stored block needs no output memory at all, and one byte has nowhere to go. */
    static const uint8_t empty[] = {0x01u, 0x00u, 0x00u, 0xFFu, 0xFFu};
    CHECK_EQ_U64(inflate_raw(empty, sizeof(empty), nullptr, 0u, &result), INFLATE_OK);
    static const uint8_t one[] = {0x01u, 0x01u, 0x00u, 0xFEu, 0xFFu, 'x'};
    CHECK_EQ_U64(inflate_raw(one, sizeof(one), nullptr, 0u, &result), INFLATE_NO_ROOM);
}

TEST(raw_leaves_trailing_bytes_alone) {
    static const uint8_t stream[] = {0x01u, 0x01u, 0x00u, 0xFEu, 0xFFu, 'x', 0xAAu, 0xBBu};
    uint8_t output[1];
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, sizeof(stream), output, sizeof(output), &result), INFLATE_OK);
    CHECK_EQ_U64(result.input_used, 6u);

    /* After a Huffman block too, which reads ahead into its bit buffer and has to give the whole bytes back. */
    uint8_t *bytes = test_alloc(t, 64u);
    itest_writer_t writer = {.bytes = bytes};
    itest_bits(&writer, 1u, 1u);
    itest_bits(&writer, 1u, 2u);
    itest_fixed_symbol(&writer, 'q');
    itest_fixed_symbol(&writer, 256u);
    itest_flush(&writer);
    const uint64_t stream_length = writer.length;
    memset(bytes + stream_length, 0xCC, 16u);
    CHECK_EQ_U64(inflate_raw(bytes, stream_length + 16u, output, sizeof(output), &result), INFLATE_OK);
    CHECK_EQ_U64(result.input_used, stream_length);
    CHECK_EQ_U64(output[0], 'q');
}

TEST(stored_after_huffman_block_is_byte_aligned) {
    uint8_t *bytes = test_alloc(t, 64u);
    itest_writer_t writer = {.bytes = bytes};
    itest_bits(&writer, 0u, 1u);
    itest_bits(&writer, 1u, 2u);
    itest_fixed_symbol(&writer, 'a');
    itest_fixed_symbol(&writer, 256u);
    itest_bits(&writer, 1u, 1u);
    itest_bits(&writer, 0u, 2u);
    itest_flush(&writer);
    static const uint8_t stored[] = {0x02u, 0x00u, 0xFDu, 0xFFu, 'b', 'c'};
    memcpy(bytes + writer.length, stored, sizeof(stored));
    uint8_t output[3];
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(bytes, writer.length + sizeof(stored), output, sizeof(output), &result), INFLATE_OK);
    CHECK_MEM_EQ(output, "abc", 3u);
}

TEST(every_length_and_distance) {
    /* Fill 32768 bytes that tell any two positions apart, then make matches at the edges of every code. */
    enum : uint32_t { HISTORY = 32768u };
    uint8_t *history = test_alloc(t, HISTORY);
    for (uint32_t index = 0u; index < HISTORY; index += 1u) {
        history[index] = (uint8_t)((index * 31u + (index >> 8u) * 7u) ^ (index >> 3u));
    }
    uint8_t *stream = test_alloc(t, ITEST_STREAM_BYTES);
    uint8_t *output = test_alloc(t, ITEST_STREAM_BYTES);
    uint8_t *expected = test_alloc(t, ITEST_STREAM_BYTES);

    /* Every length from 3 to 258, at one distance. */
    uint64_t used = 0u;
    stream[0] = 0x00u; /* not final, stored */
    stream[1] = (uint8_t)(HISTORY & 255u);
    stream[2] = (uint8_t)(HISTORY >> 8u);
    stream[3] = (uint8_t)(~HISTORY & 255u);
    stream[4] = (uint8_t)((~HISTORY >> 8u) & 255u);
    memcpy(stream + 5u, history, HISTORY);
    memcpy(expected, history, HISTORY);
    used = HISTORY;
    itest_writer_t writer = {.bytes = stream, .length = 5u + HISTORY};
    itest_bits(&writer, 1u, 1u);
    itest_bits(&writer, 1u, 2u);
    for (uint32_t length = 3u; length <= 258u; length += 1u) {
        itest_fixed_match(&writer, length, 1000u);
        for (uint32_t index = 0u; index < length; index += 1u) {
            expected[used] = expected[used - 1000u];
            used += 1u;
        }
    }
    /* Each distance code's first and last distance, and some between, where 32768 is the very last. */
    uint32_t base = 1u;
    for (uint32_t code = 0u; code < 30u; code += 1u) {
        const uint32_t bits = code < 4u ? 0u : (code - 2u) / 2u;
        const uint32_t samples[3] = {base, base + (1u << bits) / 2u, base + (1u << bits) - 1u};
        for (uint32_t index = 0u; index < 3u; index += 1u) {
            itest_fixed_match(&writer, 5u, samples[index]);
            for (uint32_t byte = 0u; byte < 5u; byte += 1u) {
                expected[used] = expected[used - samples[index]];
                used += 1u;
            }
        }
        base += 1u << bits;
    }
    CHECK_EQ_U64(base, 32769u);
    itest_fixed_symbol(&writer, 256u);
    itest_flush(&writer);

    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, used, &result), INFLATE_OK);
    CHECK_EQ_U64(result.output_used, used);
    CHECK_MEM_EQ(output, expected, used);
}

TEST(length_258_has_two_spellings) {
    /* Symbol 285 is 258 with no extra bits. Symbol 284 with all five extra bits set is also 258. */
    uint8_t stream[32];
    itest_writer_t writer = {.bytes = stream};
    itest_bits(&writer, 1u, 1u);
    itest_bits(&writer, 1u, 2u);
    itest_fixed_symbol(&writer, 'z');
    itest_fixed_symbol(&writer, 284u);
    itest_bits(&writer, 31u, 5u);
    itest_code(&writer, 0u, 5u);
    itest_fixed_symbol(&writer, 285u);
    itest_code(&writer, 0u, 5u);
    itest_fixed_symbol(&writer, 256u);
    itest_flush(&writer);
    uint8_t *output = test_alloc(t, 517u);
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, 517u, &result), INFLATE_OK);
    CHECK_EQ_U64(result.output_used, 517u);
    for (uint32_t index = 0u; index < 517u; index += 1u) {
        CHECK_EQ_U64(output[index], 'z');
    }
}

TEST(overlapping_match_repeats) {
    uint8_t stream[32];
    itest_writer_t writer = {.bytes = stream};
    itest_bits(&writer, 1u, 1u);
    itest_bits(&writer, 1u, 2u);
    itest_fixed_symbol(&writer, 'a');
    itest_fixed_symbol(&writer, 'b');
    itest_fixed_match(&writer, 7u, 2u);
    itest_fixed_symbol(&writer, 256u);
    itest_flush(&writer);
    uint8_t output[9];
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_OK);
    CHECK_MEM_EQ(output, "ababababa", 9u);
}

TEST(invalid_fixed_symbols) {
    for (uint32_t symbol = 286u; symbol <= 287u; symbol += 1u) {
        uint8_t stream[16];
        itest_writer_t writer = {.bytes = stream};
        itest_bits(&writer, 1u, 1u);
        itest_bits(&writer, 1u, 2u);
        itest_fixed_symbol(&writer, symbol);
        itest_bits(&writer, 0u, 16u);
        itest_flush(&writer);
        uint8_t output[4];
        inflate_result_t result;
        CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_SYMBOL);
    }
    for (uint32_t symbol = 30u; symbol <= 31u; symbol += 1u) {
        uint8_t stream[16];
        itest_writer_t writer = {.bytes = stream};
        itest_bits(&writer, 1u, 1u);
        itest_bits(&writer, 1u, 2u);
        itest_fixed_symbol(&writer, 'a');
        itest_fixed_symbol(&writer, 257u);
        itest_code(&writer, symbol, 5u);
        itest_bits(&writer, 0u, 16u);
        itest_flush(&writer);
        uint8_t output[8];
        inflate_result_t result;
        CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_SYMBOL);
    }
}

TEST(distance_past_start_of_output) {
    /* Exactly as far back as there is output is fine, and one further is not. */
    for (uint32_t distance = 1u; distance <= 3u; distance += 1u) {
        uint8_t stream[16];
        itest_writer_t writer = {.bytes = stream};
        itest_bits(&writer, 1u, 1u);
        itest_bits(&writer, 1u, 2u);
        itest_fixed_symbol(&writer, 'a');
        itest_fixed_symbol(&writer, 'b');
        itest_fixed_match(&writer, 3u, distance);
        itest_fixed_symbol(&writer, 256u);
        itest_flush(&writer);
        uint8_t output[8];
        inflate_result_t result;
        CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result),
                     distance <= 2u ? INFLATE_OK : INFLATE_BAD_DISTANCE);
    }
    /* A match with no output at all. */
    uint8_t stream[16];
    itest_writer_t writer = {.bytes = stream};
    itest_bits(&writer, 1u, 1u);
    itest_bits(&writer, 1u, 2u);
    itest_fixed_match(&writer, 3u, 1u);
    itest_fixed_symbol(&writer, 256u);
    itest_flush(&writer);
    uint8_t output[8];
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_DISTANCE);
}

TEST(match_past_capacity_writes_nothing) {
    uint8_t stream[16];
    itest_writer_t writer = {.bytes = stream};
    itest_bits(&writer, 1u, 1u);
    itest_bits(&writer, 1u, 2u);
    itest_fixed_symbol(&writer, 'a');
    itest_fixed_match(&writer, 10u, 1u);
    itest_fixed_symbol(&writer, 256u);
    itest_flush(&writer);
    uint8_t output[12];
    memset(output, 0x5A, sizeof(output));
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, 10u, &result), INFLATE_NO_ROOM);
    CHECK_EQ_U64(result.output_used, 1u);
    CHECK_EQ_U64(output[1], 0x5Au);
    CHECK_EQ_U64(output[10], 0x5Au);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, 11u, &result), INFLATE_OK);
    CHECK_EQ_U64(result.output_used, 11u);
}

/* The lengths of a literal/length alphabet with 'a', 'b' and end-of-block, and their codes. */
static void itest_dynamic_literals(uint8_t *lengths, uint32_t a_bits, uint32_t b_bits, uint32_t end_bits) {
    memset(lengths, 0, ITEST_LITLEN_SYMBOLS);
    lengths['a'] = (uint8_t)a_bits;
    lengths['b'] = (uint8_t)b_bits;
    lengths[256] = (uint8_t)end_bits;
}

TEST(dynamic_block_with_one_distance_code) {
    uint8_t lit_lengths[ITEST_LITLEN_SYMBOLS];
    itest_dynamic_literals(lit_lengths, 1u, 3u, 3u);
    lit_lengths[257] = 2u; /* length 3 */
    uint32_t codes[ITEST_LITLEN_SYMBOLS] = {0u};
    itest_canonical(lit_lengths, 258u, codes);
    static const uint8_t dist_lengths[1] = {1u};
    uint8_t stream[256];
    itest_writer_t writer = {.bytes = stream};
    itest_dynamic_header(&writer, lit_lengths, 258u, dist_lengths, 1u);
    itest_code(&writer, codes['a'], 1u);
    itest_code(&writer, codes['b'], 3u);
    itest_code(&writer, codes[257], 2u);
    itest_code(&writer, 0u, 1u); /* distance symbol 0, distance 1 */
    itest_code(&writer, codes[256], 3u);
    itest_flush(&writer);
    uint8_t output[5];
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_OK);
    CHECK_MEM_EQ(output, "abbbb", 5u);

    /* The other pattern of that one bit is no code, and has to fail rather than decode as anything. */
    writer = (itest_writer_t){.bytes = stream};
    itest_dynamic_header(&writer, lit_lengths, 258u, dist_lengths, 1u);
    itest_code(&writer, codes['a'], 1u);
    itest_code(&writer, codes[257], 2u);
    itest_code(&writer, 1u, 1u);
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_SYMBOL);
}

TEST(dynamic_block_codes_up_to_fifteen_bits) {
    /* Lengths 1, 2, ... 14, 15, 15 make a complete code, and take the slow path past the 9-bit table. */
    uint8_t lit_lengths[ITEST_LITLEN_SYMBOLS];
    memset(lit_lengths, 0, sizeof(lit_lengths));
    for (uint32_t index = 0u; index < 15u; index += 1u) {
        lit_lengths['A' + index] = (uint8_t)(index + 1u);
    }
    lit_lengths[256] = 15u;
    uint32_t codes[ITEST_LITLEN_SYMBOLS] = {0u};
    itest_canonical(lit_lengths, 257u, codes);
    static const uint8_t dist_lengths[1] = {0u};

    uint8_t stream[256];
    itest_writer_t writer = {.bytes = stream};
    itest_dynamic_header(&writer, lit_lengths, 257u, dist_lengths, 1u);
    for (uint32_t index = 0u; index < 15u; index += 1u) {
        itest_code(&writer, codes['A' + index], index + 1u);
    }
    itest_code(&writer, codes[256], 15u);
    itest_flush(&writer);
    uint8_t output[15];
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_OK);
    CHECK_MEM_EQ(output, "ABCDEFGHIJKLMNO", 15u);
}

TEST(dynamic_block_only_end_code) {
    uint8_t lit_lengths[ITEST_LITLEN_SYMBOLS];
    memset(lit_lengths, 0, sizeof(lit_lengths));
    lit_lengths[256] = 1u;
    static const uint8_t dist_lengths[1] = {0u};
    uint8_t stream[256];
    itest_writer_t writer = {.bytes = stream};
    itest_dynamic_header(&writer, lit_lengths, 257u, dist_lengths, 1u);
    itest_code(&writer, 0u, 1u);
    itest_flush(&writer);
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, writer.length, nullptr, 0u, &result), INFLATE_OK);
}

TEST(dynamic_block_no_distance_codes_fails_only_when_used) {
    uint8_t lit_lengths[ITEST_LITLEN_SYMBOLS];
    itest_dynamic_literals(lit_lengths, 2u, 0u, 2u);
    lit_lengths[257] = 1u;
    uint32_t codes[ITEST_LITLEN_SYMBOLS] = {0u};
    itest_canonical(lit_lengths, 258u, codes);
    static const uint8_t dist_lengths[1] = {0u};
    uint8_t stream[256];
    itest_writer_t writer = {.bytes = stream};
    itest_dynamic_header(&writer, lit_lengths, 258u, dist_lengths, 1u);
    itest_code(&writer, codes['a'], 2u);
    itest_code(&writer, codes[256], 2u);
    itest_flush(&writer);
    uint8_t output[4];
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_OK);

    writer = (itest_writer_t){.bytes = stream};
    itest_dynamic_header(&writer, lit_lengths, 258u, dist_lengths, 1u);
    itest_code(&writer, codes['a'], 2u);
    itest_code(&writer, codes[257], 1u);
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_SYMBOL);
}

TEST(dynamic_header_rejections) {
    uint8_t lit_lengths[ITEST_LITLEN_SYMBOLS];
    static const uint8_t one_distance[1] = {1u};
    uint8_t stream[1024];
    uint8_t output[8];
    inflate_result_t result;
    itest_writer_t writer;

    /* Over-subscribed literal code: three codes of one bit. */
    itest_dynamic_literals(lit_lengths, 1u, 1u, 1u);
    writer = (itest_writer_t){.bytes = stream};
    itest_dynamic_header(&writer, lit_lengths, 257u, one_distance, 1u);
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_HUFFMAN_SET);

    /* Incomplete literal code: more than the single code of one bit that is allowed. */
    itest_dynamic_literals(lit_lengths, 2u, 2u, 2u);
    writer = (itest_writer_t){.bytes = stream};
    itest_dynamic_header(&writer, lit_lengths, 257u, one_distance, 1u);
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_HUFFMAN_SET);

    /* A single code, but not of one bit. */
    itest_dynamic_literals(lit_lengths, 0u, 0u, 2u);
    writer = (itest_writer_t){.bytes = stream};
    itest_dynamic_header(&writer, lit_lengths, 257u, one_distance, 1u);
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_HUFFMAN_SET);

    /* The same for the distance code. */
    itest_dynamic_literals(lit_lengths, 1u, 2u, 2u);
    static const uint8_t two_bit_distance[1] = {2u};
    writer = (itest_writer_t){.bytes = stream};
    itest_dynamic_header(&writer, lit_lengths, 257u, two_bit_distance, 1u);
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_HUFFMAN_SET);

    /* Over-subscribed distance code. */
    static const uint8_t three_distances[3] = {1u, 1u, 1u};
    writer = (itest_writer_t){.bytes = stream};
    itest_dynamic_header(&writer, lit_lengths, 257u, three_distances, 3u);
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_HUFFMAN_SET);

    /* Over-subscribed by exactly one code, at the longest length, so only the very last step of the sum can see it. */
    memset(lit_lengths, 0, sizeof(lit_lengths));
    for (uint32_t index = 0u; index < 15u; index += 1u) {
        lit_lengths['A' + index] = (uint8_t)(index + 1u);
    }
    lit_lengths['A' + 15u] = 15u;
    lit_lengths[256] = 15u;
    writer = (itest_writer_t){.bytes = stream};
    itest_dynamic_header(&writer, lit_lengths, 257u, one_distance, 1u);
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_HUFFMAN_SET);

    /* No code for end-of-block. */
    itest_dynamic_literals(lit_lengths, 1u, 1u, 0u);
    writer = (itest_writer_t){.bytes = stream};
    itest_dynamic_header(&writer, lit_lengths, 257u, one_distance, 1u);
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_MISSING_END_CODE);
}

TEST(dynamic_header_counts) {
    uint8_t stream[64];
    uint8_t output[8];
    inflate_result_t result;
    /* HLIT 30 means 287 codes, and HDIST 30 means 31. */
    itest_writer_t writer = {.bytes = stream};
    itest_bits(&writer, 1u, 1u);
    itest_bits(&writer, 2u, 2u);
    itest_bits(&writer, 30u, 5u);
    itest_bits(&writer, 0u, 5u);
    itest_bits(&writer, 0u, 4u);
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_CODE_LENGTHS);

    writer = (itest_writer_t){.bytes = stream};
    itest_bits(&writer, 1u, 1u);
    itest_bits(&writer, 2u, 2u);
    itest_bits(&writer, 0u, 5u);
    itest_bits(&writer, 30u, 5u);
    itest_bits(&writer, 0u, 4u);
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_CODE_LENGTHS);
}

TEST(code_length_code_rejections) {
    uint8_t stream[64];
    uint8_t output[8];
    inflate_result_t result;
    /* All 19 of length 1: over-subscribed. */
    itest_writer_t writer = {.bytes = stream};
    itest_bits(&writer, 1u, 1u);
    itest_bits(&writer, 2u, 2u);
    itest_bits(&writer, 0u, 5u);
    itest_bits(&writer, 0u, 5u);
    itest_bits(&writer, ITEST_CODE_LENGTH_SYMBOLS - 4u, 4u);
    for (uint32_t index = 0u; index < ITEST_CODE_LENGTH_SYMBOLS; index += 1u) {
        itest_bits(&writer, 1u, 3u);
    }
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_HUFFMAN_SET);

    /* All zero: no codes at all, and unlike the other two alphabets this one must be complete. */
    writer = (itest_writer_t){.bytes = stream};
    itest_bits(&writer, 1u, 1u);
    itest_bits(&writer, 2u, 2u);
    itest_bits(&writer, 0u, 5u);
    itest_bits(&writer, 0u, 5u);
    itest_bits(&writer, ITEST_CODE_LENGTH_SYMBOLS - 4u, 4u);
    for (uint32_t index = 0u; index < ITEST_CODE_LENGTH_SYMBOLS; index += 1u) {
        itest_bits(&writer, 0u, 3u);
    }
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_HUFFMAN_SET);

    /* A single code of one bit is allowed for the other two, and not here. */
    writer = (itest_writer_t){.bytes = stream};
    itest_bits(&writer, 1u, 1u);
    itest_bits(&writer, 2u, 2u);
    itest_bits(&writer, 0u, 5u);
    itest_bits(&writer, 0u, 5u);
    itest_bits(&writer, 0u, 4u);
    itest_bits(&writer, 1u, 3u);
    itest_bits(&writer, 0u, 3u);
    itest_bits(&writer, 0u, 3u);
    itest_bits(&writer, 0u, 3u);
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_HUFFMAN_SET);
}

TEST(code_length_repeats) {
    itest_lengths_code_t code;
    itest_lengths_code_make(&code);
    uint8_t stream[256];
    uint8_t output[8];
    inflate_result_t result;

    /* Repeat with nothing before it. */
    itest_writer_t writer = {.bytes = stream};
    itest_dynamic_open(&writer, &code, 257u, 1u);
    itest_dynamic_length(&writer, &code, 16u);
    itest_bits(&writer, 0u, 2u);
    itest_bits(&writer, 0u, 16u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_CODE_LENGTHS);

    /* A run past the end of the 258 lengths: all three repeat codes, each overrunning by one. */
    for (uint32_t kind = 16u; kind <= 18u; kind += 1u) {
        writer = (itest_writer_t){.bytes = stream};
        itest_dynamic_open(&writer, &code, 257u, 1u);
        for (uint32_t index = 0u; index < 255u; index += 1u) {
            itest_dynamic_length(&writer, &code, 8u);
        }
        const uint32_t room = 3u;
        itest_dynamic_length(&writer, &code, kind);
        if (kind == 16u) {
            itest_bits(&writer, room + 1u - 3u, 2u);
        } else if (kind == 17u) {
            itest_bits(&writer, room + 1u - 3u, 3u);
        } else {
            /* 18 repeats at least 11, so it is past the end for any value. */
            itest_bits(&writer, 0u, 7u);
        }
        itest_bits(&writer, 0u, 16u);
        itest_flush(&writer);
        CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_BAD_CODE_LENGTHS);
    }

    /* A run of zeros across the literal and distance alphabets is one sequence, and allowed. The lengths are:
     * 'a' 2 bits, 256 2 bits, 257 1 bit, then zeros for 258, 259 and distances 0, 1, then distance 2 at 1 bit. */
    writer = (itest_writer_t){.bytes = stream};
    itest_dynamic_open(&writer, &code, 260u, 3u);
    itest_dynamic_length(&writer, &code, 18u);
    itest_bits(&writer, 'a' - 11u, 7u);
    itest_dynamic_length(&writer, &code, 2u);
    itest_dynamic_length(&writer, &code, 18u);
    itest_bits(&writer, 138u - 11u, 7u);
    itest_dynamic_length(&writer, &code, 18u);
    itest_bits(&writer, 256u - 'a' - 1u - 138u - 11u, 7u);
    itest_dynamic_length(&writer, &code, 2u);
    itest_dynamic_length(&writer, &code, 1u);
    itest_dynamic_length(&writer, &code, 17u);
    itest_bits(&writer, 4u - 3u, 3u);
    itest_dynamic_length(&writer, &code, 1u);
    uint8_t lit_lengths[ITEST_LITLEN_SYMBOLS];
    itest_dynamic_literals(lit_lengths, 2u, 0u, 2u);
    lit_lengths[257] = 1u;
    uint32_t codes[ITEST_LITLEN_SYMBOLS] = {0u};
    itest_canonical(lit_lengths, 258u, codes);
    for (uint32_t index = 0u; index < 3u; index += 1u) {
        itest_code(&writer, codes['a'], 2u);
    }
    itest_code(&writer, codes[257], 1u); /* length 3 */
    itest_code(&writer, 0u, 1u);         /* distance symbol 2, the only one, distance 3 */
    itest_code(&writer, codes[256], 2u);
    itest_flush(&writer);
    CHECK_EQ_U64(inflate_raw(stream, writer.length, output, sizeof(output), &result), INFLATE_OK);
    CHECK_EQ_U64(result.output_used, 6u);
    CHECK_MEM_EQ(output, "aaaaaa", 6u);
}

/* ---- zlib ---- */

/* A stored-block zlib stream for data, with a good header and checksum. */
static uint64_t itest_zlib_wrap(uint8_t *stream, const uint8_t *data, uint32_t length) {
    stream[0] = 0x78u;
    stream[1] = 0x01u;
    stream[2] = 0x01u;
    stream[3] = (uint8_t)(length & 255u);
    stream[4] = (uint8_t)(length >> 8u);
    stream[5] = (uint8_t)(~length & 255u);
    stream[6] = (uint8_t)((~length >> 8u) & 255u);
    memcpy(stream + 7u, data, length);
    const uint32_t adler = itest_adler32(data, length);
    uint8_t *trailer = stream + 7u + length;
    trailer[0] = (uint8_t)(adler >> 24u);
    trailer[1] = (uint8_t)(adler >> 16u);
    trailer[2] = (uint8_t)(adler >> 8u);
    trailer[3] = (uint8_t)adler;
    return 11ull + length;
}

TEST(zlib_good_and_header_rejections) {
    uint8_t stream[64];
    uint8_t output[16];
    inflate_result_t result;
    const uint64_t length = itest_zlib_wrap(stream, (const uint8_t *)"pixels", 6u);
    CHECK_EQ_U64(inflate_zlib(stream, length, output, sizeof(output), &result), INFLATE_OK);
    CHECK_EQ_U64(result.output_used, 6u);
    CHECK_EQ_U64(result.input_used, length);
    CHECK_MEM_EQ(output, "pixels", 6u);

    /* Each with a correct FCHECK, so only the field in question is wrong. */
    static const uint8_t bad_headers[][2] = {
        {0x77u, 0x01u + 3u},    /* method 7 */
        {0x89u, 0x01u + 12u},   /* window 64 KiB */
        {0x78u, 0x20u + 0x1Cu}, /* FDICT */
    };
    for (uint32_t index = 0u; index < 3u; index += 1u) {
        const uint32_t cmf = bad_headers[index][0];
        uint32_t flags = bad_headers[index][1] & 0xE0u;
        flags += 31u - (cmf * 256u + flags) % 31u;
        CHECK_EQ_U64((cmf * 256u + flags) % 31u, 0u);
        stream[0] = (uint8_t)cmf;
        stream[1] = (uint8_t)flags;
        CHECK_EQ_U64(inflate_zlib(stream, length, output, sizeof(output), &result), INFLATE_BAD_ZLIB_HEADER);
    }
    stream[0] = 0x78u;
    stream[1] = 0x02u; /* FCHECK fails */
    CHECK_EQ_U64(inflate_zlib(stream, length, output, sizeof(output), &result), INFLATE_BAD_ZLIB_HEADER);
}

TEST(zlib_checksum_every_byte) {
    uint8_t stream[64];
    uint8_t output[16];
    inflate_result_t result;
    const uint64_t length = itest_zlib_wrap(stream, (const uint8_t *)"pixels", 6u);
    for (uint64_t index = length - 4u; index < length; index += 1u) {
        stream[index] ^= 0x10u;
        CHECK_EQ_U64(inflate_zlib(stream, length, output, sizeof(output), &result), INFLATE_BAD_CHECKSUM);
        stream[index] ^= 0x10u;
    }
    /* A payload change, with the trailer left alone. */
    stream[7] ^= 1u;
    CHECK_EQ_U64(inflate_zlib(stream, length, output, sizeof(output), &result), INFLATE_BAD_CHECKSUM);
}

TEST(zlib_trailer_short_or_followed) {
    uint8_t stream[64];
    uint8_t output[16];
    inflate_result_t result;
    const uint64_t length = itest_zlib_wrap(stream, (const uint8_t *)"pixels", 6u);
    for (uint64_t cut = length - 4u; cut < length; cut += 1u) {
        CHECK_EQ_U64(inflate_zlib(stream, cut, output, sizeof(output), &result), INFLATE_TRUNCATED);
    }
    stream[length] = 0u;
    CHECK_EQ_U64(inflate_zlib(stream, length + 1u, output, sizeof(output), &result), INFLATE_TRAILING_DATA);
}

TEST(adler_over_a_long_run) {
    /* Past the run length where the sums have to be reduced, and all 0xFF, the worst case for them. */
    enum : uint32_t { LENGTH = 65000u };
    uint8_t *data = test_alloc(t, LENGTH);
    memset(data, 0xFF, LENGTH);
    uint8_t *stream = test_alloc(t, LENGTH + 16u);
    uint8_t *output = test_alloc(t, LENGTH);
    const uint64_t length = itest_zlib_wrap(stream, data, LENGTH);
    inflate_result_t result;
    CHECK_EQ_U64(inflate_zlib(stream, length, output, LENGTH, &result), INFLATE_OK);
}

/* ---- Hostile input ---- */

static uint64_t itest_next_random(uint64_t *state) {
    uint64_t value = *state;
    value ^= value << 13u;
    value ^= value >> 7u;
    value ^= value << 17u;
    *state = value;
    return value;
}

/* Whatever came in, the outcome must be a status, inside the buffers. The guard pages catch what the numbers do not. */
static void itest_decode_hostile(test_context_t *t, const itest_fixture_t *fixture, itest_guarded_t *input,
                                 uint64_t input_length, itest_guarded_t *output) {
    const uint8_t *source = input->data + input->capacity - input_length;
    inflate_result_t result;
    const inflate_status_t status =
        itest_decode(fixture, source, input_length, output->data, output->capacity, &result);
    if (status > INFLATE_TRAILING_DATA || result.input_used > input_length || result.output_used > output->capacity) {
        CHECK(false);
    }
}

TEST(hostile_every_bit_flipped) {
    itest_fixture_t *fixtures = test_alloc(t, sizeof(itest_fixture_t) * ITEST_FIXTURES_EXPECTED);
    const uint32_t count = itest_load_fixtures(t, fixtures);
    REQUIRE(count == ITEST_FIXTURES_EXPECTED);
    itest_guarded_t input = itest_guarded_make(t, 4096u);
    itest_guarded_t output = itest_guarded_make(t, 4096u);
    uint32_t tried = 0u;
    for (uint32_t index = 0u; index < count; index += 1u) {
        const itest_fixture_t *fixture = &fixtures[index];
        if (fixture->length > 1200u) {
            continue;
        }
        uint8_t *source = input.data + input.capacity - fixture->length;
        memcpy(source, fixture->bytes, fixture->length);
        for (uint64_t bit = 0u; bit < fixture->length * 8u; bit += 1u) {
            source[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            itest_decode_hostile(t, fixture, &input, fixture->length, &output);
            source[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        }
        tried += 1u;
    }
    CHECK(tried >= 10u);
    itest_guarded_open(&input);
    itest_guarded_open(&output);
}

TEST(hostile_random_mutations) {
    itest_fixture_t *fixtures = test_alloc(t, sizeof(itest_fixture_t) * ITEST_FIXTURES_EXPECTED);
    const uint32_t count = itest_load_fixtures(t, fixtures);
    REQUIRE(count == ITEST_FIXTURES_EXPECTED);
    itest_guarded_t input = itest_guarded_make(t, 8192u);
    /* Small on purpose: mutated streams run into the end of it all the time. */
    itest_guarded_t output = itest_guarded_make(t, 512u);
    uint64_t random = 0x2026110200000001ull;
    uint32_t statuses_seen = 0u;
    for (uint32_t round = 0u; round < ITEST_MUTATION_ROUNDS; round += 1u) {
        const itest_fixture_t *fixture = &fixtures[itest_next_random(&random) % count];
        if (fixture->length == 0u || fixture->length > input.capacity) {
            continue;
        }
        uint8_t *source = input.data + input.capacity - fixture->length;
        memcpy(source, fixture->bytes, fixture->length);
        const uint32_t changes = (uint32_t)(itest_next_random(&random) % 8u) + 1u;
        for (uint32_t change = 0u; change < changes; change += 1u) {
            /* The header of a stream is where the interesting decisions are, so aim half of the changes there. */
            const uint64_t span =
                (itest_next_random(&random) & 1u) != 0u && fixture->length > 64u ? 64u : fixture->length;
            source[itest_next_random(&random) % span] = (uint8_t)itest_next_random(&random);
        }
        inflate_result_t result;
        const inflate_status_t status =
            itest_decode(fixture, source, fixture->length, output.data, output.capacity, &result);
        if (status > INFLATE_TRAILING_DATA || result.input_used > fixture->length ||
            result.output_used > output.capacity) {
            CHECK(false);
        }
        statuses_seen |= 1u << status;
    }
    itest_guarded_open(&input);
    itest_guarded_open(&output);
    /* The mutations have to have reached deep into the decoder for this to prove anything. */
    const uint32_t wanted = (1u << INFLATE_NO_ROOM) | (1u << INFLATE_BAD_BLOCK_TYPE) |
                            (1u << INFLATE_BAD_STORED_LENGTH) | (1u << INFLATE_BAD_CODE_LENGTHS) |
                            (1u << INFLATE_BAD_HUFFMAN_SET) | (1u << INFLATE_BAD_SYMBOL) |
                            (1u << INFLATE_BAD_DISTANCE) | (1u << INFLATE_BAD_ZLIB_HEADER);
    CHECK_EQ_U64(statuses_seen & wanted, wanted);
}

TEST(guard_page_catches_an_overrun) {
    /* A check of the check: the data really does end against the guard, and the stream fills it to the last byte. */
    itest_guarded_t output = itest_guarded_make(t, 5u);
    static const uint8_t stream[] = {0x01u, 0x05u, 0x00u, 0xFAu, 0xFFu, 'h', 'e', 'l', 'l', 'o'};
    inflate_result_t result;
    CHECK_EQ_U64(inflate_raw(stream, sizeof(stream), output.data, output.capacity, &result), INFLATE_OK);
    CHECK_EQ_U64((uintptr_t)(output.data + output.capacity) % ITEST_PAGE_BYTES, 0u);
    itest_guarded_open(&output);
}

TEST(every_status_has_text) {
    for (uint32_t status = 0u; status <= INFLATE_TRAILING_DATA; status += 1u) {
        CHECK(strlen(inflate_status_text((inflate_status_t)status)) > 4u);
        CHECK(strcmp(inflate_status_text((inflate_status_t)status), "unknown inflate status") != 0);
    }
    CHECK_STR_EQ(inflate_status_text((inflate_status_t)200u), "unknown inflate status");
}

TEST_MAIN()
