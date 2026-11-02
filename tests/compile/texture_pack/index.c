/*
 * ctest compile_texture_pack. See README.md. Arguments come from test.cmake: tool manifest textures work_dir. It builds its
 * own small pack at run time, so it needs no network and never looks at the real pack in /tmp/mc_c_assets/.
 */
#include "harness/index.h"
#include "texture_pack/mctex/index.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TT_COMMIT "0123456789abcdef0123456789abcdef01234567"
#define TT_OTHER_COMMIT "89abcdef0123456789abcdef0123456789abcdef"
#define TT_TOOL_TIMEOUT_MS 20000u
#define TT_PATH_FORMAT_BYTES 4096u

static const char TT_MANIFEST[] = "pack:\n"
                                  "  id: test_pack\n"
                                  "  title: Test Pack\n"
                                  "  url: https://example.com/test_pack\n"
                                  "  commit: " TT_COMMIT "\n"
                                  "  licence: CC-BY-SA-4.0\n"
                                  "  authors: ['Ann \"A\" Author', Bob]\n"
                                  "  evidence:\n"
                                  "    - { file: texture_pack.conf, contains: \"license = CC-BY-SA-4.0\" }\n"
                                  "    - { file: README.md, contains: \"Attribution-Share Alike 4.0\" }\n"
                                  "textures:\n"
                                  "  - { from: a/dirt.png, to: blocks/dirt }\n"
                                  "  - { from: a/stone.png, to: items/stone }\n"
                                  "  - { from: a/ore.png, over: a/stone.png, to: mobs/ore }\n"
                                  "  - { from: a/water.png, to: ui/water, frames: 4 }\n"
                                  "skip:\n"
                                  "  - why: Not wanted\n"
                                  "    files: [b/unused.png]\n";

/* One pack, manifest and output, in its own directory. */
typedef struct tt_case_t {
    const char *root;     /* 0 */
    const char *pack;     /* 8 */
    const char *manifest; /* 16 */
    const char *textures; /* 24 */
} tt_case_t;
static_assert(sizeof(tt_case_t) == 32);

/* One header field to overwrite, and what mctex_parse must then say. */
typedef struct tt_field_case_t {
    uint32_t offset;  /* 0 */
    uint32_t value;   /* 4 */
    uint8_t status;   /* 8: a mctex_status_t */
    uint8_t _pad0[3]; /* 9 */
} tt_field_case_t;
static_assert(sizeof(tt_field_case_t) == 12);

/* One change to the manifest, and what the refusal must say. */
typedef struct tt_change_case_t {
    const char *from;     /* 0 */
    const char *to;       /* 8 */
    const char *expected; /* 16 */
} tt_change_case_t;
static_assert(sizeof(tt_change_case_t) == 24);

/* One .git/HEAD, and what the refusal must say. */
typedef struct tt_head_case_t {
    const char *head;     /* 0 */
    const char *expected; /* 8 */
} tt_head_case_t;
static_assert(sizeof(tt_head_case_t) == 16);

/* One committed texture, and what it must be. */
typedef struct tt_shape_case_t {
    const char *to;   /* 0 */
    uint32_t width;   /* 8 */
    uint32_t height;  /* 12 */
    uint32_t frames;  /* 16 */
    uint8_t opaque;   /* 20 */
    uint8_t _pad0[3]; /* 21 */
} tt_shape_case_t;
static_assert(sizeof(tt_shape_case_t) == 24);

[[gnu::format(printf, 2, 3)]]
static char *tt_format(test_context_t *t, const char *format, ...) {
    char *text = test_alloc(t, TT_PATH_FORMAT_BYTES);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, TT_PATH_FORMAT_BYTES, format, arguments);
    va_end(arguments);
    return text;
}

static void tt_run(test_context_t *t, char *const *argv, test_process_t *out) {
    test_process_run(t, argv, TT_TOOL_TIMEOUT_MS, out);
    CHECK(out->started != 0);
    CHECK(out->timed_out == 0);
}

static void tt_mkdirs(test_context_t *t, const char *path) {
    char *const argv[] = {(char *)"mkdir", (char *)"-p", (char *)path, nullptr};
    test_process_t process;
    tt_run(t, argv, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static void tt_remove_tree(test_context_t *t, const char *path) {
    char *const argv[] = {(char *)"rm", (char *)"-rf", (char *)path, nullptr};
    test_process_t process;
    tt_run(t, argv, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static void tt_write_bytes(test_context_t *t, const char *path, const void *bytes, uint64_t length) {
    FILE *file = fopen(path, "wb");
    if (!CHECK(file != nullptr)) {
        return;
    }
    CHECK_EQ_U64(fwrite(bytes, 1, length, file), length);
    CHECK(fclose(file) == 0);
}

static void tt_write_text(test_context_t *t, const char *path, const char *text) {
    tt_write_bytes(t, path, text, strlen(text));
}

static uint8_t *tt_read(test_context_t *t, const char *path, uint64_t *out_length) {
    *out_length = 0;
    FILE *file = fopen(path, "rb");
    if (!CHECK(file != nullptr)) {
        return nullptr;
    }
    fseek(file, 0, SEEK_END);
    const int64_t size = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *bytes = test_alloc(t, (uint64_t)size + 1u);
    CHECK_EQ_U64(fread(bytes, 1, (uint64_t)size, file), (uint64_t)size);
    fclose(file);
    *out_length = (uint64_t)size;
    return bytes;
}

static bool tt_exists(const char *path) {
    struct stat info;
    return lstat(path, &info) == 0;
}

/* ---- A small PNG encoder of our own: stored deflate, filter 0, RGBA8. Independent of the decoder only in that it
 * is the simplest possible writer, and the real pack is compared with ImageMagick in the stage file. ---------------- */

static uint32_t tt_crc(const uint8_t *bytes, uint64_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint64_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8u; bit++) {
            crc = (crc & 1u) != 0 ? (crc >> 1u) ^ 0xEDB88320u : crc >> 1u;
        }
    }
    return ~crc;
}

static void tt_put_be32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value >> 24u);
    out[1] = (uint8_t)(value >> 16u);
    out[2] = (uint8_t)(value >> 8u);
    out[3] = (uint8_t)value;
}

static uint64_t tt_put_chunk(uint8_t *out, const char *type, const uint8_t *data, uint32_t length) {
    tt_put_be32(out, length);
    memcpy(out + 4, type, 4);
    if (length > 0) {
        memcpy(out + 8, data, length);
    }
    tt_put_be32(out + 8u + length, tt_crc(out + 4, 4u + (uint64_t)length));
    return 12u + (uint64_t)length;
}

