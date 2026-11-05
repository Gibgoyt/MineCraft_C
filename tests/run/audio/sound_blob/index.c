/*
 * ctest run_audio_sound_blob. See README.md. Argument from test.cmake: the blob the build generated. Nothing here plays: it reads
 * the bytes linked into this program, and asks them what the game will ask.
 */
#include "harness/index.h"
#include "simd/embed_assets/index.h"
#include "tables/blocks/index.h"
#include "types/sound_blob/index.h"

#include <stdio.h>
#include <string.h>

TEST(the_blob_linked_in_is_the_one_the_build_made) {
    REQUIRE(t->arg_count == 1);
    CHECK_EQ_U64((uintptr_t)embed_assets_sound_blob % SOUND_BLOB_ALIGNMENT, 0u);
    FILE *file = fopen(t->borrowed_args[0], "rb");
    REQUIRE(file != nullptr);
    uint8_t *bytes = test_alloc(t, embed_assets_sound_blob_bytes + 1u);
    const uint64_t length = fread(bytes, 1u, embed_assets_sound_blob_bytes + 1u, file);
    fclose(file);
    CHECK_EQ_U64(length, embed_assets_sound_blob_bytes);
    REQUIRE(length == embed_assets_sound_blob_bytes);
    CHECK_MEM_EQ(embed_assets_sound_blob, bytes, length);
}

TEST(every_block_that_can_be_mined_finds_all_three_of_its_sounds) {
    sound_blob_view_t view;
    REQUIRE(sound_blob_check(embed_assets_sound_blob, embed_assets_sound_blob_bytes, &view) == SOUND_BLOB_OK);
    uint32_t silent = 0;
    for (uint32_t b = 0; b < BLOCK_COUNT; b++) {
        const uint32_t family = block_defs[b].sound;
        if (family == SOUND_FAMILY_NONE) {
            silent++;
            continue;
        }
        for (uint32_t kind = 0; kind < SOUND_KIND_COUNT; kind++) {
            uint32_t first = 0;
            const uint32_t count = sound_blob_find(&view, family, kind, &first);
            if (count == 0u) {
                fprintf(stderr, "%s: no %s sound\n", block_defs[b].name, sound_kind_name(kind));
            }
            CHECK(count > 0u);
            for (uint32_t v = 0; v < count; v++) {
                const sound_blob_entry_t *entry = &view.borrowed_entries[first + v];
                CHECK(entry->sample_count > 0u);
                CHECK(entry->peak > 0u);
                CHECK_EQ_U64((uintptr_t)sound_blob_samples(&view, entry) % SOUND_BLOB_SOUND_ALIGNMENT, 0u);
            }
        }
    }
    /* Air, water and lava, and nothing else. */
    CHECK_EQ_U64(silent, 3u);
}

TEST_MAIN()
