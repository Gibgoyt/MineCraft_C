/*
 * ctest compile_png. See README.md. The one argument, from test.cmake, is the fixtures directory.
 */
#include "harness/index.h"
#include "png/index.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum : uint32_t {
    PTEST_PAGE_BYTES = 4096u,
    PTEST_FILE_BYTES = 1u << 20,
    PTEST_FIXTURES_EXPECTED = 79u,
    PTEST_MUTATION_ROUNDS = 30000u,
    PTEST_IMAGE_DATA_ROUNDS = 6000u,
    /* A mutated header may claim a large image. The buffers cap what the mutation test will try. */
    PTEST_MUTATION_PIXEL_BYTES = 1u << 16,
    PTEST_MUTATION_WORK_BYTES = 1u << 17,
};

static uint64_t ptest_fnv1a(const uint8_t *data, uint64_t length) {
    uint64_t value = 0xCBF29CE484222325ull;
    for (uint64_t index = 0u; index < length; index += 1u) {
        value = (value ^ data[index]) * 0x100000001B3ull;
    }
    return value;
}

/* A slow CRC-32 and Adler-32, separate from the decoder's, from the PNG specification's description of them. */
static uint32_t ptest_crc(const uint8_t *data, uint64_t length) {
    uint32_t value = 0xFFFFFFFFu;
    for (uint64_t index = 0u; index < length; index += 1u) {
        value ^= data[index];
        for (uint32_t bit = 0u; bit < 8u; bit += 1u) {
            value = (value >> 1u) ^ (0xEDB88320u & (0u - (value & 1u)));
        }
    }
    return ~value;
}

static uint32_t ptest_adler32(const uint8_t *data, uint64_t length) {
    uint32_t low = 1u;
    uint32_t high = 0u;
    for (uint64_t index = 0u; index < length; index += 1u) {
        low = (low + data[index]) % 65521u;
        high = (high + low) % 65521u;
    }
    return (high << 16u) | low;
}

static void ptest_put_u32(uint8_t *target, uint32_t value) {
    target[0] = (uint8_t)(value >> 24u);
    target[1] = (uint8_t)(value >> 16u);
    target[2] = (uint8_t)(value >> 8u);
    target[3] = (uint8_t)value;
}

static uint32_t ptest_get_u32(const uint8_t *source) {
    return ((uint32_t)source[0] << 24u) | ((uint32_t)source[1] << 16u) | ((uint32_t)source[2] << 8u) |
           (uint32_t)source[3];
}

/* ---- Guarded memory: a byte read or written past the end of a buffer hits a page that faults, and the test dies. ---- */

typedef struct ptest_guarded_t {
    uint8_t *block;    /* 0: page aligned */
    uint8_t *data;     /* 8: ends flush against the guard page */
    uint64_t capacity; /* 16 */
    uint64_t rounded;  /* 24: the bytes of block before the guard page */
} ptest_guarded_t;
static_assert(sizeof(ptest_guarded_t) == 32);

static ptest_guarded_t ptest_guarded_make(test_context_t *t, uint64_t capacity) {
    ptest_guarded_t guarded;
    const uint64_t pages = (capacity + PTEST_PAGE_BYTES - 1u) / PTEST_PAGE_BYTES + 1u;
    guarded.rounded = pages * PTEST_PAGE_BYTES;
    uint8_t *raw = test_alloc(t, guarded.rounded + 2u * PTEST_PAGE_BYTES);
    guarded.block = raw + (PTEST_PAGE_BYTES - (uintptr_t)raw % PTEST_PAGE_BYTES) % PTEST_PAGE_BYTES;
    guarded.data = guarded.block + guarded.rounded - capacity;
    guarded.capacity = capacity;
    mprotect(guarded.block + guarded.rounded, PTEST_PAGE_BYTES, PROT_NONE);
    return guarded;
}

/* The harness clears its arena between tests, so the page has to be open again by then. */
static void ptest_guarded_open(ptest_guarded_t *guarded) {
    mprotect(guarded->block + guarded->rounded, PTEST_PAGE_BYTES, PROT_READ | PROT_WRITE);
}

/* ---- A file builder, for the exact files a real encoder would never write. ---- */

typedef struct ptest_file_t {
    uint8_t *bytes;  /* 0 */
    uint64_t length; /* 8 */
} ptest_file_t;
static_assert(sizeof(ptest_file_t) == 16);

static ptest_file_t ptest_file_make(test_context_t *t) {
    ptest_file_t file = {.bytes = test_alloc(t, PTEST_FILE_BYTES)};
    static const uint8_t signature[8] = {0x89u, 'P', 'N', 'G', '\r', '\n', 0x1Au, '\n'};
    memcpy(file.bytes, signature, 8u);
    file.length = 8u;
    return file;
}

static void ptest_chunk(ptest_file_t *file, const char *type, const void *data, uint32_t length) {
    uint8_t *start = file->bytes + file->length;
    ptest_put_u32(start, length);
    memcpy(start + 4u, type, 4u);
    if (length != 0u) {
        memcpy(start + 8u, data, length);
    }
    ptest_put_u32(start + 8u + length, ptest_crc(start + 4u, (uint64_t)length + 4u));
    file->length += 12ull + length;
}

static void ptest_ihdr(ptest_file_t *file, uint32_t width, uint32_t height, uint8_t depth, uint8_t color,
                       uint8_t interlace) {
    uint8_t header[13];
    ptest_put_u32(header, width);
    ptest_put_u32(header + 4u, height);
    header[8] = depth;
    header[9] = color;
    header[10] = 0u;
    header[11] = 0u;
    header[12] = interlace;
    ptest_chunk(file, "IHDR", header, 13u);
}

/* An IDAT chunk whose payload is raw as a stored-block zlib stream, with a good Adler-32. */
static void ptest_idat(ptest_file_t *file, const uint8_t *raw, uint32_t length) {
    uint8_t *stream = file->bytes + file->length + 8u;
    stream[0] = 0x78u;
    stream[1] = 0x01u;
    uint64_t used = 2u;
    uint32_t done = 0u;
    do {
        const uint32_t left = length - done;
        const uint32_t run = left < 65535u ? left : 65535u;
        stream[used] = done + run == length ? 1u : 0u;
        stream[used + 1u] = (uint8_t)(run & 255u);
        stream[used + 2u] = (uint8_t)(run >> 8u);
        stream[used + 3u] = (uint8_t)(~run & 255u);
        stream[used + 4u] = (uint8_t)((~run >> 8u) & 255u);
        if (run != 0u) {
            memcpy(stream + used + 5u, raw + done, run);
        }
        used += 5ull + run;
        done += run;
    } while (done < length);
    ptest_put_u32(stream + used, ptest_adler32(raw, length));
    used += 4u;
    /* The payload is already in place, right where ptest_chunk would copy it from: this is that function's tail. */
    uint8_t *start = file->bytes + file->length;
    ptest_put_u32(start, (uint32_t)used);
    memcpy(start + 4u, "IDAT", 4u);
    ptest_put_u32(start + 8u + used, ptest_crc(start + 4u, used + 4u));
    file->length += 12u + used;
}

static void ptest_iend(ptest_file_t *file) {
    ptest_chunk(file, "IEND", nullptr, 0u);
}

