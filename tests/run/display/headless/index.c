/* ctest run_display_headless. See README.md. No card, console or input device is opened, here or in the program. */
#include "harness/index.h"

#include "png/index.h"
#include "display/headless/demo/index.h"
#include "display/headless/png/index.h"
#include "display/headless/script/index.h"
#include "display/index.h"
#include "file/index.h"
#include "heap/index.h"
#include "main/index.h"

#include <dirent.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum : uint32_t {
    TOUR_WIDTH = 320u,
    TOUR_HEIGHT = 240u,
    TOUR_LAST_TICK = 15u,
    PROGRAM_TIMEOUT_MS = 60000u,
};

/* ---- A temporary directory --------------------------------------------------------------------------------------- */

typedef struct temp_dir_t {
    char path[64];
} temp_dir_t;
static_assert(sizeof(temp_dir_t) == 64);

static bool temp_dir_make(temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_headless_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void temp_dir_remove(const temp_dir_t *dir) {
    DIR *const handle = opendir(dir->path);
    if (handle != nullptr) {
        char path[512];
        for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
            if (entry->d_name[0] != '.') {
                snprintf(path, sizeof(path), "%s/%s", dir->path, entry->d_name);
                unlink(path);
            }
        }
        closedir(handle);
    }
    rmdir(dir->path);
}

/* ---- A second opinion on a PNG ------------------------------------------------------------------------------------ */

/* Bit by bit, from the definitions, sharing nothing with the encoder. */
static uint32_t strict_crc32(const uint8_t *bytes, uint64_t length) {
    uint32_t crc = 0xffffffffu;
    for (uint64_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8u; bit++) {
            crc = (crc >> 1u) ^ ((crc & 1u) != 0u ? 0xedb88320u : 0u);
        }
    }
    return ~crc;
}

static uint32_t strict_adler32(const uint8_t *bytes, uint64_t length) {
    uint64_t low = 1u;
    uint64_t high = 0u;
    for (uint64_t i = 0; i < length; i++) {
        low = (low + bytes[i]) % 65521u;
        high = (high + low) % 65521u;
    }
    return (uint32_t)(high << 16u | low);
}

static uint32_t strict_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] << 24u | (uint32_t)bytes[1] << 16u | (uint32_t)bytes[2] << 8u | (uint32_t)bytes[3];
}

/* Walks a file against the specification, exactly as this encoder should have laid it out, and gives back the raw image
 * bytes (filter byte, then RGB, per row) from the stored blocks. Returns false at the first thing wrong. */
static bool strict_read(test_context_t *t, const uint8_t *file, uint64_t length, uint32_t width, uint32_t height,
                        uint8_t *raw, uint64_t raw_capacity) {
    static const uint8_t signature[8] = {0x89u, 'P', 'N', 'G', '\r', '\n', 0x1au, '\n'};
    const uint64_t raw_expected = ((uint64_t)width * 3u + 1u) * height;
    if (!CHECK(length > 8u) || !CHECK(memcmp(file, signature, 8u) == 0) || !CHECK(raw_capacity >= raw_expected)) {
        return false;
    }
    uint64_t at = 8u;
    uint32_t chunk_index = 0;
    uint64_t raw_at = 0;
    bool saw_final = false;
    bool saw_end = false;
    while (at < length) {
        if (!CHECK(at + 12u <= length) || !CHECK(!saw_end)) {
            return false;
        }
        const uint64_t payload = strict_u32(file + at);
        if (!CHECK(at + 12u + payload <= length)) {
            return false;
        }
        const uint8_t *const type = file + at + 4u;
        const uint8_t *const data = file + at + 8u;
        if (!CHECK_EQ_U64(strict_u32(data + payload), strict_crc32(type, 4u + payload))) {
            return false;
        }
        if (chunk_index == 0u) {
            if (!CHECK(memcmp(type, "IHDR", 4u) == 0) || !CHECK_EQ_U64(payload, 13u)) {
                return false;
            }
            CHECK_EQ_U64(strict_u32(data), width);
            CHECK_EQ_U64(strict_u32(data + 4u), height);
            CHECK_EQ_U64(data[8], 8u);  /* bit depth */
            CHECK_EQ_U64(data[9], 2u);  /* RGB */
            CHECK_EQ_U64(data[10], 0u); /* compression */
            CHECK_EQ_U64(data[11], 0u); /* filter method */
            CHECK_EQ_U64(data[12], 0u); /* interlace */
        } else if (memcmp(type, "IDAT", 4u) == 0) {
            if (!CHECK_EQ_U64(chunk_index, 1u) || !CHECK(payload >= 6u)) {
                return false;
            }
            CHECK_EQ_U64((data[0] & 0x0fu), 8u);                          /* DEFLATE */
            CHECK(data[0] >> 4u <= 7u);                                   /* a window PNG allows */
            CHECK_EQ_U64(((uint32_t)data[0] * 256u + data[1]) % 31u, 0u); /* the header's own check */
            CHECK_EQ_U64(data[1] & 0x20u, 0u);                            /* no preset dictionary */
            uint64_t z = 2u;
            while (!saw_final) {
                if (!CHECK(z + 5u <= payload - 4u)) {
                    return false;
                }
                const uint32_t header = data[z];
                CHECK_EQ_U64(header & 0x06u, 0u); /* BTYPE 00, stored */
                saw_final = (header & 1u) != 0u;
                const uint32_t block = (uint32_t)data[z + 1u] | (uint32_t)data[z + 2u] << 8u;
                const uint32_t inverse = (uint32_t)data[z + 3u] | (uint32_t)data[z + 4u] << 8u;
                CHECK_EQ_U64(block ^ inverse, 0xffffu);
                z += 5u;
                if (!CHECK(z + block <= payload - 4u) || !CHECK(raw_at + block <= raw_expected)) {
                    return false;
                }
                memcpy(raw + raw_at, data + z, block);
                raw_at += block;
                z += block;
            }
            CHECK_EQ_U64(z, payload - 4u); /* nothing between the last block and the trailer */
            CHECK_EQ_U64(strict_u32(data + payload - 4u), strict_adler32(raw, raw_at));
        } else if (memcmp(type, "IEND", 4u) == 0) {
            CHECK_EQ_U64(payload, 0u);
            saw_end = true;
        } else {
            CHECK(false); /* this encoder writes nothing else */
        }
        at += 12u + payload;
        chunk_index++;
    }
    return CHECK(saw_end) && CHECK(saw_final) && CHECK_EQ_U64(raw_at, raw_expected);
}