/* Any IHDR with any raw image data, in one stored block. raw_length is at most 65535. */
static uint8_t *tt_png_raw(test_context_t *t, uint32_t width, uint32_t height, const uint8_t *raw, uint32_t raw_length,
                           uint64_t *out_length) {
    uint8_t *png = test_alloc(t, (uint64_t)raw_length + 128u);
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    memcpy(png, signature, 8);
    uint64_t at = 8;
    uint8_t ihdr[13] = {0, 0, 0, 0, 0, 0, 0, 0, 8, 6, 0, 0, 0};
    tt_put_be32(ihdr, width);
    tt_put_be32(ihdr + 4, height);
    at += tt_put_chunk(png + at, "IHDR", ihdr, 13u);

    uint8_t *zlib = test_alloc(t, (uint64_t)raw_length + 16u);
    zlib[0] = 0x78;
    zlib[1] = 0x01;
    zlib[2] = 0x01;
    zlib[3] = (uint8_t)raw_length;
    zlib[4] = (uint8_t)(raw_length >> 8u);
    zlib[5] = (uint8_t)~raw_length;
    zlib[6] = (uint8_t)(~raw_length >> 8u);
    if (raw_length > 0) {
        memcpy(zlib + 7, raw, raw_length);
    }
    uint32_t a = 1;
    uint32_t b = 0;
    for (uint32_t i = 0; i < raw_length; i++) {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    tt_put_be32(zlib + 7u + raw_length, (b << 16u) | a);
    at += tt_put_chunk(png + at, "IDAT", zlib, raw_length + 11u);
    at += tt_put_chunk(png + at, "IEND", nullptr, 0);
    *out_length = at;
    return png;
}

static void tt_write_png(test_context_t *t, const char *path, uint32_t width, uint32_t height, const uint8_t *rgba) {
    const uint32_t row = 1u + width * 4u;
    const uint32_t raw_length = row * height;
    uint8_t *raw = test_alloc(t, raw_length);
    for (uint32_t y = 0; y < height; y++) {
        raw[y * row] = 0;
        memcpy(raw + y * row + 1u, rgba + (uint64_t)y * width * 4u, (uint64_t)width * 4u);
    }
    uint64_t length = 0;
    uint8_t *png = tt_png_raw(t, width, height, raw, raw_length, &length);
    tt_write_bytes(t, path, png, length);
}

/* ---- The fixture --------------------------------------------------------------------------------------------- */

static const uint8_t TT_DIRT[16] = {1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
static const uint8_t TT_STONE[16] = {70, 80, 90, 255, 100, 110, 120, 255, 10, 20, 30, 255, 40, 50, 60, 255};
static const uint8_t TT_ORE[16] = {9, 9, 9, 0, 1, 2, 3, 255, 200, 100, 50, 128, 0, 255, 0, 64};
/* Worked by hand: (top * alpha + base * (255 - alpha) + 127) / 255. */
static const uint8_t TT_ORE_OVER_STONE[16] = {70, 80, 90, 255, 1, 2, 3, 255, 105, 60, 40, 255, 30, 101, 45, 255};

static tt_case_t tt_case_begin(test_context_t *t, uint32_t label) {
    tt_case_t c;
    c.root = tt_format(t, "%s/%s/%u", t->borrowed_args[3], t->name, label);
    c.pack = tt_format(t, "%s/pack", c.root);
    c.manifest = tt_format(t, "%s/import.yaml", c.root);
    c.textures = tt_format(t, "%s/out", c.root);
    tt_remove_tree(t, c.root);
    tt_mkdirs(t, tt_format(t, "%s/a", c.pack));
    tt_mkdirs(t, tt_format(t, "%s/b", c.pack));
    tt_mkdirs(t, tt_format(t, "%s/.git/refs/heads", c.pack));

    uint8_t water[64];
    for (uint32_t i = 0; i < 64u; i++) {
        water[i] = (uint8_t)(i * 3u + 7u);
    }
    tt_write_png(t, tt_format(t, "%s/a/dirt.png", c.pack), 2u, 2u, TT_DIRT);
    tt_write_png(t, tt_format(t, "%s/a/stone.png", c.pack), 2u, 2u, TT_STONE);
    tt_write_png(t, tt_format(t, "%s/a/ore.png", c.pack), 2u, 2u, TT_ORE);
    tt_write_png(t, tt_format(t, "%s/a/water.png", c.pack), 2u, 8u, water);
    tt_write_png(t, tt_format(t, "%s/b/unused.png", c.pack), 2u, 2u, TT_DIRT);
    tt_write_text(t, tt_format(t, "%s/texture_pack.conf", c.pack), "title = x\nlicense = CC-BY-SA-4.0\n");
    tt_write_text(t, tt_format(t, "%s/README.md", c.pack), "Creative Commons Attribution-Share Alike 4.0 License\n");
    tt_write_text(t, tt_format(t, "%s/.git/HEAD", c.pack), "ref: refs/heads/master\n");
    tt_write_text(t, tt_format(t, "%s/.git/refs/heads/master", c.pack), TT_COMMIT "\n");
    tt_write_text(t, c.manifest, TT_MANIFEST);
    return c;
}

/* Replaces the one occurrence of from in the case's manifest. */
static void tt_manifest_change(test_context_t *t, const tt_case_t *c, const char *from, const char *to) {
    const char *at = strstr(TT_MANIFEST, from);
    if (!CHECK(at != nullptr)) {
        return;
    }
    const uint64_t before = (uint64_t)(at - TT_MANIFEST);
    char *text = test_alloc(t, sizeof(TT_MANIFEST) + strlen(to) + 1u);
    memcpy(text, TT_MANIFEST, before);
    strcpy(text + before, to);
    strcat(text, at + strlen(from));
    tt_write_text(t, c->manifest, text);
}

static void tt_import(test_context_t *t, const tt_case_t *c, test_process_t *out) {
    char *const argv[] = {t->borrowed_args[0],  (char *)"import",     (char *)"--pack",
                          (char *)c->pack,      (char *)"--manifest", (char *)c->manifest,
                          (char *)"--textures", (char *)c->textures,  nullptr};
    tt_run(t, argv, out);
}

static void tt_verify(test_context_t *t, const char *manifest, const char *textures, test_process_t *out) {
    char *const argv[] = {
        t->borrowed_args[0], (char *)"verify", (char *)"--manifest", (char *)manifest, (char *)"--textures",
        (char *)textures,    nullptr};
    tt_run(t, argv, out);
}

static void tt_expect_refused(test_context_t *t, const tt_case_t *c, const char *needle) {
    test_process_t process;
    tt_import(t, c, &process);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, needle);
    /* All or nothing: not one directory, even with good textures before the bad one. */
    if (!CHECK(!tt_exists(c->textures))) {
        fprintf(stderr, "  wrote output despite: %s\n", needle);
    }
}

static void tt_good_import(test_context_t *t, const tt_case_t *c) {
    test_process_t process;
    tt_import(t, c, &process);
    if (!CHECK_EQ_I64(process.exit_code, 0)) {
        fprintf(stderr, "%s\n", process.output);
    }
}

static void tt_check_mctex(test_context_t *t, const char *path, uint32_t width, uint32_t height, uint32_t frames,
                           const uint8_t *pixels) {
    uint64_t length = 0;
    const uint8_t *file = tt_read(t, path, &length);
    mctex_view_t view;
    if (!CHECK_EQ_U64(mctex_parse(file, length, &view), MCTEX_OK)) {
        return;
    }
    CHECK_EQ_U64(view.width, width);
    CHECK_EQ_U64(view.height, height);
    CHECK_EQ_U64(view.frame_count, frames);
    CHECK_EQ_U64(view.pixel_bytes, (uint64_t)width * height * frames * 4u);
    CHECK_MEM_EQ(view.borrowed_pixels, pixels, view.pixel_bytes);
}

/* ---- mctex --------------------------------------------------------------------------------------------------- */

TEST(mctex_hash_is_fnv1a_64) {
    /* The published FNV test vectors. */
    CHECK_EQ_U64(mctex_hash((const uint8_t *)"", 0), UINT64_C(0xCBF29CE484222325));
    CHECK_EQ_U64(mctex_hash((const uint8_t *)"a", 1), UINT64_C(0xAF63DC4C8601EC8C));
    CHECK_EQ_U64(mctex_hash((const uint8_t *)"foobar", 6), UINT64_C(0x85944171F73967E8));
}

TEST(mctex_round_trip) {
    uint8_t file[32 + 16];
    REQUIRE(mctex_encode(file, sizeof(file), 2u, 1u, 2u, TT_DIRT, 16u) == MCTEX_OK);
    CHECK_MEM_EQ(file, "MCTX", 4u);
    mctex_view_t view;
    REQUIRE(mctex_parse(file, sizeof(file), &view) == MCTEX_OK);
    CHECK_EQ_U64(view.width, 2u);
    CHECK_EQ_U64(view.height, 1u);
    CHECK_EQ_U64(view.frame_count, 2u);
    CHECK_EQ_U64(view.pixel_bytes, 16u);
    CHECK_MEM_EQ(view.borrowed_pixels, TT_DIRT, 16u);
}

TEST(mctex_refuses) {
    uint8_t good[32 + 16];
    REQUIRE(mctex_encode(good, sizeof(good), 2u, 2u, 1u, TT_DIRT, 16u) == MCTEX_OK);
    mctex_view_t view;
    for (uint64_t length = 0; length < sizeof(good); length++) {
        CHECK_EQ_U64(mctex_parse(good, length, &view), MCTEX_TRUNCATED);
        CHECK(view.borrowed_pixels == nullptr);
    }
    uint8_t longer[sizeof(good) + 1u];
    memcpy(longer, good, sizeof(good));
    longer[sizeof(good)] = 0;
    CHECK_EQ_U64(mctex_parse(longer, sizeof(longer), &view), MCTEX_TRAILING);

    uint8_t bad[sizeof(good)];
    mctex_header_t header;
    const tt_field_case_t cases[] = {
        {offsetof(mctex_header_t, magic), 0x12345678u, MCTEX_BAD_MAGIC, {0}},
        {offsetof(mctex_header_t, version), 2u, MCTEX_BAD_VERSION, {0}},
        {offsetof(mctex_header_t, width), 0u, MCTEX_BAD_SIZE, {0}},
        {offsetof(mctex_header_t, height), 0u, MCTEX_BAD_SIZE, {0}},
        {offsetof(mctex_header_t, frame_count), 0u, MCTEX_BAD_SIZE, {0}},
        {offsetof(mctex_header_t, width), 4097u, MCTEX_BAD_SIZE, {0}},
        {offsetof(mctex_header_t, height), UINT32_MAX, MCTEX_BAD_SIZE, {0}},
        {offsetof(mctex_header_t, frame_count), 257u, MCTEX_BAD_SIZE, {0}},
        {offsetof(mctex_header_t, _pad0), 1u, MCTEX_BAD_PADDING, {0}},
        {offsetof(mctex_header_t, width), 3u, MCTEX_TRUNCATED, {0}},
        {offsetof(mctex_header_t, width), 1u, MCTEX_TRAILING, {0}},
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        memcpy(bad, good, sizeof(good));
        memcpy(bad + cases[i].offset, &cases[i].value, 4u);
        CHECK_EQ_U64(mctex_parse(bad, sizeof(bad), &view), cases[i].status);
    }
    /* Within limits on each dimension, over them together. */
    memcpy(&header, good, sizeof(header));
    header.width = 4096u;
    header.height = 4096u;
    header.frame_count = 256u;
    memcpy(bad, &header, sizeof(header));
    CHECK_EQ_U64(mctex_parse(bad, sizeof(bad), &view), MCTEX_BAD_SIZE);

    /* Every bit of every pixel and of the hash. */
    for (uint32_t i = offsetof(mctex_header_t, pixel_hash); i < sizeof(good); i++) {
        for (uint32_t bit = 0; bit < 8u; bit++) {
            memcpy(bad, good, sizeof(good));
            bad[i] ^= (uint8_t)(1u << bit);
            CHECK_EQ_U64(mctex_parse(bad, sizeof(bad), &view), MCTEX_BAD_HASH);
        }
    }

    uint8_t small[32 + 15];
    CHECK_EQ_U64(mctex_encode(small, sizeof(small), 2u, 2u, 1u, TT_DIRT, 16u), MCTEX_NO_ROOM);
    CHECK_EQ_U64(mctex_encode(good, sizeof(good), 2u, 2u, 1u, TT_DIRT, 15u), MCTEX_NO_ROOM);
    CHECK_EQ_U64(mctex_encode(good, sizeof(good), 0u, 2u, 1u, TT_DIRT, 0u), MCTEX_BAD_SIZE);
    for (uint32_t s = 0; s < 16u; s++) {
        CHECK(mctex_status_text((mctex_status_t)s)[0] != '\0');
    }
}

/* ---- A good import ------------------------------------------------------------------------------------------- */

TEST(import_writes_exactly_this) {
    const tt_case_t c = tt_case_begin(t, 0);
    tt_good_import(t, &c);

    tt_check_mctex(t, tt_format(t, "%s/blocks/dirt/index.mctex", c.textures), 2u, 2u, 1u, TT_DIRT);
    tt_check_mctex(t, tt_format(t, "%s/items/stone/index.mctex", c.textures), 2u, 2u, 1u, TT_STONE);
    tt_check_mctex(t, tt_format(t, "%s/mobs/ore/index.mctex", c.textures), 2u, 2u, 1u, TT_ORE_OVER_STONE);
    uint8_t water[64];
    for (uint32_t i = 0; i < 64u; i++) {
        water[i] = (uint8_t)(i * 3u + 7u);
    }
    tt_check_mctex(t, tt_format(t, "%s/ui/water/index.mctex", c.textures), 2u, 2u, 4u, water);

    uint64_t length = 0;
    const char *yaml = (const char *)tt_read(t, tt_format(t, "%s/mobs/ore/index.yaml", c.textures), &length);
    CHECK_CONTAINS(yaml, "name: ore\n");
    CHECK_CONTAINS(yaml, "group: mobs\n");
    CHECK_CONTAINS(yaml, "size: { width: 2, height: 2, frames: 1 }\n");
    CHECK_CONTAINS(yaml, "  kind: pack\n");
    CHECK_CONTAINS(yaml, "  commit: \"" TT_COMMIT "\"\n");
    CHECK_CONTAINS(yaml, "  licence: CC-BY-SA-4.0\n");
    CHECK_CONTAINS(yaml, "  file: a/ore.png\n");
    CHECK_CONTAINS(yaml, "  over: a/stone.png\n");

    const char *readme = (const char *)tt_read(t, tt_format(t, "%s/mobs/ore/README.md", c.textures), &length);
    CHECK_CONTAINS(readme, "Test Pack");
    CHECK_CONTAINS(readme, "Ann \"A\" Author, Bob");
    CHECK_CONTAINS(readme, "CC-BY-SA-4.0");
    CHECK_CONTAINS(readme, "composited over `a/stone.png`");

    const char *credits = (const char *)tt_read(t, tt_format(t, "%s/ATTRIBUTION.md", c.textures), &length);
    CHECK_CONTAINS(credits, "CC-BY-SA-4.0");
    CHECK_CONTAINS(credits, "https://creativecommons.org/licenses/by-sa/4.0/");
    CHECK_CONTAINS(credits, TT_COMMIT);
    CHECK_CONTAINS(credits, "https://example.com/test_pack");
    CHECK_CONTAINS(credits, "Ann \"A\" Author, Bob");
    CHECK_CONTAINS(credits, "| `blocks/dirt` | `a/dirt.png` | none |");
    CHECK_CONTAINS(credits, "| `mobs/ore` | `a/ore.png` | composited over `a/stone.png` |");
    CHECK_CONTAINS(credits, "| `ui/water` | `a/water.png` | 4 animation frames |");
    CHECK_CONTAINS(credits, "7 files in the pack: 4 imported, 1 deliberately left out, 2 holding");

    /* All four groups exist with a README, including any nothing is imported into. No temporary is left behind. */
    const char *const groups[] = {"blocks", "items", "mobs", "ui"};
    for (uint32_t i = 0; i < 4u; i++) {
        CHECK(tt_exists(tt_format(t, "%s/%s/README.md", c.textures, groups[i])));
    }
    char *const find_argv[] = {(char *)"find", (char *)c.textures, (char *)"-name", (char *)"*.tmp.*", nullptr};
    test_process_t find;
    tt_run(t, find_argv, &find);
    CHECK_EQ_U64(find.output_length, 0u);
}

/* The bytes where rounding decides: sums of 127 over a multiple of 255 go down, and 128 go up. TT_ORE never lands on either. */
TEST(compositing_rounds_to_nearest_on_each_side_of_a_half) {
    const tt_case_t c = tt_case_begin(t, 0);
    static const uint8_t top[16] = {197, 207, 217, 1, 228, 238, 248, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    static const uint8_t wanted[16] = {70, 80, 90, 255, 101, 111, 121, 255, 10, 20, 30, 255, 40, 50, 60, 255};
    tt_write_png(t, tt_format(t, "%s/a/ore.png", c.pack), 2u, 2u, top);
    tt_good_import(t, &c);
    tt_check_mctex(t, tt_format(t, "%s/mobs/ore/index.mctex", c.textures), 2u, 2u, 1u, wanted);
}

TEST(import_is_repeatable) {
    const tt_case_t c = tt_case_begin(t, 0);
    tt_good_import(t, &c);
    const char *mctex = tt_format(t, "%s/blocks/dirt/index.mctex", c.textures);
    const char *credits = tt_format(t, "%s/ATTRIBUTION.md", c.textures);
    /* An old time, which a rewrite would replace with now. */
    const struct timespec old[2] = {{.tv_sec = 1000, .tv_nsec = 0}, {.tv_sec = 1000, .tv_nsec = 0}};
    REQUIRE(utimensat(AT_FDCWD, mctex, old, 0) == 0);

    test_process_t again;
    tt_import(t, &c, &again);
    CHECK_EQ_I64(again.exit_code, 0);
    CHECK_CONTAINS(again.output, "; 0 output files changed");
    struct stat info;
    REQUIRE(stat(mctex, &info) == 0);
    CHECK_EQ_I64(info.st_mtim.tv_sec, 1000);

    /* A different output path gives the same bytes, so no path leaks into a file. */
    tt_case_t other = c;
    other.textures = tt_format(t, "%s/elsewhere/deeper", c.root);
    tt_good_import(t, &other);
    uint64_t left_length = 0;
    uint64_t right_length = 0;
    const uint8_t *left = tt_read(t, credits, &left_length);
    const uint8_t *right = tt_read(t, tt_format(t, "%s/ATTRIBUTION.md", other.textures), &right_length);
    REQUIRE(left_length == right_length);
    CHECK_MEM_EQ(left, right, left_length);
    char *const diff_argv[] = {(char *)"diff", (char *)"-r", (char *)c.textures, (char *)other.textures, nullptr};
    test_process_t diff;
    tt_run(t, diff_argv, &diff);
    CHECK_EQ_I64(diff.exit_code, 0);
}

TEST(pin_is_found_every_way_git_stores_it) {
    const tt_case_t detached = tt_case_begin(t, 0);
    tt_write_text(t, tt_format(t, "%s/.git/HEAD", detached.pack), TT_COMMIT "\n");
    tt_good_import(t, &detached);

    const tt_case_t packed = tt_case_begin(t, 1);
    REQUIRE(unlink(tt_format(t, "%s/.git/refs/heads/master", packed.pack)) == 0);
    tt_write_text(t, tt_format(t, "%s/.git/packed-refs", packed.pack),
                  "# pack-refs with: peeled fully-peeled sorted\n" TT_OTHER_COMMIT " refs/heads/master2\n" TT_COMMIT
                  " refs/heads/master\n");
    tt_good_import(t, &packed);
}

TEST(a_clone_with_git_ignore_is_fine) {
    const tt_case_t c = tt_case_begin(t, 0);
    tt_write_text(t, tt_format(t, "%s/.gitignore", c.pack), "*.tmp\n");
    tt_good_import(t, &c);
}

/* ---- What must be refused ------------------------------------------------------------------------------------ */

TEST(manifest_mistakes_are_refused) {
    const tt_change_case_t cases[] = {
        {"commit: " TT_COMMIT, "commit: 0123456", "full 40-digit"},
        {"commit: " TT_COMMIT, "commit: 0123456789ABCDEF0123456789ABCDEF01234567", "full 40-digit"},
        {"commit: " TT_COMMIT, "commit: " TT_OTHER_COMMIT, "pins"},
        {"licence: CC-BY-SA-4.0", "licence: CC-BY-NC-4.0", "allowlist"},
        {"licence: CC-BY-SA-4.0", "licence: CC-BY-ND-4.0", "allowlist"},
        {"licence: CC-BY-SA-4.0", "licence: Proprietary", "allowlist"},
        {"licence: CC-BY-SA-4.0", "licence: CC-BY-4.0", "other than CC-BY-4.0"},
        {"authors: ['Ann \"A\" Author', Bob]", "authors: []", "non-empty"},
        {"url: https://example.com/test_pack", "url: http://example.com/test_pack", "https"},
        {"contains: \"Attribution-Share Alike 4.0\"", "contains: \"Public Domain\"", "no longer says"},
        {"from: a/dirt.png", "from: ../outside.png", "safe relative path"},
        {"from: a/dirt.png", "from: /etc/passwd", "safe relative path"},
        {"from: a/dirt.png", "from: a//dirt.png", "safe relative path"},
        {"from: a/dirt.png", "from: .git/HEAD", "safe relative path"},
        {"from: a/dirt.png", "from: a/gone.png", "neither imports nor skips"},
        {"over: a/stone.png", "over: ../x.png", "safe relative path"},
        {"over: a/stone.png", "over: a/gone.png", "does not have"},
        {"to: blocks/dirt", "to: blocks/Dirt", "[a-z0-9_]+"},
        {"to: blocks/dirt", "to: blocks/../../x", "[a-z0-9_]+"},
        {"to: blocks/dirt", "to: blocks/", "empty name"},
        {"to: blocks/dirt", "to: sideways/dirt", "not a group"},
        {"to: blocks/dirt", "to: dirt", "<group>/<name>"},
        {"to: blocks/dirt", "to: items/stone", "also written by"},
        {"from: a/stone.png", "from: a/dirt.png", "also imported by"},
        {"from: a/dirt.png, to", "form: a/dirt.png, to", "unknown key"},
        {"frames: 4", "frames: 3", "does not divide"},
        {"frames: 4", "frames: 0", "from 1 to 256"},
        {"frames: 4", "frames: 257", "from 1 to 256"},
        {"frames: 4", "frames: four", "import.yaml:15:48: key 'frames': expected an integer"},
        {"to: mobs/ore }", "to: mobs/ore, frames: 2 }", "works on one frame"},
        {"files: [b/unused.png]", "files: [b/unused.png, b/gone.png]", "does not have"},
        {"files: [b/unused.png]", "files: [b/unused.png, b/unused.png]", "skipped twice"},
        {"files: [b/unused.png]", "files: [b/unused.png, a/dirt.png]", "both imported and skipped"},
        {"files: [b/unused.png]", "files: []", "neither imports nor skips"},
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const tt_case_t c = tt_case_begin(t, i);
        tt_manifest_change(t, &c, cases[i].from, cases[i].to);
        tt_expect_refused(t, &c, cases[i].expected);
    }
}

TEST(a_wrong_or_changed_licence_is_refused) {
    const tt_case_t changed = tt_case_begin(t, 0);
    tt_write_text(t, tt_format(t, "%s/texture_pack.conf", changed.pack),
                  "license = CC-BY-SA-4.0 # no\nlicense = All-Rights-Reserved\n");
    tt_expect_refused(t, &changed, "states a licence other than");

    const tt_case_t no_conf = tt_case_begin(t, 1);
    REQUIRE(unlink(tt_format(t, "%s/texture_pack.conf", no_conf.pack)) == 0);
    tt_expect_refused(t, &no_conf, "the pack has no texture_pack.conf");

    /* A licence file that turns up has to be read by someone, wherever it is and however it is spelled. */
    const char *const names[] = {"LICENSE", "LICENCE.txt", "a/license.md", "COPYING", "b/Notice", "CREDITS.txt"};
    for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const tt_case_t c = tt_case_begin(t, 2u + i);
        tt_write_text(t, tt_format(t, "%s/%s", c.pack, names[i]), "All rights reserved.\n");
        tt_expect_refused(t, &c, "no one has reviewed it");
    }
}

TEST(a_clone_at_the_wrong_commit_is_refused) {
    const tt_head_case_t cases[] = {
        {TT_OTHER_COMMIT "\n", "pins"},
        {"garbage\n", "neither a commit nor a ref"},
        {"ref: refs/../../../../etc/passwd\n", "not a safe path"},
        {"ref: refs/heads/gone\n", "neither its own file nor packed-refs"},
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const tt_case_t c = tt_case_begin(t, i);
        tt_write_text(t, tt_format(t, "%s/.git/HEAD", c.pack), cases[i].head);
        tt_expect_refused(t, &c, cases[i].expected);
    }
    const tt_case_t bad_ref = tt_case_begin(t, 10);
    tt_write_text(t, tt_format(t, "%s/.git/refs/heads/master", bad_ref.pack), "not a hash\n");
    tt_expect_refused(t, &bad_ref, "does not hold a commit");

    const tt_case_t no_git = tt_case_begin(t, 11);
    tt_remove_tree(t, tt_format(t, "%s/.git", no_git.pack));
    tt_expect_refused(t, &no_git, "no .git/HEAD");

    /* A link for .git would let the pack choose what we read as its commit. */
    const tt_case_t linked = tt_case_begin(t, 12);
    const char *git = tt_format(t, "%s/.git", linked.pack);
    tt_remove_tree(t, git);
    REQUIRE(symlink("a", git) == 0);
    tt_expect_refused(t, &linked, "may be a symbolic link");
}

TEST(a_bad_png_refuses_the_whole_import) {
    /* a/water.png is the last texture, so everything before it decoded fine and must still not be written. */
    const tt_case_t flipped = tt_case_begin(t, 0);
    const char *water = tt_format(t, "%s/a/water.png", flipped.pack);
    uint64_t length = 0;
    uint8_t *bytes = tt_read(t, water, &length);
    bytes[length / 2u] ^= 0x40u;
    tt_write_bytes(t, water, bytes, length);
    tt_expect_refused(t, &flipped, "a/water.png: refused at byte");

    const tt_case_t truncated = tt_case_begin(t, 1);
    tt_write_bytes(t, tt_format(t, "%s/a/water.png", truncated.pack), bytes, length / 2u);
    tt_expect_refused(t, &truncated, "a/water.png: refused at byte");

    const tt_case_t not_png = tt_case_begin(t, 2);
    tt_write_text(t, tt_format(t, "%s/a/water.png", not_png.pack), "#!/bin/sh\necho hello\n");
    tt_expect_refused(t, &not_png, "a/water.png: refused at byte");

    const tt_case_t empty = tt_case_begin(t, 3);
    tt_write_bytes(t, tt_format(t, "%s/a/water.png", empty.pack), "", 0);
    tt_expect_refused(t, &empty, "a/water.png: refused at byte");

    /* A tiny file claiming a huge image, which must not get to ask for the memory. */
    const tt_case_t huge = tt_case_begin(t, 4);
    uint64_t huge_length = 0;
    const uint8_t *huge_png = tt_png_raw(t, 32768u, 32768u, (const uint8_t *)"x", 1u, &huge_length);
    tt_write_bytes(t, tt_format(t, "%s/a/water.png", huge.pack), huge_png, huge_length);
    tt_expect_refused(t, &huge, "a/water.png: refused at byte");

    /* Real, valid, and wider than .mctex allows. */
    const tt_case_t wide = tt_case_begin(t, 5);
    uint8_t *wide_pixels = test_alloc(t, 5000u * 4u);
    tt_write_png(t, tt_format(t, "%s/a/water.png", wide.pack), 5000u, 1u, wide_pixels);
    tt_manifest_change(t, &wide, ", frames: 4", "");
    tt_expect_refused(t, &wide, "over the .mctex limits");

    /* Over this file's own limit: a whole 17 MiB, sparse, so the test writes almost nothing. */
    const tt_case_t big = tt_case_begin(t, 6);
    const char *big_path = tt_format(t, "%s/a/water.png", big.pack);
    REQUIRE(truncate(big_path, 17 * 1024 * 1024) == 0);
    tt_expect_refused(t, &big, "over this file's limit");
}

TEST(compositing_needs_a_matching_opaque_base) {
    const tt_case_t see_through = tt_case_begin(t, 0);
    tt_write_png(t, tt_format(t, "%s/a/stone.png", see_through.pack), 2u, 2u, TT_ORE);
    tt_expect_refused(t, &see_through, "not fully opaque");

    const tt_case_t wrong_size = tt_case_begin(t, 1);
    uint8_t pixels[9u * 4u];
    memset(pixels, 255, sizeof(pixels));
    tt_write_png(t, tt_format(t, "%s/a/stone.png", wrong_size.pack), 3u, 3u, pixels);
    tt_expect_refused(t, &wrong_size, "under it is 3x3");
}

TEST(the_pack_cannot_lead_the_tool_anywhere) {
    /* A link to a file outside the pack, where the texture should be. */
    const tt_case_t link_file = tt_case_begin(t, 0);
    const char *secret = tt_format(t, "%s/secret.png", link_file.root);
    tt_write_png(t, secret, 2u, 2u, TT_DIRT);
    const char *dirt = tt_format(t, "%s/a/dirt.png", link_file.pack);
    REQUIRE(unlink(dirt) == 0);
    REQUIRE(symlink(secret, dirt) == 0);
    tt_expect_refused(t, &link_file, "links in a pack are not followed");

    /* A link for the texture's whole directory. */
    const tt_case_t link_directory = tt_case_begin(t, 1);
    const char *directory = tt_format(t, "%s/a", link_directory.pack);
    const char *real = tt_format(t, "%s/real", link_directory.root);
    REQUIRE(rename(directory, real) == 0);
    REQUIRE(symlink(real, directory) == 0);
    tt_expect_refused(t, &link_directory, "neither imports nor skips");

    /* A FIFO would hang any tool that opens it to read. The harness's timeout makes that a failure too. */
    const tt_case_t fifo = tt_case_begin(t, 2);
    const char *fifo_path = tt_format(t, "%s/a/dirt.png", fifo.pack);
    REQUIRE(unlink(fifo_path) == 0);
    REQUIRE(mkfifo(fifo_path, 0666) == 0);
    tt_expect_refused(t, &fifo, "not a regular file");

    /* A directory where a file should be. */
    const tt_case_t directory_as_file = tt_case_begin(t, 3);
    const char *png_directory = tt_format(t, "%s/a/dirt.png", directory_as_file.pack);
    REQUIRE(unlink(png_directory) == 0);
    REQUIRE(mkdir(png_directory, 0777) == 0);
    tt_expect_refused(t, &directory_as_file, "does not have");

    /* An escape sequence in a file name must not reach the terminal, or any file. */
    const tt_case_t escape = tt_case_begin(t, 4);
    tt_write_text(t, tt_format(t, "%s/a/\x1B[31mred.png", escape.pack), "x");
    test_process_t process;
    tt_import(t, &escape, &process);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "will not repeat");
    CHECK(strchr(process.output, 0x1B) == nullptr);
    CHECK(!tt_exists(escape.textures));

    /* A hidden file below the top is not git's. */
    const tt_case_t hidden = tt_case_begin(t, 5);
    tt_write_text(t, tt_format(t, "%s/a/.hidden", hidden.pack), "x");
    tt_expect_refused(t, &hidden, "will not repeat");

    /* A texture the manifest wants that is simply not there. */
    const tt_case_t missing = tt_case_begin(t, 7);
    REQUIRE(unlink(tt_format(t, "%s/a/dirt.png", missing.pack)) == 0);
    tt_expect_refused(t, &missing, "wants a/dirt.png, which the pack does not have");

    /* An unaccounted file of any kind. */
    const tt_case_t extra = tt_case_begin(t, 6);
    tt_write_png(t, tt_format(t, "%s/a/new.png", extra.pack), 2u, 2u, TT_DIRT);
    tt_expect_refused(t, &extra, "a/new.png is in the pack");
}

