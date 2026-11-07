/* ctest run_climate_label. See README.md. */
#include "harness/index.h"

#include "climate/label/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* A temperate place on a day: 12 hours of day, no rain to speak of, no storm season. */
static climate_season_input_t label_input(float32_t lat_deg, uint16_t day) {
    return (climate_season_input_t){.lat_deg = lat_deg,
                                    .day_hours = 12.0f32,
                                    .rain_today = 0.0f32,
                                    .rain_mean = 0.0f32,
                                    .hurricane_season = 0.0f32,
                                    .day_of_year = day,
                                    .monsoon_now = 0u,
                                    ._pad0 = {0}};
}

/* The season by brute force from 4.1: the starts, each season's length, and its thirds. south shifts by two seasons. */
static uint8_t label_brute(uint32_t day, bool south) {
    static const uint32_t from[4] = {79u, 170u, 262u, 353u};
    static const uint32_t length[4] = {91u, 92u, 91u, 91u};
    uint32_t season = 3u;
    for (uint32_t s = 0u; s < 3u; s++) {
        if (day >= from[s] && day < from[s + 1u]) {
            season = s;
        }
    }
    const uint32_t into = (day + 365u - from[season]) % 365u;
    const uint32_t third = 3u * into / length[season];
    const uint32_t named = south ? (season + 2u) % 4u : season;
    return (uint8_t)(1u + named * 3u + third);
}

TEST(every_day_of_the_year_has_the_season_the_table_gives) {
    for (uint32_t day = 0u; day < 365u; day++) {
        const climate_season_input_t north = label_input(45.0f32, (uint16_t)day);
        const climate_season_input_t south = label_input(-45.0f32, (uint16_t)day);
        const climate_season_label_t n = climate_season_of(&north);
        const climate_season_label_t s = climate_season_of(&south);
        if (n.season != label_brute(day, false) || s.season != label_brute(day, true) || n.also != 0u || s.also != 0u) {
            printf("day %" PRIu32 ": north %u (want %u), south %u (want %u)\n", day, (uint32_t)n.season,
                   (uint32_t)label_brute(day, false), (uint32_t)s.season, (uint32_t)label_brute(day, true));
            CHECK(false);
            return;
        }
    }
}

typedef struct label_case_t {
    const char *name; /* 0 */
    float32_t lat;    /* 8 */
    uint16_t day;     /* 12 */
    uint8_t want;     /* 14 */
    uint8_t _pad0[1]; /* 15 */
} label_case_t;
static_assert(sizeof(label_case_t) == 16);

static void label_cases(test_context_t *t, const label_case_t *cases, uint32_t count,
                        climate_season_input_t (*make)(const label_case_t *)) {
    for (uint32_t i = 0u; i < count; i++) {
        const climate_season_input_t input = make(&cases[i]);
        const climate_season_label_t label = climate_season_of(&input);
        if (label.season != cases[i].want) {
            printf("%s: season %u, want %u\n", cases[i].name, (uint32_t)label.season, (uint32_t)cases[i].want);
        }
        CHECK_EQ_U64(label.season, cases[i].want);
    }
}

static climate_season_input_t label_plain(const label_case_t *c) {
    return label_input(c->lat, c->day);
}

TEST(the_worked_examples_come_out) {
    static const label_case_t cases[] = {
        {"day 213 north", 45.0f32, 213u, CLIMATE_SEASON_SUMMER_MID, {0}},
        {"day 349 north", 45.0f32, 349u, CLIMATE_SEASON_AUTUMN_LATE, {0}},
        {"day 349 south", -45.0f32, 349u, CLIMATE_SEASON_SPRING_LATE, {0}},
        {"day 10 north", 45.0f32, 10u, CLIMATE_SEASON_WINTER_EARLY, {0}},
        {"day 231 north", 45.0f32, 231u, CLIMATE_SEASON_SUMMER_MID, {0}},
        {"day 232 north", 45.0f32, 232u, CLIMATE_SEASON_SUMMER_LATE, {0}},
    };
    label_cases(t, cases, sizeof(cases) / sizeof(cases[0]), label_plain);
    char text[CLIMATE_SEASON_TEXT_BYTES];
    const climate_season_input_t old = label_input(45.0f32, 349u);
    climate_season_text(climate_season_of(&old), text, sizeof(text));
    CHECK_STR_EQ(text, "Late autumn");
}