static uint32_t test_xrgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t)r << 16u | (uint32_t)g << 8u | b;
}

/* A picture where every byte differs by position, so that a swapped channel or a slipped row shows. */
static void test_fill_picture(uint32_t *pixels, uint32_t pitch_pixels, uint32_t width, uint32_t height) {
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            pixels[(uint64_t)y * pitch_pixels + x] =
                0xaa000000u | test_xrgb((uint8_t)(x * 7u + 1u), (uint8_t)(y * 11u + 2u), (uint8_t)((x + y) * 13u + 3u));
        }
    }
}

/* Both readers, on one encoded picture of this size. */
static void test_encode_round_trip(test_context_t *t, uint32_t width, uint32_t height, uint32_t pad_pixels) {
    const uint32_t pitch_pixels = width + pad_pixels;
    uint32_t *const pixels = test_alloc(t, (uint64_t)pitch_pixels * height * 4u);
    test_fill_picture(pixels, pitch_pixels, width, height);

    const uint64_t capacity = display_png_bytes_needed(width, height);
    REQUIRE(capacity != 0u);
    uint8_t *const file = test_alloc(t, capacity);
    uint64_t length = 0;
    REQUIRE(display_png_encode((const uint8_t *)pixels, pitch_pixels * 4u, width, height, file, capacity, &length));
    CHECK_EQ_U64(length, capacity);

    const uint64_t raw_capacity = ((uint64_t)width * 3u + 1u) * height;
    uint8_t *const raw = test_alloc(t, raw_capacity);
    REQUIRE(strict_read(t, file, length, width, height, raw, raw_capacity));
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *const row = raw + (uint64_t)y * ((uint64_t)width * 3u + 1u);
        CHECK_EQ_U64(row[0], 0u);
        for (uint32_t x = 0; x < width; x++) {
            const uint32_t want = pixels[(uint64_t)y * pitch_pixels + x];
            const uint32_t got = test_xrgb(row[1u + x * 3u], row[2u + x * 3u], row[3u + x * 3u]);
            if (!CHECK_EQ_U64(got, want & 0x00ffffffu)) {
                return;
            }
        }
    }

    png_info_t info;
    png_error_t error;
    REQUIRE(png_probe(file, length, &info, &error) == PNG_OK);
    CHECK_EQ_U64(info.width, width);
    CHECK_EQ_U64(info.height, height);
    CHECK_EQ_U64(info.color_type, PNG_COLOR_RGB);
    CHECK_EQ_U64(info.bit_depth, 8u);
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    REQUIRE(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            const uint8_t *const p = rgba + ((uint64_t)y * width + x) * 4u;
            if (!CHECK_EQ_U64(test_xrgb(p[0], p[1], p[2]), pixels[(uint64_t)y * pitch_pixels + x] & 0x00ffffffu) ||
                !CHECK_EQ_U64(p[3], 255u)) {
                return;
            }
        }
    }
}

/* ---- The PNG encoder --------------------------------------------------------------------------------------------- */

TEST(checksums_match_their_published_check_values) {
    CHECK_EQ_U64(display_png_crc32((const uint8_t *)"123456789", 9u), 0xcbf43926u);
    CHECK_EQ_U64(display_png_crc32((const uint8_t *)"IEND", 4u), 0xae426082u);
    CHECK_EQ_U64(display_png_crc32(nullptr, 0u), 0u);
    CHECK_EQ_U64(display_png_adler32((const uint8_t *)"Wikipedia", 9u), 0x11e60398u);
    CHECK_EQ_U64(display_png_adler32(nullptr, 0u), 1u);
}

TEST(adler_agrees_with_the_slow_definition_past_its_reduction_run) {
    const uint64_t length = 100000u;
    uint8_t *const bytes = test_alloc(t, length);
    memset(bytes, 0xff, length);
    CHECK_EQ_U64(display_png_adler32(bytes, length), strict_adler32(bytes, length));
}

TEST(smallest_image_is_valid) {
    test_encode_round_trip(t, 1u, 1u, 0u);
}

TEST(a_small_image_reads_back_exactly) {
    test_encode_round_trip(t, 7u, 5u, 0u);
}

TEST(rows_with_a_pitch_wider_than_the_image_read_back_exactly) {
    test_encode_round_trip(t, 13u, 9u, 3u);
}

TEST(an_image_exactly_one_stored_block_long_is_valid) {
    /* (1456 * 3 + 1) * 15 is 65535: one full block, marked final, and no empty block after it. */
    test_encode_round_trip(t, 1456u, 15u, 0u);
}

TEST(an_image_one_row_past_one_stored_block_is_valid) {
    test_encode_round_trip(t, 1456u, 16u, 0u);
}

TEST(an_image_of_many_stored_blocks_is_valid) {
    test_encode_round_trip(t, 640u, 360u, 0u);
}

TEST(the_same_pixels_make_the_same_bytes) {
    uint32_t pixels[64];
    test_fill_picture(pixels, 8u, 8u, 8u);
    const uint64_t capacity = display_png_bytes_needed(8u, 8u);
    uint8_t *const first = test_alloc(t, capacity);
    uint8_t *const second = test_alloc(t, capacity);
    uint64_t length = 0;
    REQUIRE(display_png_encode((const uint8_t *)pixels, 32u, 8u, 8u, first, capacity, &length));
    REQUIRE(display_png_encode((const uint8_t *)pixels, 32u, 8u, 8u, second, capacity, &length));
    CHECK_MEM_EQ(first, second, capacity);
}

TEST(the_decoder_refuses_a_damaged_file_at_every_part) {
    uint32_t pixels[64];
    test_fill_picture(pixels, 8u, 8u, 8u);
    const uint64_t capacity = display_png_bytes_needed(8u, 8u);
    uint8_t *const file = test_alloc(t, capacity);
    uint8_t *const rgba = test_alloc(t, 8u * 8u * 4u);
    uint64_t length = 0;
    REQUIRE(display_png_encode((const uint8_t *)pixels, 32u, 8u, 8u, file, capacity, &length));
    png_info_t info;
    png_error_t error;
    REQUIRE(png_probe(file, length, &info, &error) == PNG_OK);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    REQUIRE(png_decode(file, length, rgba, 256u, work, info.work_bytes, &info, &error) == PNG_OK);

    const uint64_t places[] = {0u, 8u + 8u + 1u, 33u + 8u + 10u, length - 5u};
    for (uint32_t i = 0; i < sizeof(places) / sizeof(places[0]); i++) {
        file[places[i]] ^= 0x40u;
        CHECK(png_decode(file, length, rgba, 256u, work, info.work_bytes, nullptr, &error) != PNG_OK);
        file[places[i]] ^= 0x40u;
    }
    CHECK(png_decode(file, length - 1u, rgba, 256u, work, info.work_bytes, nullptr, &error) != PNG_OK);
}