TEST(usage_mistakes_exit_2) {
    const char *const lists[][5] = {
        {"", nullptr},
        {"frobnicate", nullptr},
        {"import", "--manifest", "x", nullptr},
        {"verify", "--manifest", "x", nullptr},
        {"verify", "--manifest", "x", "--textures", nullptr},
    };
    for (uint32_t i = 0; i < sizeof(lists) / sizeof(lists[0]); i++) {
        char *argv[7] = {t->borrowed_args[0]};
        for (uint32_t j = 0; j < 5u && lists[i][j] != nullptr; j++) {
            argv[j + 1u] = (char *)lists[i][j];
        }
        test_process_t process;
        tt_run(t, argv, &process);
        CHECK_EQ_I64(process.exit_code, 2);
        CHECK_CONTAINS(process.output, "usage:");
    }
}

/* ---- verify, with no pack ------------------------------------------------------------------------------------ */

/* An imported tree, with the pack deleted, so that nothing below can be reading it. */
static tt_case_t tt_imported_no_pack(test_context_t *t, uint32_t label) {
    const tt_case_t c = tt_case_begin(t, label);
    tt_good_import(t, &c);
    tt_remove_tree(t, c.pack);
    return c;
}

static void tt_expect_verify_fails(test_context_t *t, const tt_case_t *c, const char *needle) {
    test_process_t process;
    tt_verify(t, c->manifest, c->textures, &process);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, needle);
}