/* Decoding that must succeed, returning the pixels from the test arena. */
static uint8_t *ptest_decode_ok(test_context_t *t, const ptest_file_t *file, png_info_t *info) {
    png_error_t error;
    const png_status_t status = png_probe(file->bytes, file->length, info, &error);
    if (status != PNG_OK) {
        printf("probe: %s\n", error.message);
    }
    if (!CHECK(status == PNG_OK)) {
        return nullptr;
    }
    uint8_t *pixels = test_alloc(t, info->pixel_bytes);
    uint8_t *work = test_alloc(t, info->work_bytes);
    const png_status_t decoded =
        png_decode(file->bytes, file->length, pixels, info->pixel_bytes, work, info->work_bytes, nullptr, &error);
    if (decoded != PNG_OK) {
        printf("decode: %s\n", error.message);
    }
    return CHECK(decoded == PNG_OK) ? pixels : nullptr;
}

/* Decoding that must fail with this status, naming this text, and leave the pixels zero. */
static void ptest_refused_at(test_context_t *t, const ptest_file_t *file, png_status_t expected, const char *needle,
                             int32_t line) {
    enum : uint32_t { PIXEL_BYTES = 1u << 16, WORK_BYTES = 1u << 17 };
    uint8_t *pixels = test_alloc(t, PIXEL_BYTES);
    uint8_t *work = test_alloc(t, WORK_BYTES);
    memset(pixels, 0x5A, PIXEL_BYTES);
    png_error_t error;
    const png_status_t status =
        png_decode(file->bytes, file->length, pixels, PIXEL_BYTES, work, WORK_BYTES, nullptr, &error);
    if (status != expected || error.status != (uint8_t)expected || strstr(error.message, needle) == nullptr) {
        printf("line %d: got status %u (%s), wanted %u (needs \"%s\")\n", line, (uint32_t)status, error.message,
               (uint32_t)expected, needle);
        test_check(t, false, __FILE__, line, "the file is refused as expected");
        return;
    }
    for (uint32_t index = 0u; index < PIXEL_BYTES; index += 4096u) {
        if (pixels[index] != 0u) {
            test_check(t, false, __FILE__, line, "a refused file leaves the pixels zero");
            return;
        }
    }
    test_check(t, true, __FILE__, line, "the file is refused as expected");
}
#define PTEST_REFUSED(file, expected, needle) ptest_refused_at(t, (file), (expected), (needle), __LINE__)

/* The usual 2 by 2 RGBA8 image with no filtering, for tests about everything but the pixels. */
static const uint8_t PTEST_RAW_2X2[] = {
    0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 0u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u,
};

static void ptest_valid_start(ptest_file_t *file) {
    ptest_ihdr(file, 2u, 2u, 8u, PNG_COLOR_RGBA, 0u);
}

/* ---- Fixtures ---- */