TEST(sizes_it_will_not_write_are_refused_and_nothing_is_written) {
    CHECK_EQ_U64(display_png_bytes_needed(0u, 5u), 0u);
    CHECK_EQ_U64(display_png_bytes_needed(5u, 0u), 0u);
    CHECK_EQ_U64(display_png_bytes_needed(DISPLAY_PNG_MAX_DIMENSION + 1u, 5u), 0u);
    CHECK_EQ_U64(display_png_bytes_needed(5u, DISPLAY_PNG_MAX_DIMENSION + 1u), 0u);
    CHECK(display_png_bytes_needed(DISPLAY_PNG_MAX_DIMENSION, DISPLAY_PNG_MAX_DIMENSION) < 0x80000000u);

    uint32_t pixels[4] = {0};
    uint8_t out[256];
    memset(out, 0x5a, sizeof(out));
    uint64_t length = 77u;
    const uint64_t needed = display_png_bytes_needed(2u, 2u);
    CHECK(needed <= sizeof(out));
    CHECK(!display_png_encode((const uint8_t *)pixels, 8u, 2u, 2u, out, needed - 1u, &length));
    CHECK(!display_png_encode((const uint8_t *)pixels, 7u, 2u, 2u, out, sizeof(out), &length));
    CHECK(!display_png_encode((const uint8_t *)pixels, 8u, 0u, 2u, out, sizeof(out), &length));
    CHECK_EQ_U64(length, 77u);
    CHECK_EQ_U64(out[0], 0x5au);
}

/* ---- The script language ------------------------------------------------------------------------------------------ */

enum : uint32_t { TEST_EVENTS = 32u };

static bool test_parse(test_context_t *t, const char *text, display_script_t *script, display_script_error_t *error) {
    display_script_event_t *const events = test_alloc(t, sizeof(display_script_event_t) * TEST_EVENTS);
    return display_script_parse(text, strlen(text), 20u, events, TEST_EVENTS, script, error);
}

TEST(every_verb_parses_with_its_values) {
    display_script_t s;
    display_script_error_t e;
    REQUIRE(test_parse(t,
                       "# a comment\n"
                       "\n"
                       "  0 key W down   # trailing comment\n"
                       "\t1\tkey escape up\r\n"
                       "2 key code:30 down\n"
                       "3 button left down\n"
                       "4 button MIDDLE up\n"
                       "5 move -12 +34\n"
                       "6 wheel -3\n"
                       "7 screenshot shot-1_a.png\n"
                       "8 screenshot\n"
                       "9 end",
                       &s, &e));
    REQUIRE(s.event_count == 9u);
    const display_script_event_t *const ev = s.borrowed_events;
    CHECK_EQ_U64(ev[0].kind, DISPLAY_SCRIPT_KEY);
    CHECK_EQ_U64(ev[0].code, KEY_W);
    CHECK_EQ_U64(ev[0].pressed, 1u);
    CHECK_EQ_U64(ev[0].line, 3u);
    CHECK_EQ_U64(ev[1].code, KEY_ESC);
    CHECK_EQ_U64(ev[1].pressed, 0u);
    CHECK_EQ_U64(ev[1].line, 4u);
    CHECK_EQ_U64(ev[2].code, 30u);
    CHECK_EQ_U64(ev[3].kind, DISPLAY_SCRIPT_KEY);
    CHECK_EQ_U64(ev[3].code, BTN_LEFT);
    CHECK_EQ_U64(ev[4].code, BTN_MIDDLE);
    CHECK_EQ_U64(ev[5].kind, DISPLAY_SCRIPT_MOVE);
    CHECK_EQ_I64(ev[5].dx, -12);
    CHECK_EQ_I64(ev[5].dy, 34);
    CHECK_EQ_U64(ev[6].kind, DISPLAY_SCRIPT_WHEEL);
    CHECK_EQ_I64(ev[6].dx, -3);
    CHECK_EQ_U64(ev[7].kind, DISPLAY_SCRIPT_SCREENSHOT);
    char name[DISPLAY_SCRIPT_NAME_BYTES];
    display_script_screenshot_name(&s, &ev[7], name);
    CHECK_STR_EQ(name, "shot-1_a.png");
    display_script_screenshot_name(&s, &ev[8], name);
    CHECK_STR_EQ(name, "tick_00000008.png");
    CHECK_EQ_U64(s.screenshot_count, 2u);
    CHECK_EQ_U64(s.has_end, 1u);
    CHECK_EQ_U64(s.last_tick, 9u);
}

TEST(times_can_be_ticks_or_milliseconds_and_absolute_or_relative) {
    display_script_t s;
    display_script_error_t e;
    REQUIRE(test_parse(t,
                       "10 wheel 1\n"
                       "+5 wheel 1\n"     /* 15 */
                       "1000ms wheel 1\n" /* 20 at 20 Hz */
                       "+500ms wheel 1\n" /* 30 */
                       "1549ms wheel 1\n" /* 30: rounded down, and equal is allowed */
                       "+0 wheel 1\n",    /* 30 */
                       &s, &e));
    static const uint32_t want[] = {10u, 15u, 20u, 30u, 30u, 30u};
    REQUIRE(s.event_count == 6u);
    for (uint32_t i = 0; i < 6u; i++) {
        CHECK_EQ_U64(s.borrowed_events[i].tick, want[i]);
    }
    CHECK_EQ_U64(s.has_end, 0u);
    CHECK_EQ_U64(s.last_tick, 30u);
}

TEST(an_empty_script_is_fine_and_is_one_tick) {
    display_script_t s;
    display_script_error_t e;
    REQUIRE(test_parse(t, "", &s, &e));
    CHECK_EQ_U64(s.event_count, 0u);
    CHECK_EQ_U64(s.last_tick, 0u);
    REQUIRE(test_parse(t, "# nothing\n\n", &s, &e));
    CHECK_EQ_U64(s.event_count, 0u);
}

typedef struct bad_script_t {
    const char *text;
    const char *needle;
    uint32_t line;
    uint8_t _pad0[4];
} bad_script_t;
static_assert(sizeof(bad_script_t) == 24);

