/* ctest run_texture. See README.md. Arguments come from test.cmake: textures_dir blob_file vectors_dir. */
#include "harness/index.h"

#include "heap/index.h"
#include "simd/embed_assets/index.h"
#include "tables/blocks/index.h"
#include "texture/index.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum : uint64_t {
    TEXTURE_TEST_PATH_BYTES = 4096u,
    TEXTURE_TEST_MCTEX_HEADER_BYTES = 32u,
    TEXTURE_TEST_MCVEC_HEADER_BYTES = 64u,
};

static const char *texture_test_format(test_context_t *t, const char *format, ...) {
    char *text = test_alloc(t, TEXTURE_TEST_PATH_BYTES);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, TEXTURE_TEST_PATH_BYTES, format, arguments);
    va_end(arguments);
    return text;
}

/* The whole file, or nullptr with a recorded failure. */
static const uint8_t *texture_test_read(test_context_t *t, const char *path, uint64_t *out_length) {
    const int32_t fd = open(path, O_RDONLY | O_CLOEXEC);
    if (!CHECK(fd >= 0)) {
        return nullptr;
    }
    struct stat info;
    CHECK_EQ_I64(fstat(fd, &info), 0);
    const uint64_t length = (uint64_t)info.st_size;
    uint8_t *bytes = test_alloc(t, length + 1u);
    uint64_t got = 0;
    while (got < length) {
        const int64_t step = read(fd, bytes + got, length - got);
        if (!CHECK(step > 0)) {
            break;
        }
        got += (uint64_t)step;
    }
    close(fd);
    *out_length = got;
    return bytes;
}

/* A writable copy of the embedded blob, on the boundary its reader wants. */
static uint8_t *texture_test_copy(test_context_t *t) {
    uint8_t *raw = test_alloc(t, embed_assets_texture_blob_bytes + TEXTURE_BLOB_ALIGNMENT * 2u);
    uint8_t *aligned = (uint8_t *)texture_blob_align_up((uintptr_t)raw, TEXTURE_BLOB_ALIGNMENT);
    memcpy(aligned, embed_assets_texture_blob, embed_assets_texture_blob_bytes);
    return aligned;
}

static void texture_test_mend_hash(uint8_t *blob, uint64_t length) {
    texture_blob_header_t *header = (texture_blob_header_t *)(void *)blob;
    header->payload_hash = texture_blob_hash(blob + sizeof(*header), length - sizeof(*header));
}

/* ---- The embedding ----------------------------------------------------------------------------------------------- */

TEST(the_embedded_blob_is_the_one_the_build_made) {
    REQUIRE(t->arg_count == 3);
    uint64_t length = 0;
    const uint8_t *file = texture_test_read(t, t->borrowed_args[1], &length);
    REQUIRE(file != nullptr);
    CHECK_EQ_U64(embed_assets_texture_blob_bytes, length);
    REQUIRE(embed_assets_texture_blob_bytes == length);
    CHECK_MEM_EQ(embed_assets_texture_blob, file, length);
    CHECK_EQ_U64((uintptr_t)embed_assets_texture_blob % TEXTURE_BLOB_ALIGNMENT, 0u);
}

TEST(the_embedded_blob_passes_its_own_checks) {
    texture_blob_view_t view;
    CHECK_EQ_U64(texture_blob_parse(embed_assets_texture_blob, embed_assets_texture_blob_bytes, &view),
                 TEXTURE_BLOB_OK);
    REQUIRE(view.borrowed_header != nullptr);
    CHECK_EQ_U64((uintptr_t)view.borrowed_pixels % TEXTURE_BLOB_ALIGNMENT, 0u);
    CHECK_EQ_U64((uintptr_t)view.borrowed_vectors % TEXTURE_BLOB_TEXTURE_ALIGNMENT, 0u);
}

TEST(hash_is_fnv1a_64) {
    /* The published test vectors of FNV-1a 64. */
    CHECK_EQ_U64(texture_blob_hash((const uint8_t *)"", 0u), 0xCBF29CE484222325ull);
    CHECK_EQ_U64(texture_blob_hash((const uint8_t *)"a", 1u), 0xAF63DC4C8601EC8Cull);
    CHECK_EQ_U64(texture_blob_hash((const uint8_t *)"foobar", 6u), 0x85944171F73967E8ull);
}

/* ---- Lookup ------------------------------------------------------------------------------------------------------ */

/* What the source tree holds, counted here and not by the tool. */
static uint32_t texture_test_count_directories(test_context_t *t, const char *textures) {
    const char *const groups[] = {"blocks", "items", "mobs", "ui"};
    uint32_t count = 0;
    for (uint32_t g = 0; g < 4u; g++) {
        const char *path = texture_test_format(t, "%s/%s", textures, groups[g]);
        DIR *listing = opendir(path);
        if (!CHECK(listing != nullptr)) {
            continue;
        }
        for (struct dirent *item = readdir(listing); item != nullptr; item = readdir(listing)) {
            struct stat info;
            if (item->d_name[0] != '.' && stat(texture_test_format(t, "%s/%s", path, item->d_name), &info) == 0 &&
                S_ISDIR(info.st_mode)) {
                count++;
            }
        }
        closedir(listing);
    }
    return count;
}