static uint8_t *ptest_read_file(test_context_t *t, const char *directory, const char *name, uint64_t *length) {
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

typedef struct ptest_fixture_t {
    uint64_t hash;      /* 0 */
    uint8_t *bytes;     /* 8 */
    uint64_t length;    /* 16 */
    char name[48];      /* 24 */
    uint32_t width;     /* 72 */
    uint32_t height;    /* 76 */
    uint32_t depth;     /* 80 */
    uint32_t color;     /* 84 */
    uint32_t interlace; /* 88 */
    uint32_t has_alpha; /* 92 */
} ptest_fixture_t;
static_assert(sizeof(ptest_fixture_t) == 96);

static uint32_t ptest_load_fixtures(test_context_t *t, ptest_fixture_t *fixtures) {
    if (!CHECK(t->arg_count == 1)) {
        return 0u;
    }
    const char *directory = t->borrowed_args[0];
    uint64_t manifest_length = 0u;
    uint8_t *manifest = ptest_read_file(t, directory, "manifest.txt", &manifest_length);
    if (manifest == nullptr) {
        return 0u;
    }
    uint32_t count = 0u;
    const char *cursor = (const char *)manifest;
    while (*cursor != '\0' && count < PTEST_FIXTURES_EXPECTED) {
        ptest_fixture_t *fixture = &fixtures[count];
        uint64_t hash = 0u;
        int32_t consumed = 0;
        const int32_t fields =
            sscanf(cursor, "%47s %u %u %u %u %u %u %lx%n", fixture->name, &fixture->width, &fixture->height,
                   &fixture->depth, &fixture->color, &fixture->interlace, &fixture->has_alpha, &hash, &consumed);
        if (!CHECK(fields == 8)) {
            return count;
        }
        cursor += consumed;
        while (*cursor == '\n') {
            cursor += 1;
        }
        fixture->hash = hash;
        fixture->bytes = ptest_read_file(t, directory, fixture->name, &fixture->length);
        if (fixture->bytes == nullptr) {
            return count;
        }
        count += 1u;
    }
    return count;
}

TEST(fixtures_decode_exactly) {
    ptest_fixture_t *fixtures = test_alloc(t, sizeof(ptest_fixture_t) * PTEST_FIXTURES_EXPECTED);
    const uint32_t count = ptest_load_fixtures(t, fixtures);
    REQUIRE(count == PTEST_FIXTURES_EXPECTED);

    for (uint32_t index = 0u; index < count; index += 1u) {
        const ptest_fixture_t *fixture = &fixtures[index];
        png_info_t info;
        png_error_t error;
        const png_status_t status = png_probe(fixture->bytes, fixture->length, &info, &error);
        if (status != PNG_OK) {
            printf("%s: %s\n", fixture->name, error.message);
        }
        REQUIRE(status == PNG_OK);
        CHECK_EQ_U64(error.status, PNG_OK);
        CHECK_EQ_U64(info.width, fixture->width);
        CHECK_EQ_U64(info.height, fixture->height);
        CHECK_EQ_U64(info.bit_depth, fixture->depth);
        CHECK_EQ_U64(info.color_type, fixture->color);
        CHECK_EQ_U64(info.interlaced, fixture->interlace);
        CHECK_EQ_U64(info.has_alpha, fixture->has_alpha);
        CHECK_EQ_U64(info.pixel_bytes, (uint64_t)fixture->width * fixture->height * 4u);

        /* Guarded and exact, so that the sizes png_probe reports are proven to be enough. */
        ptest_guarded_t pixels = ptest_guarded_make(t, info.pixel_bytes);
        ptest_guarded_t work = ptest_guarded_make(t, info.work_bytes);
        png_info_t decoded_info;
        const png_status_t decoded = png_decode(fixture->bytes, fixture->length, pixels.data, pixels.capacity,
                                                work.data, work.capacity, &decoded_info, &error);
        if (decoded != PNG_OK) {
            printf("%s: %s\n", fixture->name, error.message);
        }
        CHECK_EQ_U64(decoded, PNG_OK);
        if (ptest_fnv1a(pixels.data, pixels.capacity) != fixture->hash) {
            printf("%s: wrong pixels\n", fixture->name);
            CHECK(false);
        }
        CHECK_MEM_EQ(&decoded_info, &info, sizeof(info));

        /* One byte short of either buffer is refused before anything is written past it. */
        CHECK_EQ_U64(png_decode(fixture->bytes, fixture->length, pixels.data, pixels.capacity - 1u, work.data,
                                work.capacity, nullptr, &error),
                     PNG_NO_ROOM);
        CHECK_EQ_U64(png_decode(fixture->bytes, fixture->length, pixels.data, pixels.capacity, work.data,
                                work.capacity - 1u, nullptr, &error),
                     PNG_NO_ROOM);
        ptest_guarded_open(&pixels);
        ptest_guarded_open(&work);
    }
}

TEST(every_truncation_says_truncated) {
    ptest_fixture_t *fixtures = test_alloc(t, sizeof(ptest_fixture_t) * PTEST_FIXTURES_EXPECTED);
    const uint32_t count = ptest_load_fixtures(t, fixtures);
    REQUIRE(count == PTEST_FIXTURES_EXPECTED);
    for (uint32_t index = 0u; index < count; index += 1u) {
        const ptest_fixture_t *fixture = &fixtures[index];
        if (fixture->length > 2000u) {
            continue;
        }
        for (uint64_t cut = 0u; cut < fixture->length; cut += 1u) {
            png_info_t info;
            png_error_t error;
            CHECK_EQ_U64(png_probe(fixture->bytes, cut, &info, &error), PNG_TRUNCATED);
            CHECK_EQ_U64(info.width, 0u);
        }
    }
}

/* ---- The exact values ---- */

TEST(pixels_scaled_from_each_depth) {
    /* gray 1: 0 1 */
    ptest_file_t file = ptest_file_make(t);
    ptest_ihdr(&file, 2u, 1u, 1u, PNG_COLOR_GRAY, 0u);
    static const uint8_t gray1[] = {0u, 0x40u};
    ptest_idat(&file, gray1, sizeof(gray1));
    ptest_iend(&file);
    png_info_t info;
    uint8_t *pixels = ptest_decode_ok(t, &file, &info);
    REQUIRE(pixels != nullptr);
    static const uint8_t gray1_out[] = {0u, 0u, 0u, 255u, 255u, 255u, 255u, 255u};
    CHECK_MEM_EQ(pixels, gray1_out, sizeof(gray1_out));

    /* gray 2: 0 1 2 3 */
    file = ptest_file_make(t);
    ptest_ihdr(&file, 4u, 1u, 2u, PNG_COLOR_GRAY, 0u);
    static const uint8_t gray2[] = {0u, 0x1Bu};
    ptest_idat(&file, gray2, sizeof(gray2));
    ptest_iend(&file);
    pixels = ptest_decode_ok(t, &file, &info);
    REQUIRE(pixels != nullptr);
    static const uint8_t gray2_out[] = {0u,   0u,   0u,   255u, 85u,  85u,  85u,  255u,
                                        170u, 170u, 170u, 255u, 255u, 255u, 255u, 255u};
    CHECK_MEM_EQ(pixels, gray2_out, sizeof(gray2_out));

    /* gray 4: 1 15 */
    file = ptest_file_make(t);
    ptest_ihdr(&file, 2u, 1u, 4u, PNG_COLOR_GRAY, 0u);
    static const uint8_t gray4[] = {0u, 0x1Fu};
    ptest_idat(&file, gray4, sizeof(gray4));
    ptest_iend(&file);
    pixels = ptest_decode_ok(t, &file, &info);
    REQUIRE(pixels != nullptr);
    static const uint8_t gray4_out[] = {17u, 17u, 17u, 255u, 255u, 255u, 255u, 255u};
    CHECK_MEM_EQ(pixels, gray4_out, sizeof(gray4_out));

    /* gray 16: the rounding edges of scaling. 0x0080 rounds down, 0x0081 up, and the ends stay at the ends. */
    file = ptest_file_make(t);
    ptest_ihdr(&file, 4u, 1u, 16u, PNG_COLOR_GRAY, 0u);
    static const uint8_t gray16[] = {0u, 0x00u, 0x80u, 0x00u, 0x81u, 0x00u, 0x00u, 0xFFu, 0xFFu};
    ptest_idat(&file, gray16, sizeof(gray16));
    ptest_iend(&file);
    pixels = ptest_decode_ok(t, &file, &info);
    REQUIRE(pixels != nullptr);
    CHECK_EQ_U64(pixels[0], 0u);
    CHECK_EQ_U64(pixels[4], 1u);
    CHECK_EQ_U64(pixels[8], 0u);
    CHECK_EQ_U64(pixels[12], 255u);
}

TEST(palette_with_partial_transparency) {
    ptest_file_t file = ptest_file_make(t);
    ptest_ihdr(&file, 3u, 1u, 2u, PNG_COLOR_PALETTE, 0u);
    static const uint8_t palette[] = {10u, 20u, 30u, 40u, 50u, 60u, 70u, 80u, 90u};
    ptest_chunk(&file, "PLTE", palette, sizeof(palette));
    static const uint8_t alpha[] = {100u};
    ptest_chunk(&file, "tRNS", alpha, sizeof(alpha));
    static const uint8_t raw[] = {0u, 0x24u}; /* 0 2 1 */
    ptest_idat(&file, raw, sizeof(raw));
    ptest_iend(&file);
    png_info_t info;
    const uint8_t *pixels = ptest_decode_ok(t, &file, &info);
    REQUIRE(pixels != nullptr);
    CHECK_EQ_U64(info.has_alpha, 1u);
    static const uint8_t expected[] = {10u, 20u, 30u, 100u, 70u, 80u, 90u, 255u, 40u, 50u, 60u, 255u};
    CHECK_MEM_EQ(pixels, expected, sizeof(expected));
}

TEST(color_key_compares_all_sixteen_bits) {
    /* Both samples scale to the same 8 bits, and only the exact one is transparent. */
    ptest_file_t file = ptest_file_make(t);
    ptest_ihdr(&file, 2u, 1u, 16u, PNG_COLOR_GRAY, 0u);
    static const uint8_t key[] = {0x12u, 0x34u};
    ptest_chunk(&file, "tRNS", key, sizeof(key));
    static const uint8_t raw[] = {0u, 0x12u, 0x34u, 0x12u, 0x35u};
    ptest_idat(&file, raw, sizeof(raw));
    ptest_iend(&file);
    png_info_t info;
    const uint8_t *pixels = ptest_decode_ok(t, &file, &info);
    REQUIRE(pixels != nullptr);
    CHECK_EQ_U64(pixels[0], pixels[4]);
    CHECK_EQ_U64(pixels[3], 0u);
    CHECK_EQ_U64(pixels[7], 255u);

    /* An RGB key needs all three samples to match. */
    file = ptest_file_make(t);
    ptest_ihdr(&file, 2u, 1u, 8u, PNG_COLOR_RGB, 0u);
    static const uint8_t rgb_key[] = {0u, 1u, 0u, 2u, 0u, 3u};
    ptest_chunk(&file, "tRNS", rgb_key, sizeof(rgb_key));
    static const uint8_t rgb_raw[] = {0u, 1u, 2u, 3u, 1u, 2u, 4u};
    ptest_idat(&file, rgb_raw, sizeof(rgb_raw));
    ptest_iend(&file);
    pixels = ptest_decode_ok(t, &file, &info);
    REQUIRE(pixels != nullptr);
    CHECK_EQ_U64(pixels[3], 0u);
    CHECK_EQ_U64(pixels[7], 255u);
}

TEST(filters_by_hand) {
    /* Five rows of three gray pixels, one row per filter, worked out on paper. */
    ptest_file_t file = ptest_file_make(t);
    ptest_ihdr(&file, 3u, 5u, 8u, PNG_COLOR_GRAY, 0u);
    static const uint8_t raw[] = {
        0u, 10u,  20u,  30u,  /* none: 10 20 30 */
        1u, 1u,   2u,   3u,   /* sub: 1 3 6 */
        2u, 1u,   1u,   1u,   /* up: 2 4 7 */
        3u, 1u,   1u,   1u,   /* average: 2 = 1 + (0+2)/2; 4 = 1 + (2+4)/2; 6 = 1 + (4+7)/2 */
        4u, 250u, 250u, 250u, /* paeth: 252 from the 2 above; then see below */
    };
    ptest_idat(&file, raw, sizeof(raw));
    ptest_iend(&file);
    png_info_t info;
    const uint8_t *pixels = ptest_decode_ok(t, &file, &info);
    REQUIRE(pixels != nullptr);
    static const uint8_t expected[15] = {
        10u,
        20u,
        30u,
        1u,
        3u,
        6u,
        2u,
        4u,
        7u,
        2u,
        4u,
        6u,
        /* Row 5, x=0: left 0, up 2, up-left 0: estimate 2, nearest up, so 250 + 2 = 252.
         * x=1: left 252, up 4, up-left 2: estimate 254, nearest left, so 250 + 252 = 502 mod 256 = 246.
         * x=2: left 246, up 6, up-left 4: estimate 248, nearest left, so 250 + 246 = 496 mod 256 = 240. */
        252u,
        246u,
        240u,
    };
    for (uint32_t index = 0u; index < 15u; index += 1u) {
        CHECK_EQ_U64(pixels[index * 4u], expected[index]);
    }
}

TEST(idat_split_at_every_byte) {
    /* Build one zlib stream, then split it into one-byte chunks with empty ones between. */
    ptest_file_t whole = ptest_file_make(t);
    ptest_valid_start(&whole);
    const uint64_t idat_offset = whole.length;
    ptest_idat(&whole, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    const uint32_t stream_length = ptest_get_u32(whole.bytes + idat_offset);
    const uint8_t *stream = whole.bytes + idat_offset + 8u;

    ptest_file_t split = ptest_file_make(t);
    ptest_valid_start(&split);
    for (uint32_t index = 0u; index < stream_length; index += 1u) {
        ptest_chunk(&split, "IDAT", stream + index, 1u);
        ptest_chunk(&split, "IDAT", nullptr, 0u);
    }
    ptest_iend(&split);
    png_info_t info;
    const uint8_t *pixels = ptest_decode_ok(t, &split, &info);
    REQUIRE(pixels != nullptr);
    CHECK_EQ_U64(info.idat_bytes, stream_length);
    static const uint8_t expected[16] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u};
    CHECK_MEM_EQ(pixels, expected, 16u);
}

TEST(width_at_the_limit_decodes) {
    /* A row of PNG_MAX_DIMENSION pixels, all set. */
    const uint32_t row_bytes = PNG_MAX_DIMENSION / 8u;
    uint8_t *raw = test_alloc(t, 1ull + row_bytes);
    memset(raw + 1u, 0xFF, row_bytes);
    ptest_file_t file = ptest_file_make(t);
    ptest_ihdr(&file, PNG_MAX_DIMENSION, 1u, 1u, PNG_COLOR_GRAY, 0u);
    ptest_idat(&file, raw, 1u + row_bytes);
    ptest_iend(&file);
    png_info_t info;
    const uint8_t *pixels = ptest_decode_ok(t, &file, &info);
    REQUIRE(pixels != nullptr);
    CHECK_EQ_U64(pixels[(PNG_MAX_DIMENSION - 1u) * 4u], 255u);
}

/* ---- Refusals ---- */

TEST(refuses_bad_signature_and_short_files) {
    ptest_file_t file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    ptest_iend(&file);
    for (uint32_t index = 0u; index < 8u; index += 1u) {
        file.bytes[index] ^= 0x01u;
        PTEST_REFUSED(&file, PNG_BAD_SIGNATURE, "signature");
        file.bytes[index] ^= 0x01u;
    }
    png_info_t info;
    png_error_t error;
    CHECK_EQ_U64(png_probe(nullptr, 0u, &info, &error), PNG_TRUNCATED);
    CHECK_CONTAINS(error.message, "too short");
    ptest_file_t empty = ptest_file_make(t);
    PTEST_REFUSED(&empty, PNG_TRUNCATED, "IEND");
}

TEST(refuses_bad_crc_on_any_chunk) {
    ptest_file_t file = ptest_file_make(t);
    ptest_valid_start(&file);
    const uint64_t ancillary = file.length;
    ptest_chunk(&file, "tEXt", "a\0b", 3u);
    const uint64_t idat = file.length;
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    const uint64_t iend = file.length;
    ptest_iend(&file);
    const uint64_t offsets[4] = {8u, ancillary, idat, iend};
    for (uint32_t index = 0u; index < 4u; index += 1u) {
        const uint64_t chunk_end = index < 3u ? offsets[index + 1u] : file.length;
        file.bytes[chunk_end - 1u] ^= 0x80u;
        PTEST_REFUSED(&file, PNG_BAD_CRC, "CRC");
        file.bytes[chunk_end - 1u] ^= 0x80u;
    }
}

TEST(refuses_bad_chunk_framing) {
    /* A length over 2^31 - 1, and a length past the end of the file. */
    ptest_file_t file = ptest_file_make(t);
    ptest_valid_start(&file);
    const uint64_t idat = file.length;
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    ptest_iend(&file);
    const uint32_t saved = ptest_get_u32(file.bytes + idat);
    ptest_put_u32(file.bytes + idat, 0x80000000u);
    PTEST_REFUSED(&file, PNG_BAD_CHUNK, "claims");
    ptest_put_u32(file.bytes + idat, 0xFFFFFFFFu);
    PTEST_REFUSED(&file, PNG_BAD_CHUNK, "claims");
    ptest_put_u32(file.bytes + idat, 0x7FFFFFFFu);
    PTEST_REFUSED(&file, PNG_TRUNCATED, "IDAT claims");
    ptest_put_u32(file.bytes + idat, saved);

    /* A type that is not letters. */
    file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_chunk(&file, "tE1t", "x", 1u);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_CHUNK, "letters");
}