TEST(verify_needs_no_pack) {
    const tt_case_t c = tt_imported_no_pack(t, 0);
    test_process_t process;
    tt_verify(t, c.manifest, c.textures, &process);
    CHECK_EQ_I64(process.exit_code, 0);
    CHECK_CONTAINS(process.output, "4 imported textures, 0 from other sources");
}

TEST(verify_accepts_what_stage_06_3_will_add) {
    const tt_case_t c = tt_imported_no_pack(t, 0);
    /* A texture of our own, with no pack behind it and a key this tool has never heard of. */
    tt_mkdirs(t, tt_format(t, "%s/mobs/zombie", c.textures));
    tt_write_text(t, tt_format(t, "%s/mobs/zombie/index.yaml", c.textures),
                  "name: zombie\ngroup: mobs\nsource: { kind: recipe }\nrecipe: { noise: 3 }\n");
    tt_write_text(t, tt_format(t, "%s/mobs/zombie/README.md", c.textures), "# ours\n");
    /* And another key on an imported one. */
    const char *yaml = tt_format(t, "%s/blocks/dirt/index.yaml", c.textures);
    uint64_t length = 0;
    const char *text = (const char *)tt_read(t, yaml, &length);
    tt_write_text(t, yaml, tt_format(t, "%semissive: 0\n", text));
    test_process_t process;
    tt_verify(t, c.manifest, c.textures, &process);
    CHECK_EQ_I64(process.exit_code, 0);
    CHECK_CONTAINS(process.output, "4 imported textures, 1 from other sources");

    /* The rule still holds for it. */
    REQUIRE(unlink(tt_format(t, "%s/mobs/zombie/README.md", c.textures)) == 0);
    tt_expect_verify_fails(t, &c, "zombie: has no README.md");
    tt_write_text(t, tt_format(t, "%s/mobs/zombie/README.md", c.textures), "# ours\n");
    REQUIRE(unlink(tt_format(t, "%s/mobs/zombie/index.yaml", c.textures)) == 0);
    tt_expect_verify_fails(t, &c, "zombie: has no index.yaml");
}

