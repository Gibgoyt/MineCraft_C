/* ctest fuzz_png. See README.md. */
#include "fuzz/support/index.h"

#include "inflate/index.h"
#include "png/index.h"

#include <inttypes.h>
#include <string.h>

/* The first byte of an input says what the rest is. */
typedef enum fp_mode_t : uint8_t {
    FP_MODE_FILE = 0, /* a PNG file */
    FP_MODE_IMAGE =
        1, /* the fields of a header, palette and raw rows, made into a file here, so that mutation reaches what is behind the zlib */
    FP_MODE_RAW = 2,  /* a raw DEFLATE stream */
    FP_MODE_ZLIB = 3, /* a zlib stream */
    FP_MODE_COUNT = 4,
} fp_mode_t;

enum : uint64_t {
    FP_INPUT_BYTES = 1ull << 20,
    /* A caller's own limit on a file: what png_probe says is theirs to refuse. This is more than a mutation needs, and keeps each run short. */
    FP_PIXEL_BYTES = 16ull << 20,
    FP_WORK_BYTES = 40ull << 20,
    FP_OUTPUT_BYTES = 4ull << 20,
    FP_FILE_BYTES = FP_INPUT_BYTES + (FP_INPUT_BYTES / 65535u + 2u) * 5u + 1024u + 1024u,
};
enum : uint32_t {
    /* width, height: 2 each; depth, color, interlace, then 2 each of the lengths of the palette and tRNS */
    FP_IMAGE_HEAD_BYTES = 11u,
};

typedef struct fp_context_t {
    fuzz_buffer_t file;   /* 0 */
    fuzz_buffer_t pixels; /* 24 */
    fuzz_buffer_t work;   /* 48 */
    fuzz_buffer_t output; /* 72 */
    uint8_t *made;        /* 96: a file of FP_MODE_IMAGE */
    uint32_t *crc_table;  /* 104 */
} fp_context_t;
static_assert(sizeof(fp_context_t) == 112);

static const char *const fp_count_names[] = {"probed good", "decoded", "decode refused", "stream inflated",
                                             "stream refused"};

#define FP_TOKEN(text) {(const uint8_t *)(text), sizeof(text) - 1u}
static const fuzz_token_t fp_tokens[] = {
    FP_TOKEN("IHDR"),
    FP_TOKEN("PLTE"),
    FP_TOKEN("IDAT"),
    FP_TOKEN("IEND"),
    FP_TOKEN("tRNS"),
    FP_TOKEN("gAMA"),
    FP_TOKEN("sRGB"),
    FP_TOKEN("iCCP"),
    FP_TOKEN("aaaa"),
    FP_TOKEN("AAAA"),
    FP_TOKEN("\x89PNG\r\n\x1a\n"),
};
enum { FP_TOKEN_COUNT = sizeof(fp_tokens) / sizeof(fp_tokens[0]) };