TEST(refuses_bad_header) {
    typedef struct bad_header_t {
        uint32_t width;     /* 0 */
        uint32_t height;    /* 4 */
        uint8_t depth;      /* 8 */
        uint8_t color;      /* 9 */
        uint8_t status;     /* 10 */
        uint8_t _pad0[5];   /* 11 */
        const char *needle; /* 16 */
    } bad_header_t;
    static_assert(sizeof(bad_header_t) == 24);
    static const bad_header_t cases[] = {
        {0u, 1u, 8u, 6u, PNG_BAD_HEADER, {0u}, "zero"},
        {1u, 0u, 8u, 6u, PNG_BAD_HEADER, {0u}, "zero"},
        {PNG_MAX_DIMENSION + 1u, 1u, 8u, 6u, PNG_TOO_LARGE, {0u}, "limit"},
        {1u, PNG_MAX_DIMENSION + 1u, 8u, 6u, PNG_TOO_LARGE, {0u}, "limit"},
        {0xFFFFFFFFu, 0xFFFFFFFFu, 16u, 6u, PNG_TOO_LARGE, {0u}, "limit"},
        {1u, 1u, 3u, 0u, PNG_BAD_HEADER, {0u}, "not defined"},
        {1u, 1u, 0u, 0u, PNG_BAD_HEADER, {0u}, "not defined"},
        {1u, 1u, 32u, 0u, PNG_BAD_HEADER, {0u}, "not defined"},
        {1u, 1u, 4u, 2u, PNG_BAD_HEADER, {0u}, "not defined"},
        {1u, 1u, 16u, 3u, PNG_BAD_HEADER, {0u}, "not defined"},
        {1u, 1u, 4u, 4u, PNG_BAD_HEADER, {0u}, "not defined"},
        {1u, 1u, 1u, 6u, PNG_BAD_HEADER, {0u}, "not defined"},
        {1u, 1u, 8u, 1u, PNG_BAD_HEADER, {0u}, "not defined"},
        {1u, 1u, 8u, 5u, PNG_BAD_HEADER, {0u}, "not defined"},
        {1u, 1u, 8u, 7u, PNG_BAD_HEADER, {0u}, "not defined"},
    };
    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); index += 1u) {
        ptest_file_t file = ptest_file_make(t);
        ptest_ihdr(&file, cases[index].width, cases[index].height, cases[index].depth, cases[index].color, 0u);
        ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
        ptest_iend(&file);
        PTEST_REFUSED(&file, (png_status_t)cases[index].status, cases[index].needle);
    }

    /* The three method bytes, and IHDR's length. */
    for (uint32_t field = 10u; field <= 12u; field += 1u) {
        ptest_file_t file = ptest_file_make(t);
        uint8_t header[13];
        ptest_put_u32(header, 2u);
        ptest_put_u32(header + 4u, 2u);
        header[8] = 8u;
        header[9] = PNG_COLOR_RGBA;
        header[10] = 0u;
        header[11] = 0u;
        header[12] = 0u;
        header[field] = 2u;
        ptest_chunk(&file, "IHDR", header, 13u);
        ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
        ptest_iend(&file);
        PTEST_REFUSED(&file, PNG_BAD_HEADER, "is not defined");
    }
    ptest_file_t file = ptest_file_make(t);
    ptest_chunk(&file, "IHDR", "\0\0\0\2\0\0\0\2\10\6\0\0", 12u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_HEADER, "must hold 13");
}