TEST(verify_catches_damage) {
    uint32_t label = 0;
    {
        const tt_case_t c = tt_imported_no_pack(t, label++);
        const char *path = tt_format(t, "%s/blocks/dirt/index.mctex", c.textures);
        uint64_t length = 0;
        uint8_t *bytes = tt_read(t, path, &length);
        bytes[length - 1u] ^= 1u;
        tt_write_bytes(t, path, bytes, length);
        tt_expect_verify_fails(t, &c, "do not match the header's hash");
    }
    {
        const tt_case_t c = tt_imported_no_pack(t, label++);
        const char *path = tt_format(t, "%s/blocks/dirt/index.mctex", c.textures);
        REQUIRE(truncate(path, 40) == 0);
        tt_expect_verify_fails(t, &c, "shorter than its header");
    }
    {
        const tt_case_t c = tt_imported_no_pack(t, label++);
        REQUIRE(unlink(tt_format(t, "%s/blocks/dirt/index.mctex", c.textures)) == 0);
        tt_expect_verify_fails(t, &c, "index.mctex: missing");
    }
    {
        const tt_case_t c = tt_imported_no_pack(t, label++);
        REQUIRE(unlink(tt_format(t, "%s/blocks/dirt/README.md", c.textures)) == 0);
        tt_expect_verify_fails(t, &c, "dirt: has no README.md");
    }
    {
        const tt_case_t c = tt_imported_no_pack(t, label++);
        REQUIRE(unlink(tt_format(t, "%s/blocks/dirt/index.yaml", c.textures)) == 0);
        tt_expect_verify_fails(t, &c, "dirt: has no index.yaml");
    }
    {
        /* A texture that is simply gone, directory and all. */
        const tt_case_t c = tt_imported_no_pack(t, label++);
        tt_remove_tree(t, tt_format(t, "%s/blocks/dirt", c.textures));
        tt_expect_verify_fails(t, &c, "it is not there");
    }
    {
        /* The licence removed from one README. */
        const tt_case_t c = tt_imported_no_pack(t, label++);
        tt_write_text(t, tt_format(t, "%s/blocks/dirt/README.md", c.textures), "# dirt\n");
        tt_expect_verify_fails(t, &c, "does not state the licence");
    }
    {
        /* The yaml disagreeing with the pixels. */
        const tt_case_t c = tt_imported_no_pack(t, label++);
        const char *path = tt_format(t, "%s/blocks/dirt/index.yaml", c.textures);
        tt_write_text(t, path,
                      "name: dirt\ngroup: blocks\nsize: { width: 3, height: 2, frames: 1 }\nsource:\n  kind: pack\n"
                      "  pack: test_pack\n  commit: \"" TT_COMMIT "\"\n  licence: CC-BY-SA-4.0\n  file: a/dirt.png\n");
        tt_expect_verify_fails(t, &c, "its size is not what index.mctex holds");
    }
    {
        /* The yaml disagreeing with the manifest: a changed licence is what this is really for. */
        const tt_case_t c = tt_imported_no_pack(t, label++);
        const char *path = tt_format(t, "%s/blocks/dirt/index.yaml", c.textures);
        tt_write_text(t, path,
                      "name: dirt\ngroup: blocks\nsize: { width: 2, height: 2, frames: 1 }\nsource:\n  kind: pack\n"
                      "  pack: test_pack\n  commit: \"" TT_COMMIT "\"\n  licence: CC0-1.0\n  file: a/dirt.png\n");
        tt_expect_verify_fails(t, &c, "does not say what the manifest says");
    }
    {
        /* Left behind by an old import. */
        const tt_case_t c = tt_imported_no_pack(t, label++);
        tt_mkdirs(t, tt_format(t, "%s/blocks/stale", c.textures));
        tt_write_text(t, tt_format(t, "%s/blocks/stale/index.yaml", c.textures), "source: { kind: pack }\n");
        tt_write_text(t, tt_format(t, "%s/blocks/stale/README.md", c.textures), "# stale\n");
        tt_expect_verify_fails(t, &c, "the manifest has no such texture");
    }
    {
        const tt_case_t c = tt_imported_no_pack(t, label++);
        tt_mkdirs(t, tt_format(t, "%s/sounds", c.textures));
        tt_expect_verify_fails(t, &c, "not one of the groups");
    }
    {
        const tt_case_t c = tt_imported_no_pack(t, label++);
        REQUIRE(unlink(tt_format(t, "%s/items/README.md", c.textures)) == 0);
        tt_expect_verify_fails(t, &c, "items: has no README.md");
    }
    {
        const tt_case_t c = tt_imported_no_pack(t, label++);
        REQUIRE(unlink(tt_format(t, "%s/ATTRIBUTION.md", c.textures)) == 0);
        tt_expect_verify_fails(t, &c, "the licence requires it");
    }
    {
        /* Attribution losing any one thing the licence asks for. */
        const char *const lost[][2] = {
            {"CC-BY-SA-4.0", "does not state CC-BY-SA-4.0"},
            {TT_COMMIT, "does not state " TT_COMMIT},
            {"Bob", "does not credit Bob"},
            {"| `blocks/dirt` |", "has no row for blocks/dirt"},
        };
        for (uint32_t i = 0; i < sizeof(lost) / sizeof(lost[0]); i++) {
            const tt_case_t c = tt_imported_no_pack(t, label++);
            const char *path = tt_format(t, "%s/ATTRIBUTION.md", c.textures);
            uint64_t length = 0;
            uint8_t *bytes = tt_read(t, path, &length);
            const uint64_t needle_length = strlen(lost[i][0]);
            uint8_t *at = bytes;
            while ((at = memmem(at, length - (uint64_t)(at - bytes), lost[i][0], needle_length)) != nullptr) {
                memset(at, 'x', needle_length);
            }
            tt_write_bytes(t, path, bytes, length);
            tt_expect_verify_fails(t, &c, lost[i][1]);
        }
    }
}