TEST(a_bad_line_is_refused_by_number_saying_what_it_wanted) {
    static const bad_script_t cases[] = {
        {"banana key w down\n", "not a time", 1u, {0}},
        {"1 key\n", "takes a name", 1u, {0}},
        {"\n\n1 key nosuchkey down\n", "no key called 'nosuchkey'", 3u, {0}},
        {"1 key w sideways\n", "should be down or up", 1u, {0}},
        {"1 key w down extra\n", "takes a name", 1u, {0}},
        {"1 key code:768 down\n", "no key called", 1u, {0}},
        {"1 button w down\n", "no button called", 1u, {0}},
        {"1 move 5\n", "move takes", 1u, {0}},
        {"1 move 1000001 0\n", "move takes", 1u, {0}},
        {"1 move x y\n", "move takes", 1u, {0}},
        {"1 wheel 1001\n", "wheel takes", 1u, {0}},
        {"1 wheel\n", "wheel takes", 1u, {0}},
        {"1 jump\n", "'jump' is not one of", 1u, {0}},
        {"5\n", "a time and then", 1u, {0}},
        {"5 wheel 1\n3 wheel 1\n", "goes back in time", 2u, {0}},
        {"99999999999 wheel 1\n", "not a time", 1u, {0}},
        {"2147483648 wheel 1\n", "past the last", 1u, {0}},
        {"1 end\n2 wheel 1\n", "nothing may follow end", 2u, {0}},
        {"1 end now\n", "end takes nothing", 1u, {0}},
        {"1 screenshot a.png b.png\n", "at most a file name", 1u, {0}},
        {"1 screenshot ../up.png\n", "not a name to use", 1u, {0}},
        {"1 screenshot a/b.png\n", "not a name to use", 1u, {0}},
        {"1 screenshot .hidden.png\n", "not a name to use", 1u, {0}},
        {"1 screenshot a.jpg\n", "not a name to use", 1u, {0}},
        {"1 screenshot a.png\n2 screenshot a.png\n", "already used", 2u, {0}},
        {"1 screenshot\n1 screenshot\n", "already used", 2u, {0}},
        {"1 screenshot tick_00000002.png\n2 screenshot\n", "already used", 2u, {0}},
        {"1 wheel 1 \x01\n", "is not text", 1u, {0}},
        {"1 a b c d e f g\n", "too many words", 1u, {0}},
        {"ms wheel 1\n", "not a time", 1u, {0}},
        {"+ wheel 1\n", "not a time", 1u, {0}},
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        display_script_t s;
        display_script_error_t e;
        const bool parsed = test_parse(t, cases[i].text, &s, &e);
        if (!CHECK(!parsed)) {
            fprintf(stderr, "should have been refused: %s", cases[i].text);
            continue;
        }
        CHECK_EQ_U64(e.line, cases[i].line);
        CHECK_CONTAINS(e.message, cases[i].needle);
        CHECK_EQ_U64(s.event_count, 0u);
    }
}

TEST(a_script_with_more_events_than_room_is_refused_and_not_overrun) {
    display_script_event_t events[3];
    memset(events, 0xee, sizeof(events));
    display_script_t s;
    display_script_error_t e;
    const char *const text = "1 wheel 1\n2 wheel 1\n3 wheel 1\n";
    CHECK(!display_script_parse(text, strlen(text), 20u, events, 2u, &s, &e));
    CHECK_CONTAINS(e.message, "more than 2 events");
    CHECK_EQ_U64(events[2].tick, 0xeeeeeeeeu);
}

TEST(a_nul_inside_a_script_is_refused) {
    display_script_event_t events[3];
    display_script_t s;
    display_script_error_t e;
    CHECK(!display_script_parse("1 wheel\0 1\n", 11u, 20u, events, 3u, &s, &e));
    CHECK_CONTAINS(e.message, "is not text");
}

TEST(state_adds_up_events_and_clears_only_the_ticks_own) {
    display_script_state_t state;
    memset(&state, 0, sizeof(state));
    const display_script_event_t key_down = {.kind = DISPLAY_SCRIPT_KEY, .code = KEY_W, .pressed = 1u};
    const display_script_event_t key_up = {.kind = DISPLAY_SCRIPT_KEY, .code = KEY_W, .pressed = 0u};
    const display_script_event_t button = {.kind = DISPLAY_SCRIPT_KEY, .code = BTN_LEFT, .pressed = 1u};
    const display_script_event_t top = {.kind = DISPLAY_SCRIPT_KEY, .code = KEY_CNT - 1u, .pressed = 1u};
    const display_script_event_t move = {.kind = DISPLAY_SCRIPT_MOVE, .dx = 5, .dy = -7};
    const display_script_event_t wheel = {.kind = DISPLAY_SCRIPT_WHEEL, .dx = 2};
    const display_script_event_t shot = {.kind = DISPLAY_SCRIPT_SCREENSHOT};

    display_script_state_begin_tick(&state, 1u);
    display_script_state_apply(&state, &key_down);
    display_script_state_apply(&state, &button);
    display_script_state_apply(&state, &top);
    display_script_state_apply(&state, &move);
    display_script_state_apply(&state, &move);
    display_script_state_apply(&state, &wheel);
    display_script_state_apply(&state, &shot);
    CHECK(display_script_state_is_down(&state, KEY_W));
    CHECK(display_script_state_is_down(&state, BTN_LEFT));
    CHECK(display_script_state_is_down(&state, KEY_CNT - 1u));
    CHECK(!display_script_state_is_down(&state, KEY_S));
    CHECK(!display_script_state_is_down(&state, KEY_CNT));
    CHECK_EQ_I64(state.tick_dx, 10);
    CHECK_EQ_I64(state.tick_dy, -14);
    CHECK_EQ_I64(state.tick_wheel, 2);

    display_script_state_begin_tick(&state, 2u);
    display_script_state_apply(&state, &key_up);
    CHECK_EQ_U64(state.tick, 2u);
    CHECK(!display_script_state_is_down(&state, KEY_W));
    CHECK(display_script_state_is_down(&state, BTN_LEFT));
    CHECK_EQ_I64(state.tick_dx, 0);
    CHECK_EQ_I64(state.tick_wheel, 0);
    CHECK_EQ_I64(state.mouse_x, 10);
    CHECK_EQ_I64(state.mouse_y, -14);
    CHECK_EQ_I64(state.wheel, 2);
}

/* ---- The backend ------------------------------------------------------------------------------------------------- */