TEST(refuses_bad_order) {
    ptest_file_t file = ptest_file_make(t);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_ORDER, "must be IHDR");

    file = ptest_file_make(t);
    ptest_chunk(&file, "tEXt", "a\0b", 3u);
    ptest_valid_start(&file);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_ORDER, "must be IHDR");

    file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_valid_start(&file);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_ORDER, "second IHDR");

    file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_ORDER, "no IDAT");

    /* No IEND at all: the chunks stop at a clean boundary. */
    file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    PTEST_REFUSED(&file, PNG_TRUNCATED, "before an IEND");

    /* Separated IDAT, even by a chunk that may go anywhere. */
    file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    ptest_chunk(&file, "tEXt", "a\0b", 3u);
    ptest_chunk(&file, "IDAT", nullptr, 0u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_ORDER, "consecutive");

    /* IEND with data, and anything after IEND. */
    file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    ptest_chunk(&file, "IEND", "x", 1u);
    PTEST_REFUSED(&file, PNG_BAD_ORDER, "must be empty");

    file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    ptest_iend(&file);
    file.bytes[file.length] = 0u;
    file.length += 1u;
    PTEST_REFUSED(&file, PNG_BAD_ORDER, "1 bytes follow IEND");
}

TEST(refuses_unknown_critical_chunk_and_skips_ancillary) {
    ptest_file_t file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_chunk(&file, "XXXX", "x", 1u);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_UNKNOWN_CRITICAL, "XXXX");

    file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_chunk(&file, "xXXX", "x", 1u);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    ptest_chunk(&file, "xXXX", "x", 1u);
    ptest_iend(&file);
    png_info_t info;
    CHECK(ptest_decode_ok(t, &file, &info) != nullptr);
}

static const uint8_t PTEST_PALETTE_RAW[] = {0u, 0u};
static const uint8_t PTEST_THREE_ENTRIES[] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u};

TEST(refuses_bad_palette) {
    /* Missing on a palette image. */
    ptest_file_t file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_PALETTE, 0u);
    ptest_idat(&file, PTEST_PALETTE_RAW, 2u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_PALETTE, "no PLTE");

    /* On gray images. */
    for (uint32_t color = 0u; color <= 4u; color += 4u) {
        file = ptest_file_make(t);
        ptest_ihdr(&file, 1u, 1u, 8u, (uint8_t)color, 0u);
        ptest_chunk(&file, "PLTE", PTEST_THREE_ENTRIES, 9u);
        ptest_idat(&file, PTEST_PALETTE_RAW, 2u);
        ptest_iend(&file);
        PTEST_REFUSED(&file, PNG_BAD_PALETTE, "grayscale");
    }

    /* Sizes: not in threes, empty, past 256 entries. */
    static const uint8_t many[771] = {0u};
    const uint32_t lengths[3] = {8u, 0u, 771u};
    for (uint32_t index = 0u; index < 3u; index += 1u) {
        file = ptest_file_make(t);
        ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_PALETTE, 0u);
        ptest_chunk(&file, "PLTE", many, lengths[index]);
        ptest_idat(&file, PTEST_PALETTE_RAW, 2u);
        ptest_iend(&file);
        PTEST_REFUSED(&file, PNG_BAD_PALETTE, "in threes");
    }

    /* More entries than the depth can name. */
    file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 1u, PNG_COLOR_PALETTE, 0u);
    ptest_chunk(&file, "PLTE", PTEST_THREE_ENTRIES, 9u);
    ptest_idat(&file, PTEST_PALETTE_RAW, 2u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_PALETTE, "at bit depth 1");

    /* Twice, after IDAT, and after tRNS. */
    file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_PALETTE, 0u);
    ptest_chunk(&file, "PLTE", PTEST_THREE_ENTRIES, 9u);
    ptest_chunk(&file, "PLTE", PTEST_THREE_ENTRIES, 9u);
    ptest_idat(&file, PTEST_PALETTE_RAW, 2u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_ORDER, "second PLTE");

    file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_PALETTE, 0u);
    ptest_chunk(&file, "PLTE", PTEST_THREE_ENTRIES, 9u);
    ptest_idat(&file, PTEST_PALETTE_RAW, 2u);
    ptest_chunk(&file, "PLTE", PTEST_THREE_ENTRIES, 9u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_ORDER, "after IDAT");

    file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_RGB, 0u);
    static const uint8_t key[] = {0u, 1u, 0u, 2u, 0u, 3u};
    ptest_chunk(&file, "tRNS", key, 6u);
    ptest_chunk(&file, "PLTE", PTEST_THREE_ENTRIES, 9u);
    static const uint8_t rgb_raw[] = {0u, 1u, 2u, 3u};
    ptest_idat(&file, rgb_raw, 4u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_ORDER, "after tRNS");
}

TEST(refuses_palette_index_past_the_end) {
    /* Depth 2 can name 4 entries, and only 3 exist. The last pixel uses the one that does not. */
    ptest_file_t file = ptest_file_make(t);
    ptest_ihdr(&file, 4u, 1u, 2u, PNG_COLOR_PALETTE, 0u);
    ptest_chunk(&file, "PLTE", PTEST_THREE_ENTRIES, 9u);
    static const uint8_t raw[] = {0u, 0x1Bu};
    ptest_idat(&file, raw, sizeof(raw));
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_INDEX, "index 3 of 3");
}