/* The phase 10 file's harmonics: P0 = 30 mm a week and P1c = -18, so P(d) = 30 - 18 cos(2 pi d / 365). */
static climate_season_input_t label_tropic(const label_case_t *c) {
    climate_season_input_t input = label_input(c->lat, c->day);
    input.rain_mean = 30.0f32;
    const float64_t theta = 2.0f64 * 3.14159265358979323846f64 * (float64_t)c->day / 365.0f64;
    input.rain_today = (float32_t)(30.0f64 - 18.0f64 * cos(theta));
    return input;
}

TEST(between_the_tropics_it_is_wet_when_rain_is_above_its_mean) {
    static const label_case_t cases[] = {
        {"day 0 at 10 N", 10.0f32, 0u, CLIMATE_SEASON_DRY, {0}},
        {"day 182 at 10 N", 10.0f32, 182u, CLIMATE_SEASON_WET, {0}},
        {"day 182 at 23.5 N exactly", 23.5f32, 182u, CLIMATE_SEASON_WET, {0}},
        {"day 182 at 23.5 S exactly", -23.5f32, 182u, CLIMATE_SEASON_WET, {0}},
        {"day 182 at 23.6 N", 23.6f32, 182u, CLIMATE_SEASON_SUMMER_EARLY, {0}},
        {"day 0 at the equator", 0.0f32, 0u, CLIMATE_SEASON_DRY, {0}},
    };
    label_cases(t, cases, sizeof(cases) / sizeof(cases[0]), label_tropic);
    /* Rain exactly at its mean is not above it. */
    climate_season_input_t even = label_input(5.0f32, 100u);
    even.rain_today = 30.0f32;
    even.rain_mean = 30.0f32;
    CHECK_EQ_U64(climate_season_of(&even).season, CLIMATE_SEASON_DRY);
}

TEST(polar_night_and_midnight_sun_need_the_circle_and_a_whole_day) {
    climate_season_input_t input = label_input(70.0f32, 353u);
    input.day_hours = 0.0f32;
    CHECK_EQ_U64(climate_season_of(&input).season, CLIMATE_SEASON_POLAR_NIGHT);
    input.day_hours = 24.0f32;
    CHECK_EQ_U64(climate_season_of(&input).season, CLIMATE_SEASON_MIDNIGHT_SUN);
    input.day_hours = 12.0f32;
    CHECK_EQ_U64(climate_season_of(&input).season, CLIMATE_SEASON_WINTER_EARLY);
    input.day_hours = 23.9f32;
    CHECK_EQ_U64(climate_season_of(&input).season, CLIMATE_SEASON_WINTER_EARLY);
    /* 66.5 itself is on the circle; 60 is not, whatever it is fed. */
    input.lat_deg = 66.5f32;
    input.day_hours = 0.0f32;
    CHECK_EQ_U64(climate_season_of(&input).season, CLIMATE_SEASON_POLAR_NIGHT);
    input.lat_deg = -70.0f32;
    input.day_hours = 24.0f32;
    CHECK_EQ_U64(climate_season_of(&input).season, CLIMATE_SEASON_MIDNIGHT_SUN);
    input.lat_deg = 60.0f32;
    input.day_hours = 0.0f32;
    CHECK_EQ_U64(climate_season_of(&input).season, CLIMATE_SEASON_WINTER_EARLY);
}