/* Writes text to a file in dir, and gives back its path. */
static const char *test_write_script(test_context_t *t, const temp_dir_t *dir, const char *text) {
    char *const path = test_alloc(t, 128u);
    snprintf(path, 128u, "%s/script.txt", dir->path);
    file_t file;
    if (file_open(&file, path, FILE_MODE_WRITE) != FILE_OK || file_write_all(&file, text, strlen(text)) != FILE_OK) {
        CHECK(false);
    }
    (void)file_close(&file);
    return path;
}

static display_headless_config_t test_config(const char *script_path, const char *dir, uint32_t tick_count) {
    return (display_headless_config_t){.script_path = script_path,
                                       .screenshot_dir = dir,
                                       .width = 64u,
                                       .height = 48u,
                                       .tick_count = tick_count,
                                       .tick_hz = 20u};
}

typedef struct test_scene_t {
    uint64_t calls_in_order_bad;
    uint32_t ticks_seen;
    uint32_t draws_seen;
    uint32_t events_seen;
    uint32_t fail_on_tick; /* UINT32_MAX for never */
    int64_t mouse_x_at_5;
    uint8_t w_down_at_5;
    uint8_t w_down_at_6;
    uint8_t fail_in_draw;
    uint8_t _pad0[5];
} test_scene_t;
static_assert(sizeof(test_scene_t) == 40);

static bool test_scene_tick(void *context, const display_headless_tick_t *tick) {
    test_scene_t *const scene = context;
    if (tick->tick != scene->ticks_seen || scene->draws_seen != scene->ticks_seen || tick->state->tick != tick->tick) {
        scene->calls_in_order_bad++;
    }
    scene->ticks_seen++;
    scene->events_seen += tick->event_count;
    for (uint32_t i = 0; i < tick->event_count; i++) {
        if (tick->events[i].tick != tick->tick) {
            scene->calls_in_order_bad++;
        }
    }
    if (tick->tick == 5u) {
        scene->mouse_x_at_5 = tick->state->mouse_x;
        scene->w_down_at_5 = display_script_state_is_down(tick->state, KEY_W) ? 1u : 0u;
    }
    if (tick->tick == 6u) {
        scene->w_down_at_6 = display_script_state_is_down(tick->state, KEY_W) ? 1u : 0u;
    }
    return scene->fail_in_draw != 0u || tick->tick != scene->fail_on_tick;
}

/* Paints the frame one colour by tick number, so a screenshot can show which tick wrote it. */
static bool test_scene_draw(void *context, const display_headless_tick_t *tick, display_buffer_t *frame) {
    test_scene_t *const scene = context;
    scene->draws_seen++;
    if (scene->draws_seen != scene->ticks_seen) {
        scene->calls_in_order_bad++;
    }
    for (uint32_t y = 0; y < frame->height; y++) {
        uint32_t *const row = (uint32_t *)(void *)(frame->owned_map + (uint64_t)y * frame->pitch);
        for (uint32_t x = 0; x < frame->width; x++) {
            row[x] = 0x00100000u * (tick->tick + 1u);
        }
    }
    return scene->fail_in_draw == 0u || tick->tick != scene->fail_on_tick;
}

static uint8_t *test_read_file(test_context_t *t, const char *path, uint64_t *out_length) {
    uint64_t length = 0;
    if (file_path_size(path, &length) != FILE_OK) {
        CHECK(false);
        return nullptr;
    }
    uint8_t *const bytes = test_alloc(t, length + 1u);
    if (file_read_whole(path, bytes, length + 1u, out_length) != FILE_OK) {
        CHECK(false);
        return nullptr;
    }
    return bytes;
}

/* Decodes a PNG file to RGBA with compile/src/png, or nullptr with a failed check. */
static uint8_t *test_decode_file(test_context_t *t, const char *path, png_info_t *info) {
    uint64_t length = 0;
    const uint8_t *const file = test_read_file(t, path, &length);
    if (file == nullptr) {
        return nullptr;
    }
    png_error_t error;
    if (!CHECK(png_probe(file, length, info, &error) == PNG_OK)) {
        fprintf(stderr, "%s: %s\n", path, error.message);
        return nullptr;
    }
    uint8_t *const rgba = test_alloc(t, info->pixel_bytes);
    uint8_t *const work = test_alloc(t, info->work_bytes);
    if (!CHECK(png_decode(file, length, rgba, info->pixel_bytes, work, info->work_bytes, info, &error) == PNG_OK)) {
        fprintf(stderr, "%s: %s\n", path, error.message);
        return nullptr;
    }
    return rgba;
}

static uint32_t test_pixel(const uint8_t *rgba, uint32_t width, uint32_t x, uint32_t y) {
    const uint8_t *const p = rgba + ((uint64_t)y * width + x) * 4u;
    return test_xrgb(p[0], p[1], p[2]);
}

static void backend_runs_ticks_in_order_with_their_input_and_shots(test_context_t *t, const temp_dir_t *dir) {
    const char *const script = test_write_script(t, dir,
                                                 "2 key w down\n"
                                                 "3 screenshot three.png\n"
                                                 "5 move 9 0\n"
                                                 "5 screenshot\n"
                                                 "6 key w up\n"
                                                 "7 end\n");
    const display_headless_config_t config = test_config(script, dir->path, 0u);
    display_offscreen_t offscreen;
    REQUIRE(display_offscreen_init(&offscreen, &config));
    heap_counters_t before;
    heap_counters_read(&before);

    test_scene_t scene_state = {.fail_on_tick = UINT32_MAX};
    const display_headless_scene_t scene = {
        .borrowed_context = &scene_state, .tick = test_scene_tick, .draw = test_scene_draw};
    const bool ran = display_headless_run(&offscreen.headless, &scene);

    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls); /* no heap calls in the loop */
    const uint64_t frames = offscreen.headless.frames_presented;
    const uint64_t shots = offscreen.headless.screenshots_written;
    display_offscreen_deinit(&offscreen);
    REQUIRE(ran);
    CHECK_EQ_U64(scene_state.calls_in_order_bad, 0u);
    CHECK_EQ_U64(scene_state.ticks_seen, 8u); /* 0 to 7: the tick of end runs */
    CHECK_EQ_U64(scene_state.draws_seen, 8u);
    CHECK_EQ_U64(scene_state.events_seen, 5u);
    CHECK_EQ_I64(scene_state.mouse_x_at_5, 9);
    CHECK_EQ_U64(scene_state.w_down_at_5, 1u);
    CHECK_EQ_U64(scene_state.w_down_at_6, 0u);
    CHECK_EQ_U64(frames, 8u);
    CHECK_EQ_U64(shots, 2u);

    /* Each shot shows its own tick's frame. */
    char path[128];
    png_info_t info;
    snprintf(path, sizeof(path), "%s/three.png", dir->path);
    const uint8_t *const three = test_decode_file(t, path, &info);
    REQUIRE(three != nullptr);
    CHECK_EQ_U64(info.width, 64u);
    CHECK_EQ_U64(info.height, 48u);
    CHECK_EQ_U64(test_pixel(three, 64u, 63u, 47u), 0x00400000u);
    snprintf(path, sizeof(path), "%s/tick_00000005.png", dir->path);
    const uint8_t *const five = test_decode_file(t, path, &info);
    REQUIRE(five != nullptr);
    CHECK_EQ_U64(test_pixel(five, 64u, 0u, 0u), 0x00600000u);
}