TEST(refuses_bad_transparency) {
    static const uint8_t bytes[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    static const uint8_t four_alphas[4] = {1u, 2u, 3u, 4u};

    /* On images that already have alpha. */
    static const uint8_t alpha_colors[] = {PNG_COLOR_GRAY_ALPHA, PNG_COLOR_RGBA};
    for (uint32_t index = 0u; index < 2u; index += 1u) {
        ptest_file_t file = ptest_file_make(t);
        ptest_ihdr(&file, 1u, 1u, 8u, alpha_colors[index], 0u);
        ptest_chunk(&file, "tRNS", bytes, 2u);
        ptest_idat(&file, PTEST_RAW_2X2, 3u);
        ptest_iend(&file);
        PTEST_REFUSED(&file, PNG_BAD_TRANSPARENCY, "alpha channel");
    }

    /* Wrong sizes. */
    ptest_file_t file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_GRAY, 0u);
    ptest_chunk(&file, "tRNS", bytes, 3u);
    ptest_idat(&file, PTEST_PALETTE_RAW, 2u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_TRANSPARENCY, "must hold 2");

    file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_RGB, 0u);
    ptest_chunk(&file, "tRNS", bytes, 4u);
    ptest_idat(&file, PTEST_RAW_2X2, 4u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_TRANSPARENCY, "must hold 6");

    /* A key too big for the depth. */
    file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 4u, PNG_COLOR_GRAY, 0u);
    static const uint8_t big_key[] = {0u, 16u};
    ptest_chunk(&file, "tRNS", big_key, 2u);
    ptest_idat(&file, PTEST_PALETTE_RAW, 2u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_TRANSPARENCY, "does not fit");

    /* A palette image: before PLTE, longer than PLTE, empty. */
    file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_PALETTE, 0u);
    ptest_chunk(&file, "tRNS", four_alphas, 1u);
    ptest_chunk(&file, "PLTE", PTEST_THREE_ENTRIES, 9u);
    ptest_idat(&file, PTEST_PALETTE_RAW, 2u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_ORDER, "before PLTE");

    file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_PALETTE, 0u);
    ptest_chunk(&file, "PLTE", PTEST_THREE_ENTRIES, 9u);
    ptest_chunk(&file, "tRNS", four_alphas, 4u);
    ptest_idat(&file, PTEST_PALETTE_RAW, 2u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_TRANSPARENCY, "palette entries");

    file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_PALETTE, 0u);
    ptest_chunk(&file, "PLTE", PTEST_THREE_ENTRIES, 9u);
    ptest_chunk(&file, "tRNS", nullptr, 0u);
    ptest_idat(&file, PTEST_PALETTE_RAW, 2u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_TRANSPARENCY, "palette entries");

    /* Twice, and after IDAT. */
    file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_GRAY, 0u);
    ptest_chunk(&file, "tRNS", bytes, 2u);
    ptest_chunk(&file, "tRNS", bytes, 2u);
    ptest_idat(&file, PTEST_PALETTE_RAW, 2u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_ORDER, "second tRNS");

    file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_GRAY, 0u);
    ptest_idat(&file, PTEST_PALETTE_RAW, 2u);
    ptest_chunk(&file, "tRNS", bytes, 2u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_ORDER, "tRNS comes after IDAT");
}

TEST(refuses_bad_image_data) {
    /* Too little, and too much, by one byte. The checksum and framing are good, so only the size is wrong. */
    ptest_file_t file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2) - 1u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_DATA, "holds 17 bytes");

    static const uint8_t longer[19] = {0u};
    file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_idat(&file, longer, sizeof(longer));
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_DATA, "more bytes than expected");

    /* A filter type of 5, on the first row and on the last. */
    uint8_t raw[18];
    memcpy(raw, PTEST_RAW_2X2, sizeof(raw));
    raw[0] = 5u;
    file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_idat(&file, raw, sizeof(raw));
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_FILTER, "row 0 has filter type 5");
    raw[0] = 0u;
    raw[9] = 255u;
    file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_idat(&file, raw, sizeof(raw));
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_FILTER, "row 1 has filter type 255");

    /* Not zlib at all, and the wrong checksum. */
    file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_chunk(&file, "IDAT", "not zlib data", 13u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_DATA, "zlib header");

    file = ptest_file_make(t);
    ptest_valid_start(&file);
    const uint64_t idat = file.length;
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    ptest_iend(&file);
    const uint32_t stream_length = ptest_get_u32(file.bytes + idat);
    file.bytes[idat + 8u + stream_length - 1u] ^= 1u;
    ptest_put_u32(file.bytes + idat + 8u + stream_length,
                  ptest_crc(file.bytes + idat + 4u, (uint64_t)stream_length + 4u));
    PTEST_REFUSED(&file, PNG_BAD_DATA, "Adler-32");

    /* Bytes after the zlib stream, inside IDAT. */
    file = ptest_file_make(t);
    ptest_valid_start(&file);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    /* Rewind over the chunk just written and rewrite it with one more payload byte. */
    const uint64_t second = idat;
    const uint32_t old_length = ptest_get_u32(file.bytes + second);
    file.bytes[second + 8u + old_length] = 0u;
    ptest_put_u32(file.bytes + second, old_length + 1u);
    ptest_put_u32(file.bytes + second + 8u + old_length + 1u,
                  ptest_crc(file.bytes + second + 4u, (uint64_t)old_length + 5u));
    file.length = second + 12u + old_length + 1u;
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_DATA, "follow the end");
}

TEST(interlaced_data_size_is_exact) {
    /* A 1 by 1 Adam7 image is one pass of one row: 2 bytes for 8-bit gray. The empty passes have no filter bytes. */
    ptest_file_t file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_GRAY, 1u);
    static const uint8_t raw[3] = {0u, 77u, 0u};
    ptest_idat(&file, raw, 2u);
    ptest_iend(&file);
    png_info_t info;
    const uint8_t *pixels = ptest_decode_ok(t, &file, &info);
    REQUIRE(pixels != nullptr);
    CHECK_EQ_U64(info.raw_bytes, 2u);
    CHECK_EQ_U64(pixels[0], 77u);

    file = ptest_file_make(t);
    ptest_ihdr(&file, 1u, 1u, 8u, PNG_COLOR_GRAY, 1u);
    ptest_idat(&file, raw, 3u);
    ptest_iend(&file);
    PTEST_REFUSED(&file, PNG_BAD_DATA, "more bytes than expected");
}

TEST(decompression_bomb_is_refused_before_memory) {
    /* Enormous claim, tiny file. Nothing is allocated for it: probe says no, with nothing but the file to go on. */
    ptest_file_t file = ptest_file_make(t);
    ptest_ihdr(&file, PNG_MAX_DIMENSION, PNG_MAX_DIMENSION, 16u, PNG_COLOR_RGBA, 1u);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    ptest_iend(&file);
    png_info_t info;
    png_error_t error;
    CHECK_EQ_U64(png_probe(file.bytes, file.length, &info, &error), PNG_BAD_DATA);
    CHECK_CONTAINS(error.message, "cannot expand");
    CHECK_EQ_U64(info.pixel_bytes, 0u);

    /* png_decode says the same, and with no buffers at all it does not touch them. */
    CHECK_EQ_U64(png_decode(file.bytes, file.length, nullptr, 0u, nullptr, 0u, nullptr, &error), PNG_BAD_DATA);
}

TEST(a_real_bomb_stops_at_the_claimed_size) {
    /* A real zlib stream of zeros far bigger than the image claims. It stops when the work buffer's raw part is full. */
    enum : uint32_t { DECLARED = 16u };
    uint8_t *stream_bytes = test_alloc(t, 4096u);
    /* Fixed block: a literal 0, then matches of 258 at distance 1 until far past any image this small. */
    uint64_t bit_count = 0u;
    stream_bytes[0] = 0x78u;
    stream_bytes[1] = 0x01u;
    uint8_t *deflate = stream_bytes + 2u;
#define PTEST_BIT(value)                                                                                               \
    do {                                                                                                               \
        if ((value) != 0u) {                                                                                           \
            deflate[bit_count / 8u] = (uint8_t)(deflate[bit_count / 8u] | (1u << (bit_count % 8u)));                   \
        }                                                                                                              \
        bit_count += 1u;                                                                                               \
    } while (false)
    PTEST_BIT(1u); /* final */
    PTEST_BIT(1u); /* fixed: BTYPE 01, lowest bit first */
    PTEST_BIT(0u);
    for (uint32_t bit = 8u; bit > 0u; bit -= 1u) {
        PTEST_BIT((0x30u >> (bit - 1u)) & 1u); /* literal 0 */
    }
    for (uint32_t round = 0u; round < 2000u; round += 1u) {
        for (uint32_t bit = 8u; bit > 0u; bit -= 1u) {
            PTEST_BIT((0xC5u >> (bit - 1u)) & 1u); /* symbol 285: length 258 */
        }
        for (uint32_t bit = 0u; bit < 5u; bit += 1u) {
            PTEST_BIT(0u); /* distance symbol 0: distance 1 */
        }
    }
#undef PTEST_BIT
    ptest_file_t file = ptest_file_make(t);
    ptest_ihdr(&file, 3u, 1u, 8u, PNG_COLOR_RGB, 0u);
    ptest_chunk(&file, "IDAT", stream_bytes, (uint32_t)(2u + bit_count / 8u + 8u));
    ptest_iend(&file);
    png_info_t info;
    png_error_t error;
    REQUIRE(png_probe(file.bytes, file.length, &info, &error) == PNG_OK);
    CHECK(info.raw_bytes < DECLARED);
    ptest_guarded_t pixels = ptest_guarded_make(t, info.pixel_bytes);
    /* The IDAT copy has to fit too. The raw part comes first, so an overrun would corrupt the input it is reading. */
    ptest_guarded_t work = ptest_guarded_make(t, info.work_bytes);
    CHECK_EQ_U64(
        png_decode(file.bytes, file.length, pixels.data, pixels.capacity, work.data, work.capacity, nullptr, &error),
        PNG_BAD_DATA);
    CHECK_CONTAINS(error.message, "more bytes than expected");
    ptest_guarded_open(&pixels);
    ptest_guarded_open(&work);
}