TEST(second_labels_follow_the_cell) {
    climate_season_input_t input = label_input(15.0f32, 240u);
    input.hurricane_season = 0.26f32;
    CHECK_EQ_U64(climate_season_of(&input).also, CLIMATE_SEASON_ALSO_HURRICANE);
    input.hurricane_season = 0.25f32;
    CHECK_EQ_U64(climate_season_of(&input).also, 0u);
    input.monsoon_now = 1u;
    CHECK_EQ_U64(climate_season_of(&input).also, CLIMATE_SEASON_ALSO_MONSOON);
    input.hurricane_season = 1.0f32;
    const climate_season_label_t both = climate_season_of(&input);
    CHECK_EQ_U64(both.also, CLIMATE_SEASON_ALSO_HURRICANE | CLIMATE_SEASON_ALSO_MONSOON);
    char text[CLIMATE_SEASON_TEXT_BYTES];
    climate_season_text(both, text, sizeof(text));
    CHECK_STR_EQ(text, "Dry season \xC2\xB7 Hurricane season \xC2\xB7 Monsoon");
}

TEST(the_text_is_exact_and_never_overruns) {
    char text[CLIMATE_SEASON_TEXT_BYTES];
    const climate_season_label_t mid = {.season = CLIMATE_SEASON_SUMMER_MID,
                                        .also = CLIMATE_SEASON_ALSO_HURRICANE | CLIMATE_SEASON_ALSO_MONSOON};
    const uint32_t length = climate_season_text(mid, text, sizeof(text));
    CHECK_STR_EQ(text, "Mid summer \xC2\xB7 Hurricane season \xC2\xB7 Monsoon");
    CHECK_EQ_U64(length, strlen("Mid summer \xC2\xB7 Hurricane season \xC2\xB7 Monsoon"));
    static const char *const names[CLIMATE_SEASON_COUNT] = {
        "",
        "Early spring",
        "Mid spring",
        "Late spring",
        "Early summer",
        "Mid summer",
        "Late summer",
        "Early autumn",
        "Mid autumn",
        "Late autumn",
        "Early winter",
        "Mid winter",
        "Late winter",
        "Wet season",
        "Dry season",
        "Polar night",
        "Midnight sun",
    };
    /* Every label, every second label, into buffers of every size from 0 to 64, with a guard byte after each. */
    for (uint32_t season = 0u; season <= CLIMATE_SEASON_COUNT; season++) {
        for (uint32_t also = 0u; also <= CLIMATE_SEASON_ALSO_ALL; also++) {
            const climate_season_label_t label = {.season = (uint8_t)season, .also = (uint8_t)also};
            char whole[CLIMATE_SEASON_TEXT_BYTES];
            const uint32_t full = climate_season_text(label, whole, sizeof(whole));
            if (season < CLIMATE_SEASON_COUNT) {
                char want[CLIMATE_SEASON_TEXT_BYTES * 2u];
                snprintf(want, sizeof(want), "%s%s%s", names[season],
                         season != 0u && (also & 1u) != 0u ? " \xC2\xB7 Hurricane season" : "",
                         season != 0u && (also & 2u) != 0u ? " \xC2\xB7 Monsoon" : "");
                if (!CHECK_STR_EQ(whole, want)) {
                    return;
                }
            } else if (!CHECK_STR_EQ(whole, "")) {
                return; /* out of range: nothing */
            }
            for (uint32_t bytes = 0u; bytes <= 64u; bytes++) {
                char buffer[80];
                memset(buffer, 0x5A, sizeof(buffer));
                const uint32_t wrote = climate_season_text(label, buffer, bytes);
                const bool guard = (uint8_t)buffer[bytes] == 0x5Au;
                const bool ended = bytes == 0u || (wrote < bytes && buffer[wrote] == '\0');
                const bool prefix = memcmp(buffer, whole, wrote) == 0 && wrote <= full;
                /* A cut never leaves half of a middle dot behind. */
                const bool whole_char = wrote == 0u || ((uint8_t)buffer[wrote - 1u] & 0xC0u) != 0xC0u;
                if (!guard || !ended || !prefix || !whole_char || (bytes > full && wrote != full)) {
                    printf("season %" PRIu32 " also %" PRIu32 " into %" PRIu32 " bytes: wrote %" PRIu32 "\n", season,
                           also, bytes, wrote);
                    CHECK(false);
                    return;
                }
            }
        }
    }
}

TEST_MAIN()