TEST(backend_runs_ticks_in_order_with_their_input_and_shots) {
    temp_dir_t dir;
    REQUIRE(temp_dir_make(&dir));
    heap_counters_t before;
    heap_counters_read(&before);
    backend_runs_ticks_in_order_with_their_input_and_shots(t, &dir);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes); /* the Owner gave everything back */
    temp_dir_remove(&dir);
}

TEST(a_run_with_no_script_is_a_tick_count_of_silence) {
    const display_headless_config_t config = test_config(nullptr, nullptr, 3u);
    display_offscreen_t offscreen;
    REQUIRE(display_offscreen_init(&offscreen, &config));
    test_scene_t scene_state = {.fail_on_tick = UINT32_MAX};
    const display_headless_scene_t scene = {
        .borrowed_context = &scene_state, .tick = test_scene_tick, .draw = test_scene_draw};
    CHECK(display_headless_run(&offscreen.headless, &scene));
    CHECK_EQ_U64(scene_state.ticks_seen, 3u);
    CHECK_EQ_U64(scene_state.events_seen, 0u);
    display_offscreen_deinit(&offscreen);
}

TEST(a_scene_can_leave_out_either_function) {
    const display_headless_config_t config = test_config(nullptr, nullptr, 2u);
    display_offscreen_t offscreen;
    REQUIRE(display_offscreen_init(&offscreen, &config));
    const display_headless_scene_t scene = {.borrowed_context = nullptr, .tick = nullptr, .draw = nullptr};
    CHECK(display_headless_run(&offscreen.headless, &scene));
    CHECK_EQ_U64(offscreen.headless.frames_presented, 2u);
    display_offscreen_deinit(&offscreen);
}

static void backend_stops_on_failure(test_context_t *t, const temp_dir_t *dir) {
    const char *const script = test_write_script(t, dir, "4 screenshot never.png\n");
    const display_headless_config_t config = test_config(script, dir->path, 0u);
    for (uint8_t in_draw = 0; in_draw < 2u; in_draw++) {
        display_offscreen_t offscreen;
        REQUIRE(display_offscreen_init(&offscreen, &config));
        test_scene_t scene_state = {.fail_on_tick = 2u, .fail_in_draw = in_draw};
        const display_headless_scene_t scene = {
            .borrowed_context = &scene_state, .tick = test_scene_tick, .draw = test_scene_draw};
        CHECK(!display_headless_run(&offscreen.headless, &scene));
        CHECK_EQ_U64(scene_state.ticks_seen, 3u);
        CHECK_EQ_U64(offscreen.headless.screenshots_written, 0u);
        display_offscreen_deinit(&offscreen);
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/never.png", dir->path);
    CHECK(access(path, F_OK) != 0);
}

TEST(a_failing_scene_ends_the_run_as_failed) {
    temp_dir_t dir;
    REQUIRE(temp_dir_make(&dir));
    backend_stops_on_failure(t, &dir);
    temp_dir_remove(&dir);
}

static void backend_unwritable(test_context_t *t, const temp_dir_t *dir) {
    const char *const script = test_write_script(t, dir, "0 screenshot a.png\n");
    /* A file sits where the screenshot's own temporary would go. */
    char blocker[128];
    snprintf(blocker, sizeof(blocker), "%s/a.png.tmp", dir->path);
    REQUIRE(mkdir(blocker, 0700) == 0);
    const display_headless_config_t config = test_config(script, dir->path, 0u);
    display_offscreen_t offscreen;
    REQUIRE(display_offscreen_init(&offscreen, &config));
    const display_headless_scene_t scene = {.borrowed_context = nullptr, .tick = nullptr, .draw = nullptr};
    CHECK(!display_headless_run(&offscreen.headless, &scene));
    CHECK_EQ_U64(offscreen.headless.screenshots_written, 0u);
    display_offscreen_deinit(&offscreen);
    rmdir(blocker);
}

TEST(a_screenshot_that_cannot_be_written_ends_the_run_as_failed) {
    temp_dir_t dir;
    REQUIRE(temp_dir_make(&dir));
    backend_unwritable(t, &dir);
    temp_dir_remove(&dir);
}

static void refusals(test_context_t *t, const temp_dir_t *dir) {
    heap_counters_t before;
    heap_counters_read(&before);
    const char *const with_end = test_write_script(t, dir, "3 end\n");
    display_offscreen_t offscreen;
    display_headless_config_t config = test_config(with_end, nullptr, 9u);
    CHECK(!display_offscreen_init(&offscreen, &config)); /* --ticks and end both say when */

    const char *const with_shot = test_write_script(t, dir, "3 screenshot\n");
    config = test_config(with_shot, nullptr, 0u);
    CHECK(!display_offscreen_init(&offscreen, &config)); /* nowhere to put it */
    config = test_config(with_shot, dir->path, 3u);
    CHECK(!display_offscreen_init(&offscreen, &config)); /* ticks 0 to 2 would drop the line at tick 3 */
    config = test_config(with_shot, dir->path, 4u);
    CHECK(display_offscreen_init(&offscreen, &config)); /* and this is just enough */
    display_offscreen_deinit(&offscreen);

    config = test_config(nullptr, nullptr, 0u);
    CHECK(!display_offscreen_init(&offscreen, &config)); /* how long? */
    config = test_config("/nonexistent/script", nullptr, 0u);
    CHECK(!display_offscreen_init(&offscreen, &config));
    config = test_config(nullptr, nullptr, 1u);
    config.width = 0u;
    CHECK(!display_offscreen_init(&offscreen, &config));
    config.width = DISPLAY_PNG_MAX_DIMENSION + 1u;
    CHECK(!display_offscreen_init(&offscreen, &config));

    const char *const bad = test_write_script(t, dir, "nonsense\n");
    config = test_config(bad, nullptr, 0u);
    CHECK(!display_offscreen_init(&offscreen, &config));

    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes); /* every refusal left nothing behind */
}