TEST(every_texture_is_found_under_its_own_name) {
    REQUIRE(t->arg_count == 3);
    texture_t texture;
    REQUIRE(texture_init(&texture));
    const uint32_t count = texture_count(&texture);
    CHECK_EQ_U64(count, texture_test_count_directories(t, t->borrowed_args[0]));
    CHECK(count > 500u); /* 511 when this was written: not merely equal, but not gone either */
    const char *previous = "";
    for (uint32_t id = 0; id < count; id++) {
        texture_info_t info;
        REQUIRE(texture_info_get(&texture, id, &info));
        CHECK(strcmp(previous, info.borrowed_name) < 0);
        previous = info.borrowed_name;
        uint32_t found = UINT32_MAX;
        CHECK(texture_find(&texture, info.borrowed_name, &found));
        CHECK_EQ_U64(found, id);
    }
    texture_deinit(&texture);
}

TEST(what_the_game_is_known_to_need_is_there) {
    texture_t texture;
    REQUIRE(texture_init(&texture));
    const char *const names[] = {"blocks/dirt", "blocks/oak_leaves", "blocks/lava_still",
                                 "items/wheat", "mobs/zombie",       "mobs/creeper",
                                 "ui/logo"};
    for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        uint32_t id = 0;
        CHECK(texture_find(&texture, names[i], &id));
    }
    texture_deinit(&texture);
}

/* The block tables name faces by the texture's name, and a renderer looks them up here: both ends have to agree. */
TEST(every_texture_a_block_names_is_found_in_the_blob) {
    texture_t texture;
    REQUIRE(texture_init(&texture));
    for (uint32_t i = 0; i < BLOCK_TEXTURE_COUNT; i++) {
        uint32_t id = 0;
        if (!texture_find(&texture, block_texture_names[i], &id)) {
            fprintf(stderr, "not in the blob: %s\n", block_texture_names[i]);
            CHECK(false);
        }
    }
    texture_deinit(&texture);
}

TEST(near_misses_find_nothing) {
    texture_t texture;
    REQUIRE(texture_init(&texture));
    const char *const names[] = {"",
                                 "blocks",
                                 "blocks/",
                                 "blocks/dir",
                                 "blocks/dirt2",
                                 "blocks/Dirt",
                                 "BLOCKS/dirt",
                                 "/blocks/dirt",
                                 "blocks/dirt/",
                                 "items/dirt",
                                 "dirt",
                                 "blocks//dirt",
                                 "blocks/dirt ",
                                 "no/such/texture"};
    for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        uint32_t id = 0x12345678u;
        CHECK(!texture_find(&texture, names[i], &id));
        CHECK_EQ_U64(id, 0x12345678u); /* it writes only when it finds */
    }
    texture_deinit(&texture);
}

TEST(a_very_long_name_finds_nothing_and_stays_in_bounds) {
    texture_t texture;
    REQUIRE(texture_init(&texture));
    char *name = test_alloc(t, 70000u);
    memset(name, 'a', 69999u);
    uint32_t id = 0;
    CHECK(!texture_find(&texture, name, &id));
    texture_deinit(&texture);
}

/* ---- Every texture, against its source files ---------------------------------------------------------------------- */

static void texture_test_check_alpha(test_context_t *t, const texture_blob_entry_t *entry, const texture_mip_t *mip) {
    for (uint64_t i = 0; i < mip->bytes / 4u; i++) {
        const uint8_t alpha = mip->borrowed_pixels[i * 4u + 3u];
        if (entry->alpha_class == TEXTURE_BLOB_ALPHA_OPAQUE) {
            CHECK_EQ_U64(alpha, 255u);
        } else if (entry->alpha_class == TEXTURE_BLOB_ALPHA_CUTOUT) {
            CHECK(alpha == 0u || alpha == 255u);
        }
    }
}

/* Level 0 keeps each colour byte, and its alpha is the source's under the class's rule. */
static void texture_test_check_level_zero(test_context_t *t, const texture_blob_entry_t *entry,
                                          const texture_mip_t *mip, const uint8_t *source) {
    for (uint64_t i = 0; i < mip->bytes / 4u; i++) {
        CHECK_MEM_EQ(mip->borrowed_pixels + i * 4u, source + i * 4u, 3u);
        const uint8_t from = source[i * 4u + 3u];
        uint8_t want = from;
        if (entry->alpha_class == TEXTURE_BLOB_ALPHA_OPAQUE) {
            want = 255u;
        } else if (entry->alpha_class == TEXTURE_BLOB_ALPHA_CUTOUT) {
            want = from >= TEXTURE_BLOB_CUTOUT_THRESHOLD ? 255u : 0u;
        }
        CHECK_EQ_U64(mip->borrowed_pixels[i * 4u + 3u], want);
    }
}

