/*
 * ctest compile_sound_pack. See README.md. Arguments come from test.cmake:
 *   tool sounds generated_blob work_dir
 * It makes no sound: every sample here stays in memory, or in a file under work_dir.
 */
#include "harness/index.h"
#include "sound_pack/mcsnd/index.h"
#include "sound_pack/synth/index.h"
#include "sound_pack/tree/index.h"
#include "types/sound_blob/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define SPT_PATH_BYTES 1024u
#define SPT_TIMEOUT_MS 30000u
#define SPT_BLOB_BYTES (1ull << 26)
#define SPT_RAW_SAMPLES 4800u

static const char *spt_arg(test_context_t *t, int32_t index) {
    if (index >= t->arg_count) {
        test_check(t, false, __FILE__, __LINE__, "test.cmake gives four arguments");
        return "";
    }
    return t->borrowed_args[index];
}

static char *spt_path(test_context_t *t, const char *first, const char *second) {
    char *path = test_alloc(t, SPT_PATH_BYTES);
    snprintf(path, SPT_PATH_BYTES, "%s/%s", first, second);
    return path;
}

static void spt_write(test_context_t *t, const char *path, const void *bytes, uint64_t length) {
    FILE *file = fopen(path, "wb");
    CHECK(file != nullptr);
    if (file != nullptr) {
        CHECK_EQ_U64(fwrite(bytes, 1u, length, file), length);
        fclose(file);
    }
}

static void spt_write_text(test_context_t *t, const char *path, const char *text) {
    spt_write(t, path, text, strlen(text));
}

/* A whole file on a 64-byte boundary, or nullptr. */
static uint8_t *spt_read(test_context_t *t, const char *path, uint64_t *out_length) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        return nullptr;
    }
    uint8_t *raw = test_alloc(t, SPT_BLOB_BYTES + SOUND_BLOB_ALIGNMENT);
    uint8_t *bytes = raw + (sound_blob_align_up((uintptr_t)raw, SOUND_BLOB_ALIGNMENT) - (uintptr_t)raw);
    *out_length = fread(bytes, 1u, SPT_BLOB_BYTES, file);
    fclose(file);
    return bytes;
}

static void spt_run(test_context_t *t, char *const *argv, test_process_t *run) {
    test_process_run(t, argv, SPT_TIMEOUT_MS, run);
    CHECK(run->started != 0u);
    CHECK(run->timed_out == 0u);
}

static void spt_verify(test_context_t *t, const char *sounds, test_process_t *run) {
    char *const argv[] = {(char *)spt_arg(t, 0), (char *)"verify", (char *)"--sounds", (char *)sounds, nullptr};
    spt_run(t, argv, run);
}