TEST(errors_report_the_offset) {
    ptest_file_t file = ptest_file_make(t);
    ptest_valid_start(&file);
    const uint64_t offset = file.length;
    ptest_chunk(&file, "XXXX", nullptr, 0u);
    ptest_idat(&file, PTEST_RAW_2X2, sizeof(PTEST_RAW_2X2));
    ptest_iend(&file);
    png_info_t info;
    png_error_t error;
    CHECK_EQ_U64(png_probe(file.bytes, file.length, &info, &error), PNG_UNKNOWN_CRITICAL);
    CHECK_EQ_U64(error.offset, offset);
    CHECK_EQ_U64(error.status, PNG_UNKNOWN_CRITICAL);
}

/* ---- Hostile input ---- */

/* Rewrites every chunk's CRC, so that a change reaches the code behind the CRC check. Stops at a broken length. */
static void ptest_fix_crcs(uint8_t *bytes, uint64_t length) {
    uint64_t offset = 8u;
    while (length - offset >= 12u) {
        const uint64_t chunk_length = ptest_get_u32(bytes + offset);
        if (chunk_length > length - offset - 12u) {
            return;
        }
        ptest_put_u32(bytes + offset + 8u + chunk_length, ptest_crc(bytes + offset + 4u, chunk_length + 4u));
        offset += 12u + chunk_length;
    }
}

static uint64_t ptest_next_random(uint64_t *state) {
    uint64_t value = *state;
    value ^= value << 13u;
    value ^= value >> 7u;
    value ^= value << 17u;
    *state = value;
    return value;
}

/* True if this byte of a good file lies in the payload of an IDAT chunk. */
static bool ptest_in_idat(const uint8_t *bytes, uint64_t length, uint64_t position) {
    uint64_t offset = 8u;
    while (offset < length) {
        const uint64_t chunk_length = ptest_get_u32(bytes + offset);
        if (position >= offset + 8u && position < offset + 8u + chunk_length) {
            return memcmp(bytes + offset + 4u, "IDAT", 4u) == 0;
        }
        offset += 12u + chunk_length;
    }
    return false;
}