static void texture_test_check_one(test_context_t *t, const texture_t *texture, uint32_t id) {
    texture_info_t info;
    REQUIRE(texture_info_get(texture, id, &info));
    const texture_blob_entry_t *entry = info.borrowed_entry;
    uint64_t mctex_length = 0;
    const uint8_t *mctex = texture_test_read(
        t, texture_test_format(t, "%s/%s/index.mctex", t->borrowed_args[0], info.borrowed_name), &mctex_length);
    REQUIRE(mctex != nullptr);
    uint32_t file_width = 0;
    uint32_t file_height = 0;
    uint32_t file_frames = 0;
    memcpy(&file_width, mctex + 8u, 4u);
    memcpy(&file_height, mctex + 12u, 4u);
    memcpy(&file_frames, mctex + 16u, 4u);
    CHECK_EQ_U64(entry->width, file_width);
    CHECK_EQ_U64(entry->height, file_height);
    CHECK_EQ_U64(entry->frame_count, file_frames);

    uint64_t expected_width = file_width;
    uint64_t expected_height = file_height;
    for (uint32_t level = 0; level < entry->level_count; level++) {
        for (uint32_t frame = 0; frame < entry->frame_count; frame++) {
            texture_mip_t mip;
            REQUIRE(texture_mip_get(texture, id, level, frame, &mip));
            CHECK_EQ_U64(mip.width, expected_width);
            CHECK_EQ_U64(mip.height, expected_height);
            CHECK_EQ_U64(mip.bytes, expected_width * expected_height * 4u);
            CHECK_EQ_U64((uintptr_t)mip.borrowed_pixels % 4u, 0u);
            texture_test_check_alpha(t, entry, &mip);
            if (level == 0u) {
                texture_test_check_level_zero(t, entry, &mip,
                                              mctex + TEXTURE_TEST_MCTEX_HEADER_BYTES + mip.bytes * frame);
            }
        }
        expected_width = expected_width > 1u ? expected_width / 2u : 1u;
        expected_height = expected_height > 1u ? expected_height / 2u : 1u;
    }
    /* It ends at 1 by 1, and nothing lies past the end. */
    CHECK_EQ_U64(expected_width, 1u);
    CHECK_EQ_U64(expected_height, 1u);
    texture_mip_t last;
    REQUIRE(texture_mip_get(texture, id, entry->level_count - 1u, 0u, &last));
    CHECK_EQ_U64(last.width, 1u);
    CHECK_EQ_U64(last.height, 1u);
    CHECK(!texture_mip_get(texture, id, entry->level_count, 0u, &last));
    CHECK(!texture_mip_get(texture, id, 0u, entry->frame_count, &last));

    /* Vector data, section by section, frame by frame. */
    uint64_t mcvec_length = 0;
    const uint8_t *mcvec = texture_test_read(
        t, texture_test_format(t, "%s/%s/index.mcvec", t->borrowed_args[2], info.borrowed_name), &mcvec_length);
    REQUIRE(mcvec != nullptr);
    const uint64_t count = (uint64_t)entry->width * entry->height;
    const uint64_t all = count * entry->frame_count;
    const uint8_t *palette = mcvec + TEXTURE_TEST_MCVEC_HEADER_BYTES;
    const uint8_t *indices = palette + entry->palette_count * 4ull;
    const uint8_t *height = indices + all;
    const uint8_t *normal = height + all;
    const uint8_t *emissive = normal + all * 2u;
    CHECK_EQ_U64(mcvec_length, TEXTURE_TEST_MCVEC_HEADER_BYTES + entry->palette_count * 4ull + all * 5u);
    for (uint32_t frame = 0; frame < entry->frame_count; frame++) {
        texture_vector_t vector;
        REQUIRE(texture_vector_get(texture, id, frame, &vector));
        CHECK_EQ_U64(vector.count, count);
        CHECK_MEM_EQ(vector.borrowed_palette, palette, entry->palette_count * 4ull);
        CHECK_MEM_EQ(vector.borrowed_indices, indices + count * frame, count);
        CHECK_MEM_EQ(vector.borrowed_height, height + count * frame, count);
        CHECK_MEM_EQ(vector.borrowed_normal, normal + count * frame * 2u, count * 2u);
        CHECK_MEM_EQ(vector.borrowed_emissive, emissive + count * frame, count);
    }
    texture_vector_t vector;
    CHECK(!texture_vector_get(texture, id, entry->frame_count, &vector));
    /* The header's figures came through. */
    CHECK_MEM_EQ(entry->mean_rgba, mcvec + 40u, 4u);
    CHECK_EQ_U64(entry->alpha_class, mcvec[44]);
    CHECK_EQ_U64(entry->emissive_class, mcvec[45]);
    CHECK_EQ_U64(entry->emissive_level, mcvec[46]);
    CHECK_MEM_EQ(&entry->source_hash, mctex + 24u, 8u);
}