/* A private copy of the committed tree, to break. */
static char *spt_tree(test_context_t *t, const char *name) {
    char *root = spt_path(t, spt_arg(t, 3), name);
    test_process_t run;
    char *const rm[] = {(char *)"rm", (char *)"-rf", root, nullptr};
    spt_run(t, rm, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    char *copy = test_alloc(t, SPT_PATH_BYTES);
    snprintf(copy, SPT_PATH_BYTES, "%s", spt_arg(t, 3));
    for (char *c = copy + 1; *c != '\0'; c++) {
        if (*c == '/') {
            *c = '\0';
            mkdir(copy, 0777);
            *c = '/';
        }
    }
    mkdir(copy, 0777);
    char *const cp[] = {(char *)"cp", (char *)"-r", (char *)spt_arg(t, 1), root, nullptr};
    spt_run(t, cp, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    return root;
}

/* A byte to flip, and the status that must then come back. */
typedef struct spt_damage_t {
    uint32_t offset;  /* 0 */
    uint8_t status;   /* 4: an mcsnd_status_t or a sound_blob_status_t */
    uint8_t _pad0[3]; /* 5 */
} spt_damage_t;
static_assert(sizeof(spt_damage_t) == 8);

static const sound_synth_recipe_t SPT_RECIPE = {
    .seed = 7u,
    .ms = 100u,
    .grains = 10u,
    .grain_ms = 8u,
    .low_hz = 300u,
    .high_hz = 5000u,
    .tone_hz = 200u,
    .tone_ms = 20u,
    .tone_level = 40u,
    .level = 80u,
};

static uint32_t spt_peak(const int16_t *samples, uint32_t count) {
    uint32_t peak = 0;
    for (uint32_t i = 0; i < count; i++) {
        const int32_t value = samples[i];
        const uint32_t size = (uint32_t)(value < 0 ? -value : value);
        peak = size > peak ? size : peak;
    }
    return peak;
}

static int16_t *spt_render(test_context_t *t, const sound_synth_recipe_t *recipe) {
    const uint32_t count = sound_synth_sample_count(recipe);
    int16_t *samples = test_alloc(t, (uint64_t)count * sizeof(int16_t));
    float64_t *work = test_alloc(t, (uint64_t)count * sizeof(float64_t));
    sound_synth_render(recipe, samples, work);
    return samples;
}

TEST(a_recipe_makes_a_sound_of_its_length_and_level_with_silent_ends) {
    CHECK(sound_synth_recipe_problem(&SPT_RECIPE) == nullptr);
    CHECK_EQ_U64(sound_synth_sample_count(&SPT_RECIPE), 4800u);
    const int16_t *samples = spt_render(t, &SPT_RECIPE);
    CHECK_EQ_I64(samples[0], 0);
    CHECK_EQ_I64(samples[4799], 0);
    /* 80% of 32767, by hand: 26213.6. */
    CHECK_EQ_U64(spt_peak(samples, 4800u), 26214u);
}

TEST(the_same_recipe_gives_the_same_samples_and_a_new_seed_does_not) {
    const int16_t *first = spt_render(t, &SPT_RECIPE);
    const int16_t *again = spt_render(t, &SPT_RECIPE);
    CHECK_MEM_EQ(first, again, 4800u * sizeof(int16_t));
    sound_synth_recipe_t other = SPT_RECIPE;
    other.seed = 8u;
    const int16_t *different = spt_render(t, &other);
    CHECK(memcmp(first, different, 4800u * sizeof(int16_t)) != 0);
}

TEST(a_grain_at_the_start_means_the_sound_starts_at_once_and_dies_away) {
    sound_synth_recipe_t recipe = SPT_RECIPE;
    recipe.tone_hz = 0u;
    recipe.tone_ms = 0u;
    recipe.tone_level = 0u;
    recipe.grains = 1u;
    const int16_t *samples = spt_render(t, &recipe);
    /* The first 10 ms hold the peak, and by the last 40 ms a single 8 ms grain has died to a small part of it. */
    CHECK_EQ_U64(spt_peak(samples, 480u), 26214u);
    CHECK(spt_peak(samples + 2880, 1920u) < 26214u / 20u);
}

/* A recipe's every limit, one at a time. */
TEST(a_recipe_out_of_range_says_so) {
    sound_synth_recipe_t r = SPT_RECIPE;
    r.ms = 19u;
    CHECK(sound_synth_recipe_problem(&r) != nullptr);
    r.ms = 2001u;
    CHECK(sound_synth_recipe_problem(&r) != nullptr);
    r = SPT_RECIPE;
    r.grains = 0u;
    CHECK(sound_synth_recipe_problem(&r) != nullptr);
    r = SPT_RECIPE;
    r.grain_ms = 0u;
    CHECK(sound_synth_recipe_problem(&r) != nullptr);
    r = SPT_RECIPE;
    r.low_hz = r.high_hz;
    CHECK(sound_synth_recipe_problem(&r) != nullptr);
    r = SPT_RECIPE;
    r.high_hz = 20001u;
    CHECK(sound_synth_recipe_problem(&r) != nullptr);
    r = SPT_RECIPE;
    r.level = 0u;
    CHECK(sound_synth_recipe_problem(&r) != nullptr);
    r.level = 101u;
    CHECK(sound_synth_recipe_problem(&r) != nullptr);
    r = SPT_RECIPE;
    r.tone_level = 0u; /* a tone at no strength is a mistake */
    CHECK(sound_synth_recipe_problem(&r) != nullptr);
    r = SPT_RECIPE;
    r.tone_ms = 0u;
    CHECK(sound_synth_recipe_problem(&r) != nullptr);
}

TEST(an_mcsnd_round_trips_and_damage_is_refused) {
    const uint8_t samples[8] = {1, 0, 2, 0, 3, 0, 4, 0};
    uint8_t file[sizeof(mcsnd_header_t) + 8u];
    CHECK_EQ_U64(mcsnd_encode(file, sizeof(file), samples, 8u), MCSND_OK);
    mcsnd_view_t view;
    CHECK_EQ_U64(mcsnd_parse(file, sizeof(file), &view), MCSND_OK);
    CHECK_EQ_U64(view.sample_count, 4u);
    CHECK_MEM_EQ(view.borrowed_samples, samples, 8u);

    CHECK_EQ_U64(mcsnd_parse(file, sizeof(mcsnd_header_t) - 1u, &view), MCSND_TRUNCATED);
    CHECK_EQ_U64(mcsnd_parse(file, sizeof(file) - 2u, &view), MCSND_TRUNCATED);
    uint8_t longer[sizeof(file) + 2u] = {0};
    memcpy(longer, file, sizeof(file));
    CHECK_EQ_U64(mcsnd_parse(longer, sizeof(longer), &view), MCSND_TRAILING);
    const spt_damage_t damage[] = {{0u, MCSND_BAD_MAGIC, {0}}, {4u, MCSND_BAD_VERSION, {0}},
                                   {8u, MCSND_BAD_RATE, {0}},  {12u, MCSND_TRUNCATED, {0}},
                                   {16u, MCSND_BAD_HASH, {0}}, {24u, MCSND_BAD_PADDING, {0}},
                                   {32u, MCSND_BAD_HASH, {0}}};
    for (uint32_t i = 0; i < sizeof(damage) / sizeof(damage[0]); i++) {
        file[damage[i].offset] ^= 1u;
        CHECK_EQ_U64(mcsnd_parse(file, sizeof(file), &view), damage[i].status);
        file[damage[i].offset] ^= 1u;
    }
    CHECK_EQ_U64(mcsnd_encode(file, sizeof(file), samples, 7u), MCSND_BAD_SIZE);
    CHECK_EQ_U64(mcsnd_encode(file, sizeof(file), samples, 0u), MCSND_BAD_SIZE);
    CHECK_EQ_U64(mcsnd_encode(file, sizeof(file) - 1u, samples, 8u), MCSND_NO_ROOM);
}

TEST(a_sounds_name_says_its_kind_and_variant) {
    uint8_t kind = 0;
    uint8_t variant = 0;
    CHECK(sound_tree_parse_name("hit_1", &kind, &variant));
    CHECK_EQ_U64(kind, SOUND_KIND_HIT);
    CHECK_EQ_U64(variant, 1u);
    CHECK(sound_tree_parse_name("break_8", &kind, &variant));
    CHECK_EQ_U64(kind, SOUND_KIND_BREAK);
    CHECK_EQ_U64(variant, 8u);
    CHECK(sound_tree_parse_name("place_3", &kind, &variant));
    CHECK_EQ_U64(kind, SOUND_KIND_PLACE);
    const char *const bad[] = {"hit", "hit_", "hit_0", "hit_9", "hit_10", "hit_1x", "smash_1", ""};
    for (uint32_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(!sound_tree_parse_name(bad[i], &kind, &variant));
    }
}

/* ---- The committed tree and the blob the build made -------------------------------------------------------------- */

TEST(the_committed_tree_is_whole_and_every_sound_in_it_is_a_recording) {
    test_process_t run;
    spt_verify(t, spt_arg(t, 1), &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_CONTAINS(run.output, "90 sounds, 90 recorded, 0 made from recipes");
}

/* Sign changes per second over the loud part: rough, but it can't be fooled by loudness. */
static uint64_t spt_family_crossings(const sound_blob_view_t *view, uint32_t family) {
    uint64_t crossings = 0;
    uint64_t samples_seen = 0;
    for (uint32_t i = 0; i < view->sound_count; i++) {
        const sound_blob_entry_t *entry = &view->borrowed_entries[i];
        if (entry->family != family || entry->kind != SOUND_KIND_BREAK) {
            continue;
        }
        const int16_t *samples = sound_blob_samples(view, entry);
        for (uint32_t s = 1; s < entry->sample_count; s++) {
            crossings += (samples[s - 1u] < 0) != (samples[s] < 0) ? 1u : 0u;
        }
        samples_seen += entry->sample_count;
    }
    return crossings * SOUND_BLOB_SAMPLE_RATE / samples_seen;
}

TEST(the_generated_blob_has_every_kind_of_every_family_and_they_differ) {
    uint64_t length = 0;
    const uint8_t *blob = spt_read(t, spt_arg(t, 2), &length);
    REQUIRE(blob != nullptr);
    sound_blob_view_t view;
    REQUIRE(sound_blob_check(blob, length, &view) == SOUND_BLOB_OK);
    CHECK_EQ_U64(view.sound_count, 90u);
    for (uint32_t family = 1; family < SOUND_FAMILY_COUNT; family++) {
        for (uint32_t kind = 0; kind < SOUND_KIND_COUNT; kind++) {
            uint32_t first = 0;
            const uint32_t count = sound_blob_find(&view, family, kind, &first);
            CHECK_EQ_U64(count, 3u);
            for (uint32_t v = 0; v < count; v++) {
                const sound_blob_entry_t *entry = &view.borrowed_entries[first + v];
                const int16_t *samples = sound_blob_samples(&view, entry);
                CHECK_EQ_U64(entry->variant, v + 1u);
                CHECK_EQ_U64(spt_peak(samples, entry->sample_count), entry->peak);
                CHECK(entry->peak > 8000u);
                CHECK_EQ_I64(samples[0], 0);
                CHECK_EQ_I64(samples[entry->sample_count - 1u], 0);
                /* Short: hits up to 170 ms, breaks up to 700, places up to 280. */
                const uint32_t limit = kind == SOUND_KIND_HIT ? 8160u : kind == SOUND_KIND_BREAK ? 33600u : 13440u;
                CHECK(entry->sample_count <= limit);
                /* Variants must differ, or picking at random does nothing. */
                if (v > 0u) {
                    const sound_blob_entry_t *before = &view.borrowed_entries[first + v - 1u];
                    const uint32_t shorter =
                        before->sample_count < entry->sample_count ? before->sample_count : entry->sample_count;
                    CHECK(memcmp(sound_blob_samples(&view, before), samples, shorter * sizeof(int16_t)) != 0);
                }
            }
        }
    }
    /* A break is fuller than a hit: longer, and louder. */
    uint32_t hit = 0;
    uint32_t smash = 0;
    (void)sound_blob_find(&view, SOUND_FAMILY_STONE, SOUND_KIND_HIT, &hit);
    (void)sound_blob_find(&view, SOUND_FAMILY_STONE, SOUND_KIND_BREAK, &smash);
    CHECK(view.borrowed_entries[smash].sample_count > view.borrowed_entries[hit].sample_count);
    CHECK(view.borrowed_entries[smash].peak > view.borrowed_entries[hit].peak);
    /* What each family was chosen to be: the rustle and hiss sit far above the dull ones. */
    const uint64_t dirt = spt_family_crossings(&view, SOUND_FAMILY_DIRT);
    const uint64_t wood = spt_family_crossings(&view, SOUND_FAMILY_WOOD);
    const uint64_t stone = spt_family_crossings(&view, SOUND_FAMILY_STONE);
    const uint64_t sand = spt_family_crossings(&view, SOUND_FAMILY_SAND);
    const uint64_t leaves = spt_family_crossings(&view, SOUND_FAMILY_LEAVES);
    const uint64_t glass = spt_family_crossings(&view, SOUND_FAMILY_GLASS);
    const uint64_t snow = spt_family_crossings(&view, SOUND_FAMILY_SNOW);
    const uint64_t gravel = spt_family_crossings(&view, SOUND_FAMILY_GRAVEL);
    printf("crossings per second: dirt %" PRIu64 " wood %" PRIu64 " stone %" PRIu64 " sand %" PRIu64 " leaves %" PRIu64
           " glass %" PRIu64 " snow %" PRIu64 " gravel %" PRIu64 "\n",
           dirt, wood, stone, sand, leaves, glass, snow, gravel);
    CHECK(dirt < stone);
    CHECK(wood < stone);
    CHECK(dirt < sand);
    CHECK(stone < leaves);
    CHECK(dirt < glass);
    CHECK(snow < gravel);
}

TEST(the_same_tree_gives_the_same_blob_and_a_rerun_leaves_it_alone) {
    char *out = spt_path(t, spt_arg(t, 3), "again.mcsb");
    char *tree = spt_tree(t, "repeat");
    char *const argv[] = {
        (char *)spt_arg(t, 0), (char *)"blob", (char *)"--sounds", tree, (char *)"--out", out, nullptr};
    test_process_t run;
    spt_run(t, argv, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_STR_EQ(run.output, "");
    uint64_t length = 0;
    uint64_t built_length = 0;
    const uint8_t *blob = spt_read(t, out, &length);
    const uint8_t *built = spt_read(t, spt_arg(t, 2), &built_length);
    REQUIRE(blob != nullptr);
    REQUIRE(built != nullptr);
    CHECK_EQ_U64(length, built_length);
    REQUIRE(length == built_length);
    CHECK_MEM_EQ(blob, built, length);
}

TEST(damage_to_a_blob_is_refused) {
    uint64_t length = 0;
    uint8_t *blob = spt_read(t, spt_arg(t, 2), &length);
    REQUIRE(blob != nullptr);
    sound_blob_view_t view;
    REQUIRE(sound_blob_check(blob, length, &view) == SOUND_BLOB_OK);
    CHECK_EQ_U64(sound_blob_check(blob + 1, length - 1u, &view), SOUND_BLOB_BAD_ALIGNMENT);
    CHECK_EQ_U64(sound_blob_check(blob, 10u, &view), SOUND_BLOB_TRUNCATED);
    CHECK_EQ_U64(sound_blob_check(blob, length - 2u, &view), SOUND_BLOB_TRUNCATED);
    const spt_damage_t damage[] = {
        {0u, SOUND_BLOB_BAD_MAGIC, {0}},   {4u, SOUND_BLOB_BAD_VERSION, {0}}, {8u, SOUND_BLOB_BAD_LAYOUT, {0}},
        {12u, SOUND_BLOB_BAD_LAYOUT, {0}}, {16u, SOUND_BLOB_TRUNCATED, {0}},  {24u, SOUND_BLOB_BAD_HASH, {0}},
        {32u, SOUND_BLOB_BAD_LAYOUT, {0}}, {40u, SOUND_BLOB_BAD_LAYOUT, {0}}, {48u, SOUND_BLOB_BAD_LAYOUT, {0}},
        {56u, SOUND_BLOB_BAD_LAYOUT, {0}}, {64u, SOUND_BLOB_BAD_HASH, {0}},
    };
    for (uint32_t i = 0; i < sizeof(damage) / sizeof(damage[0]); i++) {
        blob[damage[i].offset] ^= 1u;
        CHECK_EQ_U64(sound_blob_check(blob, length, &view), damage[i].status);
        blob[damage[i].offset] ^= 1u;
    }
    /* Past the hash: an entry that lies, with its hash made right, as nothing else would catch it. */
    sound_blob_header_t *header = (sound_blob_header_t *)(void *)blob;
    sound_blob_entry_t *entries = (sound_blob_entry_t *)(void *)(blob + header->entries_offset);
    const sound_blob_entry_t saved = entries[1];
    const uint64_t saved_hash = header->payload_hash;
    entries[1].family = entries[0].family;
    entries[1].kind = entries[0].kind;
    entries[1].variant = entries[0].variant;
    header->payload_hash = sound_blob_hash(blob + sizeof(*header), length - sizeof(*header));
    CHECK_EQ_U64(sound_blob_check(blob, length, &view), SOUND_BLOB_BAD_ENTRY);
    entries[1] = saved;
    entries[1].samples_offset = header->samples_bytes;
    header->payload_hash = sound_blob_hash(blob + sizeof(*header), length - sizeof(*header));
    CHECK_EQ_U64(sound_blob_check(blob, length, &view), SOUND_BLOB_BAD_ENTRY);
    entries[1] = saved;
    header->payload_hash = saved_hash;
    CHECK_EQ_U64(sound_blob_check(blob, length, &view), SOUND_BLOB_OK);
}

/* ---- Refusals ----------------------------------------------------------------------------------------------------- */

/* A small recipe of ours to replace one sound's file with, and what the tool must say about it. */
#define SPT_GOOD_SOURCE "source: { kind: synth }\n"
#define SPT_GOOD_RECIPE                                                                                                \
    "recipe: { seed: 1, ms: 80, grains: 3, grain_ms: 5, low_hz: 200, high_hz: 3000, tone_hz: 0, overtone_hz: 0, "      \
    "tone_ms: 0, tone_level: 0, level: 60 }\n"
#define SPT_GOOD_NAME "name: hit_1\nfamily: stone\n"

typedef struct spt_bad_file_t {
    const char *text;    /* 0 */
    const char *message; /* 8 */
} spt_bad_file_t;
static_assert(sizeof(spt_bad_file_t) == 16);

/* Turns one recorded sound of a copied tree into a recipe, which is the fallback for a material with no recording. */
static void spt_use_recipe(test_context_t *t, char *tree, const char *sound) {
    char relative[SPT_PATH_BYTES];
    snprintf(relative, sizeof relative, "%s/index.mcsnd", sound);
    test_process_t run;
    char *const rm[] = {(char *)"rm", (char *)"-f", spt_path(t, tree, relative), nullptr};
    spt_run(t, rm, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    snprintf(relative, sizeof relative, "%s/index.yaml", sound);
    spt_write_text(t, spt_path(t, tree, relative), SPT_GOOD_NAME SPT_GOOD_SOURCE SPT_GOOD_RECIPE);
}

static const spt_bad_file_t SPT_BAD_FILES[] = {
    {SPT_GOOD_NAME SPT_GOOD_SOURCE SPT_GOOD_RECIPE "colour: red\n", "unknown field 'colour' in a sound"},
    {"family: stone\n" SPT_GOOD_SOURCE SPT_GOOD_RECIPE, "missing field 'name'"},
    {"name: hit_1\n" SPT_GOOD_SOURCE SPT_GOOD_RECIPE, "missing field 'family'"},
    {"name: hit_2\nfamily: stone\n" SPT_GOOD_SOURCE SPT_GOOD_RECIPE, "name 'hit_2' is not its directory's"},
    {"name: hit_1\nfamily: wood\n" SPT_GOOD_SOURCE SPT_GOOD_RECIPE, "family 'wood' is not its directory's"},
    {SPT_GOOD_NAME SPT_GOOD_RECIPE, "needs a mapping 'source'"},
    {SPT_GOOD_NAME "source: { kind: magic }\n" SPT_GOOD_RECIPE, "source kind 'magic'"},
    {SPT_GOOD_NAME "source: { flavour: synth }\n" SPT_GOOD_RECIPE, "source needs a 'kind'"},
    {SPT_GOOD_NAME SPT_GOOD_SOURCE, "needs a 'recipe'"},
    {SPT_GOOD_NAME SPT_GOOD_SOURCE "recipe: { seed: 1 }\n", "missing field 'ms' in recipe"},
    {SPT_GOOD_NAME SPT_GOOD_SOURCE
     "recipe: { seed: 1, ms: 5, grains: 3, grain_ms: 5, low_hz: 200, high_hz: 3000, tone_hz: 0, overtone_hz: 0, "
     "tone_ms: 0, tone_level: 0, level: 60 }\n",
     "ms must be 20 to 2000"},
    {SPT_GOOD_NAME SPT_GOOD_SOURCE
     "recipe: { seed: -1, ms: 80, grains: 3, grain_ms: 5, low_hz: 200, high_hz: 3000, tone_hz: 0, overtone_hz: 0, "
     "tone_ms: 0, tone_level: 0, level: 60 }\n",
     "must not be negative"},
    {SPT_GOOD_NAME "source: { kind: recorded, origin: Somebody, file: a.ogg, url: x, licence: CC0-1.0 }\n"
                   "recipe: { seed: 1 }\n",
     "a recording has no recipe"},
    {SPT_GOOD_NAME "source: { kind: recorded, file: a.ogg, url: x, licence: CC0-1.0 }\n", "needs 'origin'"},
    {SPT_GOOD_NAME "source: { kind: recorded, origin: Somebody, url: x, licence: CC0-1.0 }\n", "needs 'file'"},
    {SPT_GOOD_NAME "source: { kind: recorded, origin: Somebody, file: a.ogg, licence: CC0-1.0 }\n", "needs 'url'"},
    {SPT_GOOD_NAME "source: { kind: recorded, origin: Somebody, file: a.ogg, url: x }\n", "needs 'licence'"},
    {SPT_GOOD_NAME "source: { kind: recorded, origin: Somebody, file: a.ogg, url: x, licence: CC-BY-SA-4.0 }\n",
     "licence 'CC-BY-SA-4.0' is not CC0-1.0"},
    {SPT_GOOD_NAME "source: { kind: recorded, origin: Somebody, file: a.ogg, url: x, licence: CC0-1.0 }\n",
     "origin 'Somebody' is not credited"},
    {SPT_GOOD_NAME "source: { kind: recorded, origin: Somebody, file: a.ogg, url: x, licence: CC0-1.0 }\n",
     "a recording needs this file"},
    {"- just\n- a list\n", "must be a mapping"},
    {"name: [\n", ""},
};
enum { SPT_BAD_FILE_COUNT = sizeof(SPT_BAD_FILES) / sizeof(SPT_BAD_FILES[0]) };

TEST(each_mistake_in_a_sounds_file_is_refused) {
    char *tree = spt_tree(t, "bad_file");
    spt_use_recipe(t, tree, "stone/hit_1");
    char *target = spt_path(t, tree, "stone/hit_1/index.yaml");
    char *good = test_alloc(t, 4096u);
    FILE *file = fopen(target, "rb");
    REQUIRE(file != nullptr);
    const uint64_t good_length = fread(good, 1u, 4095u, file);
    fclose(file);
    test_process_t run;
    for (uint32_t i = 0; i < SPT_BAD_FILE_COUNT; i++) {
        spt_write_text(t, target, SPT_BAD_FILES[i].text);
        spt_verify(t, tree, &run);
        if (run.exit_code != 1 || strstr(run.output, SPT_BAD_FILES[i].message) == nullptr) {
            fprintf(stderr, "case %u: wanted '%s', got exit %d:\n%s\n", i, SPT_BAD_FILES[i].message, run.exit_code,
                    run.output);
        }
        CHECK_EQ_I64(run.exit_code, 1);
        CHECK_CONTAINS(run.output, SPT_BAD_FILES[i].message);
    }
    /* And the same tree with the file put back is whole, so it was that file each time. */
    spt_write(t, target, good, good_length);
    spt_verify(t, tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
}

static void spt_expect_refused(test_context_t *t, const char *tree, const char *message) {
    test_process_t run;
    spt_verify(t, tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, message);
}

static void spt_remove(test_context_t *t, char *path) {
    test_process_t run;
    char *const rm[] = {(char *)"rm", (char *)"-rf", path, nullptr};
    spt_run(t, rm, &run);
    CHECK_EQ_I64(run.exit_code, 0);
}

TEST(a_family_cannot_go_silent) {
    /* The whole family gone, then only one kind of it, then a gap in its variants. */
    char *tree = spt_tree(t, "no_family");
    spt_remove(t, spt_path(t, tree, "snow"));
    spt_expect_refused(t, tree, "family 'snow' has no directory");

    tree = spt_tree(t, "no_kind");
    spt_remove(t, spt_path(t, tree, "dirt/place_1"));
    spt_remove(t, spt_path(t, tree, "dirt/place_2"));
    spt_remove(t, spt_path(t, tree, "dirt/place_3"));
    spt_expect_refused(t, tree, "has no place sound");

    tree = spt_tree(t, "gap");
    spt_remove(t, spt_path(t, tree, "dirt/hit_2"));
    spt_expect_refused(t, tree, "its variants have a gap");
}

TEST(strangers_in_the_tree_are_refused) {
    char *tree = spt_tree(t, "stranger_family");
    mkdir(spt_path(t, tree, "cheese"), 0777);
    spt_expect_refused(t, tree, "not a sound family");

    tree = spt_tree(t, "stranger_sound");
    mkdir(spt_path(t, tree, "stone/smash_1"), 0777);
    spt_expect_refused(t, tree, "a sound's directory is");
}

TEST(what_a_sound_owes_must_be_there) {
    char *tree = spt_tree(t, "no_readme");
    spt_remove(t, spt_path(t, tree, "stone/hit_1/README.md"));
    spt_expect_refused(t, tree, "must say where it came from");

    tree = spt_tree(t, "no_attribution");
    spt_remove(t, spt_path(t, tree, "ATTRIBUTION.md"));
    spt_expect_refused(t, tree, "ATTRIBUTION.md: missing");

    tree = spt_tree(t, "stale");
    spt_use_recipe(t, tree, "stone/hit_1");
    spt_write_text(t, spt_path(t, tree, "stone/hit_1/index.mcsnd"), "x");
    spt_expect_refused(t, tree, "must have no recording beside it");
}

/* ---- Import ------------------------------------------------------------------------------------------------------- */

static void spt_import(test_context_t *t, char *tree, char *raw, const char *family, const char *name,
                       const char *licence, const char *origin, test_process_t *run) {
    char *const argv[] = {(char *)spt_arg(t, 0),
                          (char *)"import",
                          (char *)"--sounds",
                          tree,
                          (char *)"--raw",
                          raw,
                          (char *)"--family",
                          (char *)family,
                          (char *)"--name",
                          (char *)name,
                          (char *)"--origin",
                          (char *)origin,
                          (char *)"--file",
                          (char *)"Audio/a.ogg",
                          (char *)"--url",
                          (char *)"https://example.invalid/pack",
                          (char *)"--licence",
                          (char *)licence,
                          (char *)"--changes",
                          (char *)"cut to 100 ms",
                          nullptr};
    spt_run(t, argv, run);
}

TEST(a_recording_comes_in_only_with_its_licence_and_credit) {
    char *tree = spt_tree(t, "import");
    spt_use_recipe(t, tree, "stone/hit_1");
    char *raw = spt_path(t, tree, "a.raw");
    int16_t *samples = test_alloc(t, SPT_RAW_SAMPLES * sizeof(int16_t));
    for (uint32_t i = 0; i < SPT_RAW_SAMPLES; i++) {
        samples[i] = (int16_t)(i % 200u) - 100;
    }
    spt_write(t, raw, samples, SPT_RAW_SAMPLES * sizeof(int16_t));
    const char *const mcsnd = "stone/hit_1/index.mcsnd";
    struct stat info;

    /* With no recording the recipe stands in, and the tree is still whole. */
    test_process_t run;
    spt_verify(t, tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_CONTAINS(run.output, "90 sounds, 89 recorded, 1 made from recipes");

    /* Each of these changes nothing. */
    spt_import(t, tree, raw, "stone", "hit_1", "CC-BY-SA-4.0", "Some Pack", &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "licence is not CC0-1.0");
    spt_import(t, tree, raw, "cheese", "hit_1", "CC0-1.0", "Some Pack", &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "not one of the sound families");
    spt_import(t, tree, raw, "stone", "smash_1", "CC0-1.0", "Some Pack", &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "is not <hit|break|place>");
    spt_import(t, tree, raw, "stone", "hit_1", "CC0-1.0", "Some \"Pack\"", &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "plain text");
    char *odd = spt_path(t, tree, "odd.raw");
    spt_write(t, odd, samples, 3u);
    spt_import(t, tree, odd, "stone", "hit_1", "CC0-1.0", "Some Pack", &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "an odd byte");
    CHECK(stat(spt_path(t, tree, mcsnd), &info) != 0);

    /* A good one goes in, and then verify wants the credit it hasn't been given yet. */
    spt_import(t, tree, raw, "stone", "hit_1", "CC0-1.0", "Some Pack", &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK(stat(spt_path(t, tree, mcsnd), &info) == 0);
    spt_expect_refused(t, tree, "origin 'Some Pack' is not credited");

    uint64_t credits_length = 0;
    const char *credits = (const char *)spt_read(t, spt_path(t, tree, "ATTRIBUTION.md"), &credits_length);
    REQUIRE(credits != nullptr);
    char *credited = test_alloc(t, credits_length + SPT_PATH_BYTES);
    snprintf(credited, credits_length + SPT_PATH_BYTES, "%.*s| Some Pack | Somebody | CC0-1.0 | somewhere |\n",
             (int32_t)credits_length, credits);
    spt_write_text(t, spt_path(t, tree, "ATTRIBUTION.md"), credited);
    spt_verify(t, tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_CONTAINS(run.output, "90 sounds, 90 recorded, 0 made from recipes");

    /* Its README says what was done to it. */
    uint64_t readme_length = 0;
    const char *readme = (const char *)spt_read(t, spt_path(t, tree, "stone/hit_1/README.md"), &readme_length);
    REQUIRE(readme != nullptr);
    CHECK_CONTAINS(readme, "cut to 100 ms");

    /* The recording's samples are what ends up in the blob, to the byte. */
    char *out = spt_path(t, tree, "out.mcsb");
    char *const blob_argv[] = {
        (char *)spt_arg(t, 0), (char *)"blob", (char *)"--sounds", tree, (char *)"--out", out, nullptr};
    spt_run(t, blob_argv, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    uint64_t length = 0;
    const uint8_t *blob = spt_read(t, out, &length);
    REQUIRE(blob != nullptr);
    sound_blob_view_t view;
    REQUIRE(sound_blob_check(blob, length, &view) == SOUND_BLOB_OK);
    uint32_t first = 0;
    CHECK_EQ_U64(sound_blob_find(&view, SOUND_FAMILY_STONE, SOUND_KIND_HIT, &first), 3u);
    const sound_blob_entry_t *entry = &view.borrowed_entries[first];
    CHECK_EQ_U64(entry->sample_count, SPT_RAW_SAMPLES);
    CHECK_EQ_U64(entry->peak, 100u);
    CHECK_MEM_EQ(sound_blob_samples(&view, entry), samples, SPT_RAW_SAMPLES * sizeof(int16_t));

    /* Damage to the recording is caught on the next read. */
    spt_write_text(t, spt_path(t, tree, mcsnd), "MCSN");
    spt_expect_refused(t, tree, "stone/hit_1/index.mcsnd");
}

TEST(a_bad_command_line_is_status_2) {
    test_process_t run;
    char *const none[] = {(char *)spt_arg(t, 0), nullptr};
    spt_run(t, none, &run);
    CHECK_EQ_I64(run.exit_code, 2);
    char *const strange[] = {(char *)spt_arg(t, 0), (char *)"play", (char *)"--sounds", (char *)"x", nullptr};
    spt_run(t, strange, &run);
    CHECK_EQ_I64(run.exit_code, 2);
    char *const no_out[] = {(char *)spt_arg(t, 0), (char *)"blob", (char *)"--sounds", (char *)"x", nullptr};
    spt_run(t, no_out, &run);
    CHECK_EQ_I64(run.exit_code, 2);
}

TEST_MAIN()