/* ---- The committed tree -------------------------------------------------------------------------------------- */

TEST(committed_textures_verify_with_no_pack) {
    test_process_t process;
    tt_verify(t, t->borrowed_args[1], t->borrowed_args[2], &process);
    if (!CHECK_EQ_I64(process.exit_code, 0)) {
        fprintf(stderr, "%s\n", process.output);
    }
    CHECK_CONTAINS(process.output, "verified");
}

/* Looked at directly, not through the tool: every texture directory has the two files, and they add up to the manifest. */
TEST(committed_directories_have_their_files) {
    const char *const groups[] = {"blocks", "items", "mobs", "ui"};
    uint64_t directories = 0;
    for (uint32_t g = 0; g < 4u; g++) {
        const char *group = tt_format(t, "%s/%s", t->borrowed_args[2], groups[g]);
        CHECK(tt_exists(tt_format(t, "%s/README.md", group)));
        DIR *listing = opendir(group);
        REQUIRE(listing != nullptr);
        uint64_t in_group = 0;
        struct dirent *item = readdir(listing);
        while (item != nullptr) {
            if (item->d_name[0] != '.' && strcmp(item->d_name, "README.md") != 0) {
                in_group++;
                const uint64_t mark = t->arena_used;
                CHECK(tt_exists(tt_format(t, "%s/%s/index.yaml", group, item->d_name)));
                CHECK(tt_exists(tt_format(t, "%s/%s/README.md", group, item->d_name)));
                t->arena_used = mark;
            }
            item = readdir(listing);
        }
        closedir(listing);
        CHECK(in_group > 0u);
        directories += in_group;
    }

    uint64_t length = 0;
    const char *manifest = (const char *)tt_read(t, t->borrowed_args[1], &length);
    uint64_t rows = 0;
    for (const char *at = strstr(manifest, "- { from: "); at != nullptr; at = strstr(at + 1, "- { from: ")) {
        rows++;
    }
    /* At least: Stage 06.3 may add directories of its own, and may not take any away. */
    CHECK(rows > 300u);
    CHECK(directories >= rows);
}