static void fp_crc_table_make(uint32_t *table) {
    for (uint32_t n = 0u; n < 256u; n++) {
        uint32_t c = n;
        for (uint32_t k = 0u; k < 8u; k++) {
            c = (c & 1u) != 0u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        table[n] = c;
    }
}

static uint32_t fp_crc(const uint32_t *table, const uint8_t *bytes, uint64_t length) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint64_t i = 0u; i < length; i++) {
        c = table[(c ^ bytes[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

static uint32_t fp_get_be(const uint8_t *from) {
    return ((uint32_t)from[0] << 24) | ((uint32_t)from[1] << 16) | ((uint32_t)from[2] << 8) | from[3];
}

static void fp_put_be(uint8_t *to, uint32_t value) {
    to[0] = (uint8_t)(value >> 24);
    to[1] = (uint8_t)(value >> 16);
    to[2] = (uint8_t)(value >> 8);
    to[3] = (uint8_t)value;
}

/* A chunk onto the end of out, with its right CRC. Returns the new length. */
static uint64_t fp_chunk_put(const fp_context_t *fp, uint8_t *out, uint64_t at, const char *type, const uint8_t *data,
                             uint64_t length) {
    fp_put_be(out + at, (uint32_t)length);
    memcpy(out + at + 4u, type, 4u);
    if (length != 0u) {
        memcpy(out + at + 8u, data, length);
    }
    fp_put_be(out + at + 8u + length, fp_crc(fp->crc_table, out + at + 4u, length + 4u));
    return at + 12u + length;
}

/* An image's fields and rows, made into a whole file, with stored deflate blocks. Returns its length. */
static uint64_t fp_image_make(const fp_context_t *fp, const uint8_t *body, uint64_t length, uint8_t *out) {
    static const uint8_t signature[8] = {0x89u, 'P', 'N', 'G', '\r', '\n', 0x1Au, '\n'};
    memcpy(out, signature, sizeof(signature));
    uint64_t at = sizeof(signature);
    if (length < FP_IMAGE_HEAD_BYTES) {
        return at;
    }
    const uint32_t width = (uint32_t)body[0] | ((uint32_t)body[1] << 8);
    const uint32_t height = (uint32_t)body[2] | ((uint32_t)body[3] << 8);
    uint8_t ihdr[13];
    fp_put_be(ihdr, width);
    fp_put_be(ihdr + 4u, height);
    ihdr[8] = body[4];
    ihdr[9] = body[5];
    ihdr[10] = 0u;
    ihdr[11] = 0u;
    ihdr[12] = body[6];
    at = fp_chunk_put(fp, out, at, "IHDR", ihdr, sizeof(ihdr));

    uint64_t place = FP_IMAGE_HEAD_BYTES;
    const uint64_t palette_bytes = (uint64_t)body[7] | ((uint64_t)body[8] << 8);
    const uint64_t transparency_bytes = (uint64_t)body[9] | ((uint64_t)body[10] << 8);
    if (palette_bytes != 0u && palette_bytes <= length - place) {
        at = fp_chunk_put(fp, out, at, "PLTE", body + place, palette_bytes);
        place += palette_bytes;
    }
    if (transparency_bytes != 0u && transparency_bytes <= length - place) {
        at = fp_chunk_put(fp, out, at, "tRNS", body + place, transparency_bytes);
        place += transparency_bytes;
    }

    /* What is left is the rows, in zlib's plainest form. */
    const uint8_t *const raw = body + place;
    const uint64_t raw_bytes = length - place;
    const uint64_t chunk_start = at;
    uint64_t out_at = at + 8u;
    out[out_at] = 0x78u;
    out[out_at + 1u] = 0x01u;
    out_at += 2u;
    uint64_t done = 0u;
    uint32_t adler_a = 1u;
    uint32_t adler_b = 0u;
    for (uint64_t i = 0u; i < raw_bytes; i++) {
        adler_a = (adler_a + raw[i]) % 65521u;
        adler_b = (adler_b + adler_a) % 65521u;
    }
    do {
        const uint64_t block = raw_bytes - done < 65535u ? raw_bytes - done : 65535u;
        out[out_at] = done + block == raw_bytes ? 1u : 0u;
        out[out_at + 1u] = (uint8_t)block;
        out[out_at + 2u] = (uint8_t)(block >> 8);
        out[out_at + 3u] = (uint8_t)~block;
        out[out_at + 4u] = (uint8_t)(~block >> 8);
        out_at += 5u;
        if (block != 0u) {
            memcpy(out + out_at, raw + done, block);
        }
        out_at += block;
        done += block;
    } while (done < raw_bytes);
    fp_put_be(out + out_at, (adler_b << 16) | adler_a);
    out_at += 4u;
    const uint64_t idat_bytes = out_at - chunk_start - 8u;
    fp_put_be(out + chunk_start, (uint32_t)idat_bytes);
    memcpy(out + chunk_start + 4u, "IDAT", 4u);
    fp_put_be(out + out_at, fp_crc(fp->crc_table, out + chunk_start + 4u, idat_bytes + 4u));
    at = out_at + 4u;
    return fp_chunk_put(fp, out, at, "IEND", nullptr, 0u);
}

/* Probe and decode, with everything a caller is promised looked at. */
static void fp_decode_check(fp_context_t *fp, const uint8_t *file, uint64_t length) {
    png_info_t info;
    png_error_t error;
    const png_status_t probed = png_probe(file, length, &info, &error);
    if (png_status_text(probed) == nullptr) {
        fuzz_fail("a status with no text");
    }
    if (memchr(error.message, '\0', sizeof(error.message)) == nullptr) {
        fuzz_fail("an error message has no end");
    }
    if (probed == PNG_OK) {
        fuzz_count(0u);
    }
    if (probed != PNG_OK) {
        static const png_info_t zeroed;
        if (error.status != (uint8_t)probed || memcmp(&info, &zeroed, sizeof(info)) != 0) {
            fuzz_fail("a refused probe left info or the wrong status: %" PRIu32, (uint32_t)probed);
        }
        return;
    }
    if (info.width == 0u || info.height == 0u || info.width > PNG_MAX_DIMENSION || info.height > PNG_MAX_DIMENSION ||
        info.pixel_bytes != (uint64_t)info.width * info.height * 4u ||
        info.work_bytes != info.raw_bytes + info.idat_bytes || info.idat_bytes > length) {
        fuzz_fail("a probe that says what cannot be: %" PRIu32 " by %" PRIu32 ", %" PRIu64 " work", info.width,
                  info.height, info.work_bytes);
    }
    if (info.pixel_bytes > FP_PIXEL_BYTES || info.work_bytes > FP_WORK_BYTES) {
        return; /* a caller's limit, and only that */
    }
    uint8_t *const pixels = fuzz_buffer_fit(&fp->pixels, info.pixel_bytes);
    uint8_t *const work = fuzz_buffer_fit(&fp->work, info.work_bytes);
    png_info_t decoded;
    memset(pixels, 0xA5, info.pixel_bytes);
    const png_status_t status =
        png_decode(file, length, pixels, info.pixel_bytes, work, info.work_bytes, &decoded, &error);
    fuzz_count(status == PNG_OK ? 1u : 2u);
    if (status != PNG_OK) {
        for (uint64_t i = 0u; i < info.pixel_bytes; i++) {
            if (pixels[i] != 0u) {
                fuzz_fail("a refused file left a pixel at byte %" PRIu64, i);
            }
        }
    } else if (memcmp(&decoded, &info, sizeof(info)) != 0) {
        fuzz_fail("decode and probe disagree about a file");
    }

    /* The same again, for anything that hung on what it read before it was written. */
    if (status == PNG_OK && info.pixel_bytes <= (1u << 20)) {
        const uint64_t pixel_hash_bytes = info.pixel_bytes;
        uint64_t first = 0xCBF29CE484222325ull;
        for (uint64_t i = 0u; i < pixel_hash_bytes; i++) {
            first = (first ^ pixels[i]) * 0x100000001B3ull;
        }
        memset(work, 0x5A, info.work_bytes);
        memset(pixels, 0x5A, pixel_hash_bytes);
        if (png_decode(file, length, pixels, pixel_hash_bytes, work, info.work_bytes, nullptr, &error) != PNG_OK) {
            fuzz_fail("a file decoded once and not twice");
        }
        uint64_t second = 0xCBF29CE484222325ull;
        for (uint64_t i = 0u; i < pixel_hash_bytes; i++) {
            second = (second ^ pixels[i]) * 0x100000001B3ull;
        }
        if (first != second) {
            fuzz_fail("two decodes of one file differ, so it reads memory it did not write");
        }
    }

    /* Too little room is a refusal, never an overrun. */
    if (info.pixel_bytes >= 1u) {
        uint8_t *const short_pixels = fuzz_buffer_fit(&fp->pixels, info.pixel_bytes - 1u);
        const png_status_t short_status =
            png_decode(file, length, short_pixels, info.pixel_bytes - 1u, work, info.work_bytes, nullptr, &error);
        if (short_status != PNG_NO_ROOM) {
            fuzz_fail("a buffer a byte short got status %" PRIu32, (uint32_t)short_status);
        }
    }
    if (info.work_bytes >= 1u) {
        uint8_t *const short_work = fuzz_buffer_fit(&fp->work, info.work_bytes - 1u);
        uint8_t *const room = fuzz_buffer_fit(&fp->pixels, info.pixel_bytes);
        const png_status_t short_status =
            png_decode(file, length, room, info.pixel_bytes, short_work, info.work_bytes - 1u, nullptr, &error);
        if (short_status != PNG_NO_ROOM) {
            fuzz_fail("work a byte short got status %" PRIu32, (uint32_t)short_status);
        }
    }
}

/* One stream both ways it can be given. */
static void fp_inflate_check(fp_context_t *fp, bool zlib, const uint8_t *input, uint64_t length) {
    uint8_t *const output = fuzz_buffer_fit(&fp->output, FP_OUTPUT_BYTES);
    inflate_result_t result = {0};
    inflate_status_t status = zlib ? inflate_zlib(input, length, output, FP_OUTPUT_BYTES, &result)
                                   : inflate_raw(input, length, output, FP_OUTPUT_BYTES, &result);
    if (inflate_status_text(status) == nullptr) {
        fuzz_fail("a status with no text");
    }
    if (result.input_used > length || result.output_used > FP_OUTPUT_BYTES) {
        fuzz_fail("a stream says it used more than it had: %" PRIu64 " in, %" PRIu64 " out", result.input_used,
                  result.output_used);
    }
    fuzz_count(status == INFLATE_OK ? 3u : 4u);
    if (status != INFLATE_OK) {
        return;
    }
    /* What a stream took is what it takes: exactly that much room is enough, and a byte less is not. */
    const uint64_t took = result.output_used;
    uint8_t *const exact = fuzz_buffer_fit(&fp->output, took);
    inflate_result_t again = {0};
    status = zlib ? inflate_zlib(input, length, exact, took, &again) : inflate_raw(input, length, exact, took, &again);
    if (status != INFLATE_OK || again.output_used != took || again.input_used != result.input_used) {
        fuzz_fail("a stream of %" PRIu64 " bytes does not fit in that", took);
    }
    if (took != 0u) {
        uint8_t *const less = fuzz_buffer_fit(&fp->output, took - 1u);
        status = zlib ? inflate_zlib(input, length, less, took - 1u, &again)
                      : inflate_raw(input, length, less, took - 1u, &again);
        if (status != INFLATE_NO_ROOM) {
            fuzz_fail("a stream of %" PRIu64 " bytes in a byte less got status %" PRIu32, took, (uint32_t)status);
        }
    }
}

static void fp_execute(void *context, const uint8_t *input, uint64_t length) {
    fp_context_t *const fp = context;
    if (length == 0u) {
        return;
    }
    const uint8_t *const body = input + 1u;
    const uint64_t body_bytes = length - 1u;
    switch (input[0] % FP_MODE_COUNT) {
    case FP_MODE_FILE:
        fp_decode_check(fp, fuzz_buffer_hold(&fp->file, body, body_bytes), body_bytes);
        break;
    case FP_MODE_IMAGE: {
        const uint64_t made = fp_image_make(fp, body, body_bytes, fp->made);
        fp_decode_check(fp, fuzz_buffer_hold(&fp->file, fp->made, made), made);
        break;
    }
    case FP_MODE_RAW:
        fp_inflate_check(fp, false, fuzz_buffer_hold(&fp->file, body, body_bytes), body_bytes);
        break;
    default:
        fp_inflate_check(fp, true, fuzz_buffer_hold(&fp->file, body, body_bytes), body_bytes);
        break;
    }
}

/* Makes each chunk's CRC right, for as many chunks as are whole. One input in sixteen stays as it is, for the CRC check's own sake. */
static void fp_fix(void *context, uint8_t *input, uint64_t *length, uint64_t capacity, fuzz_rng_t *rng) {
    (void)capacity;
    const fp_context_t *const fp = context;
    if (*length < 1u + 8u || input[0] % FP_MODE_COUNT != FP_MODE_FILE || fuzz_rng_below(rng, 16u) == 0u) {
        return;
    }
    uint8_t *const file = input + 1u;
    const uint64_t bytes = *length - 1u;
    uint64_t at = 8u;
    while (bytes >= at + 12u) {
        const uint64_t chunk = fp_get_be(file + at);
        if (chunk > bytes - at - 12u) {
            break;
        }
        fp_put_be(file + at + 8u + chunk, fp_crc(fp->crc_table, file + at + 4u, chunk + 4u));
        at += 12u + chunk;
    }
}

/* Every kind of image, as a raw body: all colour types and depths, both orders of row, and each filter. */
static void fp_seeds_images(test_context_t *t, const fp_context_t *fp, fuzz_corpus_t *corpus) {
    (void)fp;
    static const uint8_t depths[][2] = {{0u, 1u}, {0u, 2u},  {0u, 4u},  {0u, 8u}, {0u, 16u},
                                        {2u, 8u}, {2u, 16u}, {3u, 1u},  {3u, 2u}, {3u, 4u},
                                        {3u, 8u}, {4u, 8u},  {4u, 16u}, {6u, 8u}, {6u, 16u}};
    uint8_t *const seed = test_alloc(t, 4096u);
    for (uint32_t kind = 0u; kind < sizeof(depths) / sizeof(depths[0]); kind++) {
        for (uint32_t interlace = 0u; interlace < 2u; interlace++) {
            const uint8_t color = depths[kind][0];
            const uint8_t depth = depths[kind][1];
            const uint32_t channels = color == 2u ? 3u : (color == 4u ? 2u : (color == 6u ? 4u : 1u));
            const uint32_t side = 5u;
            const uint32_t palette = color == 3u ? 12u : 0u;
            const uint32_t transparency = color == 3u ? 2u : 0u;
            seed[0] = FP_MODE_IMAGE;
            uint8_t *const head = seed + 1u;
            head[0] = (uint8_t)side;
            head[1] = 0u;
            head[2] = (uint8_t)side;
            head[3] = 0u;
            head[4] = depth;
            head[5] = color;
            head[6] = (uint8_t)interlace;
            head[7] = (uint8_t)palette;
            head[8] = 0u;
            head[9] = (uint8_t)transparency;
            head[10] = 0u;
            uint64_t at = FP_IMAGE_HEAD_BYTES;
            memset(head + at, 0x20, (uint64_t)palette + transparency);
            at += (uint64_t)palette + transparency;
            /* Rows of the whole image, or for Adam7 more than any pass wants: too much is a refusal too, and mutation cuts it down. */
            const uint32_t row_bytes = (side * channels * depth + 7u) / 8u;
            for (uint32_t y = 0u; y < side; y++) {
                head[at] = (uint8_t)(y % 5u);
                at++;
                for (uint32_t x = 0u; x < row_bytes; x++) {
                    head[at] = (uint8_t)(color == 3u ? 0u : (x * 37u + y));
                    at++;
                }
            }
            fuzz_seed_add(corpus, seed, 1u + at);
        }
    }
}

static void fp_seeds(test_context_t *t, void *context, fuzz_corpus_t *corpus) {
    const fp_context_t *const fp = context;
    static const uint8_t file_mode = FP_MODE_FILE;
    static const uint8_t raw_mode = FP_MODE_RAW;
    static const uint8_t zlib_mode = FP_MODE_ZLIB;
    /* ctest runs this from the top of the tree. */
    (void)fuzz_seed_files(t, corpus, "tests/compile/png/fixtures", ".png", &file_mode, 1u);
    (void)fuzz_seed_files(t, corpus, "tests/compile/inflate/fixtures", ".deflate", &raw_mode, 1u);
    (void)fuzz_seed_files(t, corpus, "tests/compile/inflate/fixtures", ".zlib", &zlib_mode, 1u);
    fp_seeds_images(t, fp, corpus);
    /* The two smallest streams there are, should those fixtures not be here. */
    static const uint8_t empty_fixed[] = {FP_MODE_RAW, 0x03u, 0x00u};
    static const uint8_t empty_zlib[] = {FP_MODE_ZLIB, 0x78u, 0x9Cu, 0x03u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u};
    fuzz_seed_add(corpus, empty_fixed, sizeof(empty_fixed));
    fuzz_seed_add(corpus, empty_zlib, sizeof(empty_zlib));
}

TEST(png) {
    fp_context_t *const fp = test_alloc(t, sizeof(*fp));
    fp->file = fuzz_buffer_make(t, FP_FILE_BYTES);
    fp->pixels = fuzz_buffer_make(t, FP_PIXEL_BYTES);
    fp->work = fuzz_buffer_make(t, FP_WORK_BYTES);
    fp->output = fuzz_buffer_make(t, FP_OUTPUT_BYTES);
    fp->made = test_alloc(t, FP_FILE_BYTES);
    fp->crc_table = test_alloc(t, 256u * sizeof(uint32_t));
    fp_crc_table_make(fp->crc_table);
    const fuzz_target_t target = {.name = "png",
                                  .borrowed_context = fp,
                                  .borrowed_tokens = fp_tokens,
                                  .input_capacity = FP_INPUT_BYTES,
                                  .seeds = fp_seeds,
                                  .fix = fp_fix,
                                  .execute = fp_execute,
                                  .borrowed_count_names = fp_count_names,
                                  .token_count = FP_TOKEN_COUNT,
                                  .count_name_count = 5u,
                                  .kind_count = FP_MODE_COUNT,
                                  ._pad0 = {0}};
    fuzz_run(t, &target);
}

TEST_MAIN()