TEST(hostile_every_bit_flipped) {
    ptest_fixture_t *fixtures = test_alloc(t, sizeof(ptest_fixture_t) * PTEST_FIXTURES_EXPECTED);
    const uint32_t count = ptest_load_fixtures(t, fixtures);
    REQUIRE(count == PTEST_FIXTURES_EXPECTED);
    ptest_guarded_t input = ptest_guarded_make(t, 4096u);
    ptest_guarded_t pixels = ptest_guarded_make(t, PTEST_MUTATION_PIXEL_BYTES);
    ptest_guarded_t work = ptest_guarded_make(t, PTEST_MUTATION_WORK_BYTES);
    uint32_t tried = 0u;
    uint32_t refused_in_idat = 0u;
    for (uint32_t index = 0u; index < count; index += 1u) {
        const ptest_fixture_t *fixture = &fixtures[index];
        if (fixture->length > 400u) {
            continue;
        }
        tried += 1u;
        uint8_t *source = input.data + input.capacity - fixture->length;
        memcpy(source, fixture->bytes, fixture->length);

        /* Plain flips, which the signature and the CRCs must catch, every one. */
        for (uint64_t bit = 0u; bit < fixture->length * 8u; bit += 1u) {
            source[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            png_error_t error;
            const png_status_t status = png_decode(source, fixture->length, pixels.data, pixels.capacity, work.data,
                                                   work.capacity, nullptr, &error);
            CHECK(status != PNG_OK);
            CHECK(error.message[0] != '\0');
            source[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        }

        /* The same with the CRCs repaired, so the flip reaches the chunk rules and the inflater, in buffers of the exact
         * size against their guard pages. A flip in IDAT that still decodes can only have hit something ignored, such as
         * the padding before a stored block, and must give the same image: the Adler-32 is there to promise that. */
        for (uint64_t bit = 8u * 8u; bit < fixture->length * 8u; bit += 1u) {
            memcpy(source, fixture->bytes, fixture->length);
            source[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            ptest_fix_crcs(source, fixture->length);
            png_info_t info;
            png_error_t error;
            if (png_probe(source, fixture->length, &info, &error) != PNG_OK || info.pixel_bytes > pixels.capacity ||
                info.work_bytes > work.capacity) {
                continue;
            }
            uint8_t *exact_pixels = pixels.data + pixels.capacity - info.pixel_bytes;
            const png_status_t status =
                png_decode(source, fixture->length, exact_pixels, info.pixel_bytes,
                           work.data + work.capacity - info.work_bytes, info.work_bytes, nullptr, &error);
            if (ptest_in_idat(fixture->bytes, fixture->length, bit / 8u)) {
                if (status == PNG_OK) {
                    CHECK_EQ_U64(ptest_fnv1a(exact_pixels, info.pixel_bytes), fixture->hash);
                } else {
                    refused_in_idat += 1u;
                }
            }
        }
    }
    ptest_guarded_open(&input);
    ptest_guarded_open(&pixels);
    ptest_guarded_open(&work);
    CHECK(tried >= 10u);
    CHECK(refused_in_idat > 1000u);
}

TEST(hostile_random_mutations) {
    ptest_fixture_t *fixtures = test_alloc(t, sizeof(ptest_fixture_t) * PTEST_FIXTURES_EXPECTED);
    const uint32_t count = ptest_load_fixtures(t, fixtures);
    REQUIRE(count == PTEST_FIXTURES_EXPECTED);
    ptest_guarded_t input = ptest_guarded_make(t, 16384u);
    ptest_guarded_t pixels = ptest_guarded_make(t, PTEST_MUTATION_PIXEL_BYTES);
    ptest_guarded_t work = ptest_guarded_make(t, PTEST_MUTATION_WORK_BYTES);
    uint64_t random = 0x2026110200000002ull;
    uint32_t statuses_seen = 0u;
    uint32_t decoded_rounds = 0u;
    for (uint32_t round = 0u; round < PTEST_MUTATION_ROUNDS; round += 1u) {
        const ptest_fixture_t *fixture = &fixtures[ptest_next_random(&random) % count];
        if (fixture->length > input.capacity) {
            continue;
        }
        uint8_t *source = input.data + input.capacity - fixture->length;
        memcpy(source, fixture->bytes, fixture->length);
        const uint32_t changes = (uint32_t)(ptest_next_random(&random) % 6u) + 1u;
        for (uint32_t change = 0u; change < changes; change += 1u) {
            /* After the signature. Half go near the start, where IHDR and the first bytes of the stream are. */
            const uint64_t span = fixture->length - 8u;
            const uint64_t near = span < 64u ? span : 64u;
            const uint64_t limit = (ptest_next_random(&random) & 1u) != 0u ? near : span;
            source[8u + ptest_next_random(&random) % limit] = (uint8_t)ptest_next_random(&random);
        }
        ptest_fix_crcs(source, fixture->length);

        png_info_t info;
        png_error_t error;
        png_status_t status = png_probe(source, fixture->length, &info, &error);
        if (status == PNG_OK && info.pixel_bytes <= pixels.capacity && info.work_bytes <= work.capacity) {
            status =
                png_decode(source, fixture->length, pixels.data + pixels.capacity - info.pixel_bytes, info.pixel_bytes,
                           work.data + work.capacity - info.work_bytes, info.work_bytes, nullptr, &error);
            decoded_rounds += 1u;
        }
        CHECK(status <= PNG_NO_ROOM);
        CHECK(status == PNG_OK || error.message[0] != '\0');
        statuses_seen |= 1u << status;
    }
    ptest_guarded_open(&input);
    ptest_guarded_open(&pixels);
    ptest_guarded_open(&work);
    /* The mutations have to have reached every layer for this to prove anything. */
    CHECK(decoded_rounds > 1000u);
    /* Not PNG_BAD_FILTER or PNG_BAD_INDEX: a changed stream fails its Adler-32 first. The next test goes behind it. */
    const uint32_t wanted = (1u << PNG_OK) | (1u << PNG_BAD_CHUNK) | (1u << PNG_TRUNCATED) | (1u << PNG_BAD_HEADER) |
                            (1u << PNG_TOO_LARGE) | (1u << PNG_BAD_DATA);
    CHECK_EQ_U64(statuses_seen & wanted, wanted);
}

/* The filter and pixel code sits behind the Adler-32, so random changes to a stream never reach it. These are good files
 * with a good checksum, around random image data, in every format, so the worst possible pixels do reach it. */
TEST(hostile_random_image_data) {
    static const uint8_t formats[][2] = {{1u, 0u}, {2u, 0u},  {4u, 0u},  {8u, 0u}, {16u, 0u},
                                         {8u, 2u}, {16u, 2u}, {1u, 3u},  {2u, 3u}, {4u, 3u},
                                         {8u, 3u}, {8u, 4u},  {16u, 4u}, {8u, 6u}, {16u, 6u}};
    enum : uint32_t { FORMATS = sizeof(formats) / sizeof(formats[0]), MAX_SIDE = 17u, RAW_BYTES = 1u << 14 };
    ptest_file_t file = ptest_file_make(t);
    uint8_t *raw = test_alloc(t, RAW_BYTES);
    ptest_guarded_t input = ptest_guarded_make(t, 1u << 16);
    ptest_guarded_t pixels = ptest_guarded_make(t, PTEST_MUTATION_PIXEL_BYTES);
    ptest_guarded_t work = ptest_guarded_make(t, PTEST_MUTATION_WORK_BYTES);
    uint64_t random = 0x2026110200000003ull;
    uint32_t statuses_seen = 0u;
    for (uint32_t round = 0u; round < PTEST_IMAGE_DATA_ROUNDS; round += 1u) {
        const uint8_t *format = formats[round % FORMATS];
        const uint32_t width = (uint32_t)(ptest_next_random(&random) % MAX_SIDE) + 1u;
        const uint32_t height = (uint32_t)(ptest_next_random(&random) % MAX_SIDE) + 1u;
        const uint8_t interlace = (uint8_t)(ptest_next_random(&random) & 1u);

        /* What goes before IDAT, kept for both versions of the file. */
        file.length = 8u;
        ptest_ihdr(&file, width, height, format[0], format[1], interlace);
        if (format[1] == PNG_COLOR_PALETTE) {
            const uint32_t most = 1u << format[0];
            const uint32_t entries = (uint32_t)(ptest_next_random(&random) % most) + 1u;
            for (uint32_t index = 0u; index < entries * 3u; index += 1u) {
                raw[index] = (uint8_t)ptest_next_random(&random);
            }
            ptest_chunk(&file, "PLTE", raw, entries * 3u);
            if ((ptest_next_random(&random) & 1u) != 0u) {
                ptest_chunk(&file, "tRNS", raw, (uint32_t)(ptest_next_random(&random) % entries) + 1u);
            }
        } else if (format[1] == PNG_COLOR_GRAY && (ptest_next_random(&random) & 1u) != 0u) {
            uint8_t key[2] = {0u, 0u};
            ptest_put_u32(raw, (uint32_t)ptest_next_random(&random) &
                                   ((format[0] == 16u ? 0xFFFFu : (1u << format[0]) - 1u)));
            key[0] = raw[2];
            key[1] = raw[3];
            ptest_chunk(&file, "tRNS", key, 2u);
        }
        const uint64_t prefix = file.length;

        /* A placeholder IDAT, big enough to pass the size check, to learn how much image data this image takes. */
        ptest_chunk(&file, "IDAT", raw, 64u);
        ptest_iend(&file);
        png_info_t info;
        png_error_t error;
        REQUIRE(png_probe(file.bytes, file.length, &info, &error) == PNG_OK);
        REQUIRE(info.raw_bytes <= RAW_BYTES);

        /* In one round of eight, any bytes, so that filter types go bad. Otherwise bytes that are valid filter types,
         * so that the row reaches the filters and the pixels. */
        const bool any_bytes = (round & 7u) == 0u;
        for (uint64_t index = 0u; index < info.raw_bytes; index += 1u) {
            const uint8_t byte = (uint8_t)ptest_next_random(&random);
            raw[index] = any_bytes ? byte : (uint8_t)(byte % 5u);
        }
        file.length = prefix;
        ptest_idat(&file, raw, (uint32_t)info.raw_bytes);
        ptest_iend(&file);
        REQUIRE(file.length <= input.capacity);
        uint8_t *source = input.data + input.capacity - file.length;
        memcpy(source, file.bytes, file.length);

        REQUIRE(png_probe(source, file.length, &info, &error) == PNG_OK);
        REQUIRE(info.pixel_bytes <= pixels.capacity && info.work_bytes <= work.capacity);
        const png_status_t status =
            png_decode(source, file.length, pixels.data + pixels.capacity - info.pixel_bytes, info.pixel_bytes,
                       work.data + work.capacity - info.work_bytes, info.work_bytes, nullptr, &error);
        CHECK(status == PNG_OK || status == PNG_BAD_FILTER || status == PNG_BAD_INDEX);
        statuses_seen |= 1u << status;
    }
    ptest_guarded_open(&input);
    ptest_guarded_open(&pixels);
    ptest_guarded_open(&work);
    const uint32_t wanted = (1u << PNG_OK) | (1u << PNG_BAD_FILTER) | (1u << PNG_BAD_INDEX);
    CHECK_EQ_U64(statuses_seen, wanted);
}

TEST(every_status_has_text) {
    for (uint32_t status = 0u; status <= PNG_NO_ROOM; status += 1u) {
        CHECK(strlen(png_status_text((png_status_t)status)) > 4u);
        CHECK(strcmp(png_status_text((png_status_t)status), "unknown PNG status") != 0);
    }
    CHECK_STR_EQ(png_status_text((png_status_t)200u), "unknown PNG status");
}

TEST_MAIN()