TEST(every_texture_matches_its_source_files) {
    REQUIRE(t->arg_count == 3);
    texture_t texture;
    REQUIRE(texture_init(&texture));
    for (uint32_t id = 0; id < texture_count(&texture); id++) {
        texture_test_check_one(t, &texture, id);
    }
    texture_deinit(&texture);
}

TEST(sections_hold_exactly_the_textures) {
    texture_t texture;
    REQUIRE(texture_init(&texture));
    const uint8_t *pixels = nullptr;
    uint64_t bytes = 0;
    texture_pixels_get(&texture, &pixels, &bytes);
    CHECK_EQ_U64((uintptr_t)pixels % TEXTURE_BLOB_ALIGNMENT, 0u);
    const uint32_t count = texture_count(&texture);
    uint64_t end = 0;
    for (uint32_t id = 0; id < count; id++) {
        texture_info_t info;
        REQUIRE(texture_info_get(&texture, id, &info));
        CHECK(info.borrowed_entry->pixels_offset >= end);
        CHECK(info.borrowed_entry->pixels_offset - end < TEXTURE_BLOB_TEXTURE_ALIGNMENT);
        end = info.borrowed_entry->pixels_offset + info.borrowed_entry->pixels_bytes;
        texture_mip_t first;
        REQUIRE(texture_mip_get(&texture, id, 0u, 0u, &first));
        CHECK(first.borrowed_pixels == pixels + info.borrowed_entry->pixels_offset);
    }
    CHECK_EQ_U64(end, bytes);
    texture_info_t info;
    texture_mip_t mip;
    texture_vector_t vector;
    CHECK(!texture_info_get(&texture, count, &info));
    CHECK(!texture_mip_get(&texture, count, 0u, 0u, &mip));
    CHECK(!texture_vector_get(&texture, count, 0u, &vector));
    texture_deinit(&texture);
}

/* ---- Real textures ----------------------------------------------------------------------------------------------- */

static uint32_t texture_test_id(test_context_t *t, const texture_t *texture, const char *name) {
    uint32_t id = 0;
    CHECK(texture_find(texture, name, &id));
    return id;
}

static uint64_t texture_test_solid(const texture_mip_t *mip) {
    uint64_t solid = 0;
    for (uint64_t i = 0; i < mip->bytes / 4u; i++) {
        solid += mip->borrowed_pixels[i * 4u + 3u] == 255u ? 1u : 0u;
    }
    return solid;
}

TEST(leaves_are_cutout_and_keep_their_density_down_the_chain) {
    texture_t texture;
    REQUIRE(texture_init(&texture));
    const uint32_t id = texture_test_id(t, &texture, "blocks/oak_leaves");
    texture_info_t info;
    REQUIRE(texture_info_get(&texture, id, &info));
    CHECK_EQ_U64(info.borrowed_entry->alpha_class, TEXTURE_BLOB_ALPHA_CUTOUT);
    texture_mip_t zero;
    REQUIRE(texture_mip_get(&texture, id, 0u, 0u, &zero));
    const uint64_t solid_zero = texture_test_solid(&zero);
    CHECK(solid_zero > 0u && solid_zero < zero.bytes / 4u);
    /* Every cutout texture, not just these: each level of each frame holds level 0's share, rounded, to the texel. */
    uint32_t cutouts = 0;
    for (uint32_t other = 0; other < texture_count(&texture); other++) {
        REQUIRE(texture_info_get(&texture, other, &info));
        if (info.borrowed_entry->alpha_class != TEXTURE_BLOB_ALPHA_CUTOUT) {
            continue;
        }
        cutouts++;
        for (uint32_t frame = 0; frame < info.borrowed_entry->frame_count; frame++) {
            texture_mip_t top;
            REQUIRE(texture_mip_get(&texture, other, 0u, frame, &top));
            const uint64_t top_texels = top.bytes / 4u;
            const uint64_t top_solid = texture_test_solid(&top);
            for (uint32_t level = 1; level < info.borrowed_entry->level_count; level++) {
                texture_mip_t mip;
                REQUIRE(texture_mip_get(&texture, other, level, frame, &mip));
                const uint64_t texels = mip.bytes / 4u;
                CHECK_EQ_U64(texture_test_solid(&mip), (top_solid * texels + top_texels / 2u) / top_texels);
            }
        }
    }
    CHECK(cutouts > 100u);
    texture_deinit(&texture);
}