TEST(what_it_cannot_do_it_refuses_and_leaves_nothing_behind) {
    temp_dir_t dir;
    REQUIRE(temp_dir_make(&dir));
    refusals(t, &dir);
    temp_dir_remove(&dir);
}

TEST(deinit_of_a_zeroed_backend_does_nothing) {
    display_offscreen_t offscreen;
    memset(&offscreen, 0, sizeof(offscreen));
    display_offscreen_deinit(&offscreen);
    CHECK_EQ_U64(offscreen.running, 0u);
}

/* ---- The demo scene ---------------------------------------------------------------------------------------------- */

/* A frame on the test arena, with tick 0's state. */
static display_buffer_t demo_frame(test_context_t *t, uint32_t width, uint32_t height) {
    return (display_buffer_t){.owned_map = test_alloc(t, (uint64_t)width * height * 4u),
                              .map_bytes = (uint64_t)width * height * 4u,
                              .pitch = width * 4u,
                              .width = width,
                              .height = height};
}

static uint32_t demo_pixel(const display_buffer_t *frame, uint32_t x, uint32_t y) {
    uint32_t value = 0;
    memcpy(&value, frame->owned_map + (uint64_t)y * frame->pitch + (uint64_t)x * 4u, 4u);
    return value;
}

TEST(demo_clips_to_any_frame_size) {
    static const uint32_t sizes[][2] = {{1u, 1u}, {2u, 3u}, {7u, 200u}, {200u, 7u}, {40u, 40u}};
    for (uint32_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        display_buffer_t frame = demo_frame(t, sizes[i][0], sizes[i][1]);
        display_headless_demo_t demo;
        display_headless_scene_t scene;
        display_headless_demo_scene(&demo, &scene);
        display_script_state_t state;
        memset(&state, 0, sizeof(state));
        state.mouse_x = 1000000;
        state.mouse_y = -1000000;
        state.wheel = 1000000;
        const display_headless_tick_t tick = {
            .events = nullptr, .state = &state, .event_count = 0u, .tick = 0xffffffffu};
        CHECK(scene.draw(scene.borrowed_context, &tick, &frame));
        state.mouse_x = -1000000;
        state.mouse_y = 1000000;
        state.wheel = -1000000;
        CHECK(scene.draw(scene.borrowed_context, &tick, &frame));
    }
}

TEST(demo_shows_input_and_repaints_what_the_cursor_left) {
    display_buffer_t frame = demo_frame(t, 640u, 360u);
    display_headless_demo_t demo;
    display_headless_scene_t scene;
    display_headless_demo_scene(&demo, &scene);
    display_script_state_t state;
    memset(&state, 0, sizeof(state));
    display_headless_tick_t tick = {.events = nullptr, .state = &state, .event_count = 0u, .tick = 0u};
    REQUIRE(scene.draw(scene.borrowed_context, &tick, &frame));
    CHECK_EQ_U64(demo_pixel(&frame, 320u, 180u), DISPLAY_DEMO_CURSOR_IDLE);
    uint32_t x = 0;
    uint32_t y = 0;
    display_headless_demo_key_lamp(KEY_W, &x, &y);
    CHECK_EQ_U64(demo_pixel(&frame, x, y), DISPLAY_DEMO_LAMP_OFF);

    const display_script_event_t key = {.kind = DISPLAY_SCRIPT_KEY, .code = KEY_W, .pressed = 1u};
    const display_script_event_t button = {.kind = DISPLAY_SCRIPT_KEY, .code = BTN_RIGHT, .pressed = 1u};
    const display_script_event_t move = {.kind = DISPLAY_SCRIPT_MOVE, .dx = 100, .dy = 50};
    display_script_state_apply(&state, &key);
    display_script_state_apply(&state, &button);
    display_script_state_apply(&state, &move);
    tick.tick = 5u;
    REQUIRE(scene.draw(scene.borrowed_context, &tick, &frame));
    CHECK_EQ_U64(demo_pixel(&frame, 420u, 230u), DISPLAY_DEMO_CURSOR_RIGHT);
    CHECK_EQ_U64(demo_pixel(&frame, 320u, 180u), display_flip_pattern_pixel(320u, 180u, 640u, 360u));
    CHECK_EQ_U64(demo_pixel(&frame, x, y), DISPLAY_DEMO_KEY_ON);
    display_headless_demo_tick_lamp(0u, &x, &y);
    CHECK_EQ_U64(demo_pixel(&frame, x, y), DISPLAY_DEMO_TICK_ON);
    display_headless_demo_tick_lamp(1u, &x, &y);
    CHECK_EQ_U64(demo_pixel(&frame, x, y), DISPLAY_DEMO_LAMP_OFF);
    display_headless_demo_tick_lamp(2u, &x, &y);
    CHECK_EQ_U64(demo_pixel(&frame, x, y), DISPLAY_DEMO_TICK_ON);
}

/* ---- The built program ------------------------------------------------------------------------------------------- */

static const char *const tour_path = "tests/run/display/headless/fixtures/tour.script";

static void test_program(test_context_t *t, const char *script, const char *dir, const char *width, const char *height,
                         test_process_t *out) {
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--script",
                          (char *)script,
                          (char *)"--screenshot-dir",
                          (char *)dir,
                          (char *)"--width",
                          (char *)width,
                          (char *)"--height",
                          (char *)height,
                          nullptr};
    test_process_run(t, argv, PROGRAM_TIMEOUT_MS, out);
}

static void tour_check_lamps(test_context_t *t, const uint8_t *rgba, uint32_t tick, bool w_down) {
    uint32_t x = 0;
    uint32_t y = 0;
    display_headless_demo_key_lamp(KEY_W, &x, &y);
    CHECK_EQ_U64(test_pixel(rgba, TOUR_WIDTH, x, y), w_down ? DISPLAY_DEMO_KEY_ON : DISPLAY_DEMO_LAMP_OFF);
    for (uint32_t bit = 0; bit < 8u; bit++) {
        display_headless_demo_tick_lamp(bit, &x, &y);
        CHECK_EQ_U64(test_pixel(rgba, TOUR_WIDTH, x, y),
                     (tick >> bit & 1u) != 0u ? DISPLAY_DEMO_TICK_ON : DISPLAY_DEMO_LAMP_OFF);
    }
}