TEST(committed_attribution_is_there) {
    uint64_t length = 0;
    const char *credits = (const char *)tt_read(t, tt_format(t, "%s/ATTRIBUTION.md", t->borrowed_args[2]), &length);
    CHECK_CONTAINS(credits, "CC-BY-SA-4.0");
    CHECK_CONTAINS(credits, "Pixel Perfection");
    CHECK_CONTAINS(credits, "XSSheep");
    CHECK_CONTAINS(credits, "https://github.com/minetest-texture-packs/Pixel-Perfection");
}

/* What the renderer's first frame needs, spot-checked by hand, so a wrong mapping in the manifest cannot pass quietly. */
TEST(committed_textures_are_the_shapes_they_should_be) {
    const tt_shape_case_t cases[] = {
        {"blocks/dirt", 16u, 16u, 1u, 1, {0}},
        {"blocks/stone", 16u, 16u, 1u, 1, {0}},
        {"blocks/grass_top", 16u, 16u, 1u, 1, {0}},
        {"blocks/grass_side", 16u, 16u, 1u, 1, {0}},
        {"blocks/coal_ore", 16u, 16u, 1u, 1, {0}},
        {"blocks/diamond_ore", 16u, 16u, 1u, 1, {0}},
        {"blocks/water_still", 16u, 16u, 16u, 0, {0}},
        {"blocks/lava_still", 16u, 16u, 16u, 0, {0}},
        {"blocks/river_water_flow", 32u, 32u, 16u, 0, {0}},
        {"blocks/crack", 16u, 16u, 10u, 0, {0}},
        {"blocks/furnace_front_lit", 16u, 16u, 8u, 0, {0}},
        {"items/iron_pickaxe", 16u, 16u, 1u, 0, {0}},
        {"mobs/player", 64u, 32u, 1u, 0, {0}},
        {"ui/crosshair", 6u, 6u, 1u, 0, {0}},
        {"ui/hotbar", 162u, 22u, 1u, 0, {0}},
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint64_t length = 0;
        const uint8_t *file = tt_read(t, tt_format(t, "%s/%s/index.mctex", t->borrowed_args[2], cases[i].to), &length);
        mctex_view_t view;
        if (!CHECK_EQ_U64(mctex_parse(file, length, &view), MCTEX_OK)) {
            continue;
        }
        if (!CHECK_EQ_U64(view.width, cases[i].width) || !CHECK_EQ_U64(view.height, cases[i].height) ||
            !CHECK_EQ_U64(view.frame_count, cases[i].frames)) {
            fprintf(stderr, "  %s\n", cases[i].to);
        }
        if (cases[i].opaque != 0) {
            for (uint64_t p = 3u; p < view.pixel_bytes; p += 4u) {
                CHECK_EQ_U64(view.borrowed_pixels[p], 255u);
            }
        }
    }
}

TEST_MAIN()