TEST(a_lit_furnace_is_opaque_though_its_file_says_252) {
    texture_t texture;
    REQUIRE(texture_init(&texture));
    const uint32_t id = texture_test_id(t, &texture, "blocks/furnace_front_lit");
    texture_info_t info;
    REQUIRE(texture_info_get(&texture, id, &info));
    CHECK_EQ_U64(info.borrowed_entry->alpha_class, TEXTURE_BLOB_ALPHA_OPAQUE);
    CHECK(info.borrowed_entry->emissive_level > 0u);
    texture_deinit(&texture);
}

/* Its pack file is the same pixels as the unlit top, all dim and brown, so thresholds would leave it dark. */
TEST(burning_tnt_gives_light) {
    texture_t texture;
    REQUIRE(texture_init(&texture));
    texture_info_t info;
    REQUIRE(texture_info_get(&texture, texture_test_id(t, &texture, "blocks/tnt_top_burning"), &info));
    CHECK_EQ_U64(info.borrowed_entry->emissive_class, TEXTURE_BLOB_EMISSIVE_FULL);
    CHECK_EQ_U64(info.borrowed_entry->emissive_level, 8u);
    texture_deinit(&texture);
}

TEST(an_animation_has_frames_that_differ) {
    texture_t texture;
    REQUIRE(texture_init(&texture));
    uint32_t animated = 0;
    for (uint32_t id = 0; id < texture_count(&texture); id++) {
        texture_info_t info;
        REQUIRE(texture_info_get(&texture, id, &info));
        if (info.borrowed_entry->frame_count < 2u) {
            continue;
        }
        animated++;
        texture_mip_t first;
        REQUIRE(texture_mip_get(&texture, id, 0u, 0u, &first));
        /* Some frame must differ, though not the next: the pack's water_flow and lava_flow hold each frame twice. */
        bool any_differs = false;
        for (uint32_t frame = 1; frame < info.borrowed_entry->frame_count; frame++) {
            texture_mip_t later;
            REQUIRE(texture_mip_get(&texture, id, 0u, frame, &later));
            CHECK(later.borrowed_pixels == first.borrowed_pixels + first.bytes * frame);
            any_differs = any_differs || memcmp(first.borrowed_pixels, later.borrowed_pixels, first.bytes) != 0;
        }
        CHECK(any_differs);
    }
    CHECK(animated > 0u);
    texture_deinit(&texture);
}

/* texture_vector works out mean_rgba by its own path, over the raw alpha. On an opaque, square, one-frame texture the whole
 * chain's last texel is the same average, so the two must agree, give or take rounding. */
TEST(the_last_level_agrees_with_the_mean_colour) {
    texture_t texture;
    REQUIRE(texture_init(&texture));
    uint32_t compared = 0;
    for (uint32_t id = 0; id < texture_count(&texture); id++) {
        texture_info_t info;
        REQUIRE(texture_info_get(&texture, id, &info));
        const texture_blob_entry_t *entry = info.borrowed_entry;
        if (entry->alpha_class != TEXTURE_BLOB_ALPHA_OPAQUE || entry->frame_count != 1u ||
            entry->width != entry->height) {
            continue;
        }
        compared++;
        texture_mip_t last;
        REQUIRE(texture_mip_get(&texture, id, entry->level_count - 1u, 0u, &last));
        for (uint32_t channel = 0; channel < 3u; channel++) {
            const int64_t gap = (int64_t)last.borrowed_pixels[channel] - (int64_t)entry->mean_rgba[channel];
            CHECK(gap >= -1 && gap <= 1);
        }
        CHECK_EQ_U64(last.borrowed_pixels[3], 255u);
    }
    CHECK(compared > 100u);
    texture_deinit(&texture);
}

/* ---- Damage ------------------------------------------------------------------------------------------------------ */

TEST(bytes_that_are_not_a_whole_blob_are_refused) {
    texture_blob_view_t view;
    uint8_t *blob = texture_test_copy(t);
    const uint64_t length = embed_assets_texture_blob_bytes;
    CHECK_EQ_U64(texture_blob_parse(blob, 0u, &view), TEXTURE_BLOB_TRUNCATED);
    CHECK_EQ_U64(texture_blob_parse(blob, sizeof(texture_blob_header_t) - 1u, &view), TEXTURE_BLOB_TRUNCATED);
    CHECK_EQ_U64(texture_blob_parse(blob, length - 1u, &view), TEXTURE_BLOB_TRUNCATED);
    CHECK_EQ_U64(texture_blob_parse(blob, length + 1u, &view), TEXTURE_BLOB_TRAILING);
    CHECK(view.borrowed_header == nullptr); /* a failure leaves the view empty */
    uint8_t *shifted = blob + TEXTURE_BLOB_ALIGNMENT;
    memmove(shifted + 8u, blob, length);
    CHECK_EQ_U64(texture_blob_parse(shifted + 8u, length, &view), TEXTURE_BLOB_BAD_ALIGNMENT);
}