static void program_tour(test_context_t *t, const temp_dir_t *dir) {
    test_process_t run;
    test_program(t, tour_path, dir->path, "320", "240", &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_U64(run.timed_out, 0u);
    if (!CHECK_EQ_I64(run.exit_code, 0)) {
        fprintf(stderr, "%s\n", run.output);
        return;
    }
    CHECK_CONTAINS(run.output, "16 frames, 3 screenshots");
    CHECK_CONTAINS(run.output, "0 bytes live");

    char path[128];
    png_info_t info;
    snprintf(path, sizeof(path), "%s/start.png", dir->path);
    const uint8_t *const start = test_decode_file(t, path, &info);
    REQUIRE(start != nullptr);
    CHECK_EQ_U64(info.width, TOUR_WIDTH);
    CHECK_EQ_U64(info.height, TOUR_HEIGHT);
    tour_check_lamps(t, start, 0u, false);
    CHECK_EQ_U64(test_pixel(start, TOUR_WIDTH, 160u, 120u), DISPLAY_DEMO_CURSOR_IDLE);

    /* Tick 5 holds w, has moved by (60, -40), and has the left button down, written on the line after the screenshot. */
    snprintf(path, sizeof(path), "%s/moved.png", dir->path);
    const uint8_t *const moved = test_decode_file(t, path, &info);
    REQUIRE(moved != nullptr);
    tour_check_lamps(t, moved, 5u, true);
    CHECK_EQ_U64(test_pixel(moved, TOUR_WIDTH, 220u, 80u), DISPLAY_DEMO_CURSOR_LEFT);
    CHECK_EQ_U64(test_pixel(moved, TOUR_WIDTH, 160u, 120u), display_flip_pattern_pixel(160u, 120u, 320u, 240u));

    /* Tick 14 has let go of both, and the wheel at 4 has made the cursor 48 wide: its edges at 196 and 244. y = 90 falls
     * between two rows of lamps, so what is beside the cursor there is the pattern. */
    snprintf(path, sizeof(path), "%s/tick_00000014.png", dir->path);
    const uint8_t *const released = test_decode_file(t, path, &info);
    REQUIRE(released != nullptr);
    tour_check_lamps(t, released, 14u, false);
    CHECK_EQ_U64(test_pixel(released, TOUR_WIDTH, 196u, 90u), DISPLAY_DEMO_CURSOR_IDLE);
    CHECK_EQ_U64(test_pixel(released, TOUR_WIDTH, 243u, 90u), DISPLAY_DEMO_CURSOR_IDLE);
    CHECK_EQ_U64(test_pixel(released, TOUR_WIDTH, 195u, 90u), display_flip_pattern_pixel(195u, 90u, 320u, 240u));
    CHECK_EQ_U64(test_pixel(released, TOUR_WIDTH, 244u, 90u), display_flip_pattern_pixel(244u, 90u, 320u, 240u));
}

TEST(program_replays_the_tour_and_its_screenshots_show_the_input) {
    temp_dir_t dir;
    REQUIRE(temp_dir_make(&dir));
    program_tour(t, &dir);
    temp_dir_remove(&dir);
}

static void program_twice(test_context_t *t, const temp_dir_t *first, const temp_dir_t *second) {
    test_process_t run;
    test_program(t, tour_path, first->path, "320", "240", &run);
    REQUIRE(run.exit_code == 0);
    test_program(t, tour_path, second->path, "320", "240", &run);
    REQUIRE(run.exit_code == 0);
    static const char *const names[] = {"start.png", "moved.png", "tick_00000014.png"};
    for (uint32_t i = 0; i < 3u; i++) {
        char path[128];
        uint64_t first_length = 0;
        uint64_t second_length = 0;
        snprintf(path, sizeof(path), "%s/%s", first->path, names[i]);
        const uint8_t *const a = test_read_file(t, path, &first_length);
        snprintf(path, sizeof(path), "%s/%s", second->path, names[i]);
        const uint8_t *const b = test_read_file(t, path, &second_length);
        REQUIRE(a != nullptr && b != nullptr);
        REQUIRE(first_length == second_length);
        CHECK_MEM_EQ(a, b, first_length);
    }
}

TEST(program_makes_byte_identical_screenshots_from_one_script) {
    temp_dir_t first;
    temp_dir_t second;
    REQUIRE(temp_dir_make(&first));
    REQUIRE(temp_dir_make(&second));
    program_twice(t, &first, &second);
    temp_dir_remove(&first);
    temp_dir_remove(&second);
}

static void program_bad_script(test_context_t *t, const temp_dir_t *dir) {
    const char *const script = test_write_script(t, dir, "# fine\n3 key nosuchkey down\n");
    test_process_t run;
    test_program(t, script, dir->path, "32", "32", &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_HEADLESS);
    CHECK_CONTAINS(run.output, "line 2");
    CHECK_CONTAINS(run.output, "no key called 'nosuchkey'");
}

TEST(program_refuses_a_bad_script_by_its_line_number) {
    temp_dir_t dir;
    REQUIRE(temp_dir_make(&dir));
    program_bad_script(t, &dir);
    temp_dir_remove(&dir);
}

TEST(program_refuses_flags_that_do_not_go_together) {
    test_process_t run;
    char *const lone[] = {t->borrowed_args[0], (char *)"--script", (char *)"x", nullptr};
    test_process_run(t, lone, PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "only mean something with --headless");

    char *const both[] = {t->borrowed_args[0], (char *)"--headless", (char *)"--test-pattern", nullptr};
    test_process_run(t, both, PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "does not go with");

    char *const zero[] = {t->borrowed_args[0], (char *)"--headless", (char *)"--ticks", (char *)"0", nullptr};
    test_process_run(t, zero, PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "whole number above 0");

    char *const nothing[] = {t->borrowed_args[0], (char *)"--headless", nullptr};
    test_process_run(t, nothing, PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_HEADLESS);
    CHECK_CONTAINS(run.output, "how long to run");
}

TEST(program_runs_ticks_alone_at_the_machines_own_frame_size) {
    test_process_t run;
    char *const argv[] = {t->borrowed_args[0], (char *)"--headless", (char *)"--ticks", (char *)"2", nullptr};
    test_process_run(t, argv, PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_CONTAINS(run.output, "2 frames, 0 screenshots");
    CHECK_CONTAINS(run.output, "0 bytes live");
}

TEST_MAIN()