TEST(a_changed_byte_anywhere_fails_the_hash) {
    texture_blob_view_t view;
    uint8_t *blob = texture_test_copy(t);
    const uint64_t length = embed_assets_texture_blob_bytes;
    const uint64_t positions[] = {sizeof(texture_blob_header_t), length / 2u, length - 1u};
    for (uint32_t i = 0; i < 3u; i++) {
        blob[positions[i]] ^= 1u;
        CHECK_EQ_U64(texture_blob_parse(blob, length, &view), TEXTURE_BLOB_BAD_HASH);
        blob[positions[i]] ^= 1u;
    }
    CHECK_EQ_U64(texture_blob_parse(blob, length, &view), TEXTURE_BLOB_OK);
}

/* One change to a field, as a wrong writer might make it. The hash is mended afterwards, so the check behind it answers. */
typedef struct texture_test_damage_t {
    uint32_t field_offset; /* from the start of the blob */
    uint32_t width;        /* 1, 2, 4 or 8 bytes */
    uint64_t value;        /* written over the field */
    uint8_t add;           /* 1: value is added to the field */
    uint8_t want;          /* a texture_blob_status_t */
    uint8_t _pad0[6];
} texture_test_damage_t;
static_assert(sizeof(texture_test_damage_t) == 24);

static void texture_test_apply(uint8_t *blob, const texture_test_damage_t *damage) {
    uint64_t field = 0;
    memcpy(&field, blob + damage->field_offset, damage->width);
    field = damage->add != 0u ? field + damage->value : damage->value;
    memcpy(blob + damage->field_offset, &field, damage->width);
}

#define HEADER_FIELD(field) ((uint32_t)offsetof(texture_blob_header_t, field))
#define HEADER_WIDTH(field) ((uint32_t)sizeof(((texture_blob_header_t *)nullptr)->field))
#define ENTRY_FIELD(id, field)                                                                                         \
    ((uint32_t)(sizeof(texture_blob_header_t) + (id) * sizeof(texture_blob_entry_t) +                                  \
                offsetof(texture_blob_entry_t, field)))
#define ENTRY_WIDTH(field) ((uint32_t)sizeof(((texture_blob_entry_t *)nullptr)->field))
#define SET_HEADER(field, to, status) {HEADER_FIELD(field), HEADER_WIDTH(field), (to), 0u, (status), {0}}
#define ADD_HEADER(field, to, status) {HEADER_FIELD(field), HEADER_WIDTH(field), (to), 1u, (status), {0}}
#define SET_ENTRY(id, field, to, status) {ENTRY_FIELD(id, field), ENTRY_WIDTH(field), (to), 0u, (status), {0}}
#define ADD_ENTRY(id, field, to, status) {ENTRY_FIELD(id, field), ENTRY_WIDTH(field), (to), 1u, (status), {0}}

static const texture_test_damage_t texture_test_damages[] = {
    SET_HEADER(magic, 0x12345678u, TEXTURE_BLOB_BAD_MAGIC),
    ADD_HEADER(version, 1u, TEXTURE_BLOB_BAD_VERSION),
    ADD_HEADER(total_bytes, 1u, TEXTURE_BLOB_TRUNCATED),
    ADD_HEADER(total_bytes, UINT64_MAX, TEXTURE_BLOB_TRAILING), /* adding this is taking one away */
    SET_HEADER(texture_count, 0u, TEXTURE_BLOB_BAD_LAYOUT),
    SET_HEADER(texture_count, 70000u, TEXTURE_BLOB_BAD_LAYOUT),
    ADD_HEADER(entries_offset, 8u, TEXTURE_BLOB_BAD_LAYOUT),
    ADD_HEADER(names_offset, 8u, TEXTURE_BLOB_BAD_LAYOUT),
    ADD_HEADER(name_bytes, 4096u, TEXTURE_BLOB_BAD_LAYOUT),
    ADD_HEADER(pixels_offset, 64u, TEXTURE_BLOB_BAD_LAYOUT),
    ADD_HEADER(vectors_offset, 32u, TEXTURE_BLOB_BAD_LAYOUT),
    ADD_HEADER(pixels_bytes, 4096u, TEXTURE_BLOB_BAD_LAYOUT),
    SET_HEADER(pixels_bytes, UINT64_MAX, TEXTURE_BLOB_BAD_LAYOUT),
    ADD_HEADER(vectors_bytes, 1u, TEXTURE_BLOB_BAD_LAYOUT),
    ADD_ENTRY(0u, name_hash, 1u, TEXTURE_BLOB_BAD_NAME),
    SET_ENTRY(0u, name_length, 0u, TEXTURE_BLOB_BAD_NAME),
    ADD_ENTRY(0u, name_length, 1u, TEXTURE_BLOB_BAD_NAME),
    SET_ENTRY(0u, name_offset, 0xFFFFFFF0u, TEXTURE_BLOB_BAD_NAME),
    ADD_ENTRY(0u, name_offset, 1u, TEXTURE_BLOB_BAD_NAME),
    SET_ENTRY(0u, width, 0u, TEXTURE_BLOB_BAD_SIZE),
    SET_ENTRY(0u, height, 0u, TEXTURE_BLOB_BAD_SIZE),
    SET_ENTRY(0u, width, 5000u, TEXTURE_BLOB_BAD_SIZE),
    SET_ENTRY(0u, height, 5000u, TEXTURE_BLOB_BAD_SIZE),
    SET_ENTRY(0u, frame_count, 0u, TEXTURE_BLOB_BAD_SIZE),
    SET_ENTRY(0u, frame_count, 257u, TEXTURE_BLOB_BAD_SIZE),
    ADD_ENTRY(0u, level_count, 1u, TEXTURE_BLOB_BAD_SIZE),
    SET_ENTRY(0u, palette_count, 0u, TEXTURE_BLOB_BAD_SIZE),
    SET_ENTRY(0u, palette_count, 257u, TEXTURE_BLOB_BAD_SIZE),
    ADD_ENTRY(0u, level_offset[1], 4u, TEXTURE_BLOB_BAD_OFFSET),
    SET_ENTRY(0u, level_offset[12], 4u, TEXTURE_BLOB_BAD_OFFSET),
    ADD_ENTRY(0u, pixels_bytes, 4u, TEXTURE_BLOB_BAD_OFFSET),
    ADD_ENTRY(0u, pixels_offset, 32u, TEXTURE_BLOB_BAD_OFFSET),
    ADD_ENTRY(1u, pixels_offset, 32u, TEXTURE_BLOB_BAD_OFFSET),
    ADD_ENTRY(0u, vector_offset, 32u, TEXTURE_BLOB_BAD_OFFSET),
    ADD_ENTRY(1u, vector_offset, 32u, TEXTURE_BLOB_BAD_OFFSET),
    ADD_ENTRY(0u, vector_bytes, 1u, TEXTURE_BLOB_BAD_OFFSET),
    SET_ENTRY(0u, alpha_class, 3u, TEXTURE_BLOB_BAD_CLASS),
    SET_ENTRY(0u, emissive_class, 3u, TEXTURE_BLOB_BAD_CLASS),
    SET_ENTRY(0u, emissive_level, 16u, TEXTURE_BLOB_BAD_CLASS),
    SET_ENTRY(0u, palette_exact, 2u, TEXTURE_BLOB_BAD_CLASS),
    SET_ENTRY(0u, _pad0[0], 1u, TEXTURE_BLOB_BAD_CLASS),
    SET_ENTRY(0u, _pad0[1], 1u, TEXTURE_BLOB_BAD_CLASS),
};

TEST(each_field_is_checked_with_the_hash_mended) {
    uint8_t *blob = texture_test_copy(t);
    const uint64_t length = embed_assets_texture_blob_bytes;
    /* The damage below assumes these, and none is rare. */
    const texture_blob_entry_t *first =
        (const texture_blob_entry_t *)(const void *)(blob + sizeof(texture_blob_header_t));
    REQUIRE(first->level_count >= 2u && first->level_count < TEXTURE_BLOB_MAX_LEVELS);
    uint8_t *saved = test_alloc(t, sizeof(texture_blob_header_t) + 2u * sizeof(texture_blob_entry_t));
    const uint64_t saved_bytes = sizeof(texture_blob_header_t) + 2u * sizeof(texture_blob_entry_t);
    memcpy(saved, blob, saved_bytes);
    for (uint32_t i = 0; i < sizeof(texture_test_damages) / sizeof(texture_test_damages[0]); i++) {
        const texture_test_damage_t *damage = &texture_test_damages[i];
        memcpy(blob, saved, saved_bytes);
        texture_test_apply(blob, damage);
        texture_test_mend_hash(blob, length);
        texture_blob_view_t view;
        const texture_blob_status_t status = texture_blob_parse(blob, length, &view);
        if (status != damage->want) {
            printf("damage %u at offset %u: got %u (%s), wanted %u\n", i, damage->field_offset, (uint32_t)status,
                   texture_blob_status_text(status), (uint32_t)damage->want);
        }
        CHECK_EQ_U64(status, damage->want);
    }
    /* And with everything put back it is whole again, so the table above did not pass for some other reason. */
    memcpy(blob, saved, saved_bytes);
    texture_test_mend_hash(blob, length);
    texture_blob_view_t view;
    CHECK_EQ_U64(texture_blob_parse(blob, length, &view), TEXTURE_BLOB_OK);
}

TEST(names_out_of_order_or_unterminated_are_refused) {
    uint8_t *blob = texture_test_copy(t);
    const uint64_t length = embed_assets_texture_blob_bytes;
    const texture_blob_header_t *header = (const texture_blob_header_t *)(const void *)blob;
    texture_blob_entry_t *entries = (texture_blob_entry_t *)(void *)(blob + header->entries_offset);
    uint8_t *names = blob + header->names_offset;
    texture_blob_view_t view;

    /* The second entry's name swapped for the first's: a duplicate, so the order is not strictly ascending. */
    const texture_blob_entry_t second = entries[1];
    entries[1].name_offset = entries[0].name_offset;
    entries[1].name_length = entries[0].name_length;
    entries[1].name_hash = entries[0].name_hash;
    texture_test_mend_hash(blob, length);
    CHECK_EQ_U64(texture_blob_parse(blob, length, &view), TEXTURE_BLOB_BAD_NAME);
    entries[1] = second;

    /* The NUL after the first name overwritten. */
    const uint64_t nul = (uint64_t)entries[0].name_offset + entries[0].name_length;
    names[nul] = 'x';
    texture_test_mend_hash(blob, length);
    CHECK_EQ_U64(texture_blob_parse(blob, length, &view), TEXTURE_BLOB_BAD_NAME);
    names[nul] = 0u;

    /* A NUL inside it, with the hash of what is there, so that only the check for a NUL can answer. */
    const uint8_t kept = names[entries[0].name_offset];
    const uint64_t kept_hash = entries[0].name_hash;
    names[entries[0].name_offset] = 0u;
    entries[0].name_hash = texture_blob_hash(names + entries[0].name_offset, entries[0].name_length);
    texture_test_mend_hash(blob, length);
    CHECK_EQ_U64(texture_blob_parse(blob, length, &view), TEXTURE_BLOB_BAD_NAME);
    names[entries[0].name_offset] = kept;
    entries[0].name_hash = kept_hash;
    texture_test_mend_hash(blob, length);
    CHECK_EQ_U64(texture_blob_parse(blob, length, &view), TEXTURE_BLOB_OK);
}

TEST(every_status_has_a_sentence) {
    for (uint32_t status = 0; status < 256u; status++) {
        const char *text = texture_blob_status_text((texture_blob_status_t)status);
        CHECK(text != nullptr && text[0] != '\0');
    }
}

/* ---- The Owner --------------------------------------------------------------------------------------------------- */

TEST(init_and_deinit_balance_the_heap) {
    heap_counters_t before;
    heap_counters_read(&before);
    texture_t texture;
    REQUIRE(texture_init(&texture));
    heap_counters_t during;
    heap_counters_read(&during);
    CHECK_EQ_U64(during.live_regions, before.live_regions + 1u);
    /* Lookup allocates nothing: what the game may do every frame. */
    uint32_t id = 0;
    CHECK(texture_find(&texture, "blocks/dirt", &id));
    texture_mip_t mip;
    CHECK(texture_mip_get(&texture, id, 1u, 0u, &mip));
    heap_counters_t after_lookup;
    heap_counters_read(&after_lookup);
    CHECK_EQ_U64(after_lookup.reserve_calls, during.reserve_calls);
    const uint64_t arena_allocs = texture.arena_tables.alloc_count;
    CHECK(texture_find(&texture, "blocks/stone", &id));
    CHECK_EQ_U64(texture.arena_tables.alloc_count, arena_allocs);
    texture_deinit(&texture);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    /* A second deinit does nothing, and neither does looking in what is left. */
    texture_deinit(&texture);
    CHECK(!texture_find(&texture, "blocks/dirt", &id));
    CHECK_EQ_U64(texture_count(&texture), 0u);
}

TEST(a_refused_blob_leaves_nothing_behind) {
    heap_counters_t before;
    heap_counters_read(&before);
    uint8_t *blob = texture_test_copy(t);
    blob[embed_assets_texture_blob_bytes - 1u] ^= 1u;
    texture_t texture;
    CHECK(!texture_init_from(&texture, blob, embed_assets_texture_blob_bytes));
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    uint32_t id = 0;
    CHECK(!texture_find(&texture, "blocks/dirt", &id));
    texture_deinit(&texture); /* the zeroed handle it left */
}

TEST(a_zeroed_texture_is_safe) {
    texture_t texture;
    memset(&texture, 0, sizeof(texture));
    uint32_t id = 0;
    CHECK(!texture_find(&texture, "blocks/dirt", &id));
    CHECK_EQ_U64(texture_count(&texture), 0u);
    texture_info_t info;
    CHECK(!texture_info_get(&texture, 0u, &info));
    const uint8_t *pixels = embed_assets_texture_blob;
    uint64_t bytes = 1u;
    texture_pixels_get(&texture, &pixels, &bytes);
    CHECK(pixels == nullptr);
    CHECK_EQ_U64(bytes, 0u);
    texture_deinit(&texture);
}

TEST_MAIN()
