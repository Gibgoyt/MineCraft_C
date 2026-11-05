/* ctest run_audio_mixer. See README.md. Samples written to memory and checked, so nothing here can be heard. */
#include "harness/index.h"

#include "audio/mixer/index.h"
#include "heap/index.h"
#include "player/camera/index.h"
#include "simd/embed_assets/index.h"
#include "time/index.h"
#include "types/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    AM_FAKE_SOUNDS = 12u,
    AM_FAKE_SAMPLES = 64u,
    AM_FRAMES = 256u,
    AM_SEED = 0x1234u,
    AM_BENCH_SECONDS = 20u,
};

/* A blob of the mixer's own view: sounds of a known shape, so that what comes out can be worked out by hand. Stone hits are all a ramp up from 1000, and glass breaks all a
 * flat 30000 or -30000. */
typedef struct am_fake_t {
    sound_blob_entry_t entries[AM_FAKE_SOUNDS];                   /* 0 */
    alignas(32) int16_t samples[AM_FAKE_SOUNDS][AM_FAKE_SAMPLES]; /* 288 */
    sound_blob_view_t view;
    uint8_t _pad0[8];
} am_fake_t;
static_assert(sizeof(am_fake_t) == 1856);

/* Sorted by family and kind, as the blob's are. test_alloc lines up on 16, and this on 32. */
static am_fake_t *am_fake_make(test_context_t *t) {
    const uintptr_t start = (uintptr_t)test_alloc(t, sizeof(am_fake_t) + 32u);
    am_fake_t *const fake = (am_fake_t *)((start + 31u) & ~(uintptr_t)31u);
    memset(fake, 0, sizeof(*fake));
    const uint8_t families[AM_FAKE_SOUNDS] = {
        SOUND_FAMILY_STONE,  SOUND_FAMILY_STONE,  SOUND_FAMILY_STONE, SOUND_FAMILY_WOOD,
        SOUND_FAMILY_DIRT,   SOUND_FAMILY_GRAVEL, SOUND_FAMILY_SAND,  SOUND_FAMILY_GRASS,
        SOUND_FAMILY_LEAVES, SOUND_FAMILY_SNOW,   SOUND_FAMILY_GLASS, SOUND_FAMILY_GLASS,
    };
    const uint8_t variants[AM_FAKE_SOUNDS] = {1u, 2u, 3u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 2u};
    for (uint32_t i = 0u; i < AM_FAKE_SOUNDS; i++) {
        const bool glass = families[i] == SOUND_FAMILY_GLASS;
        fake->entries[i] = (sound_blob_entry_t){.samples_offset = (uint64_t)i * sizeof(fake->samples[0]),
                                                .sample_count = AM_FAKE_SAMPLES,
                                                .peak = 0u,
                                                .family = families[i],
                                                .kind = glass ? (uint8_t)SOUND_KIND_BREAK : (uint8_t)SOUND_KIND_HIT,
                                                .variant = variants[i],
                                                ._pad0 = {0}};
        for (uint32_t s = 0u; s < AM_FAKE_SAMPLES; s++) {
            fake->samples[i][s] = glass ? (int16_t)(i == 10u ? 30000 : -30000) : (int16_t)(1000 + (int32_t)s);
        }
    }
    fake->view = (sound_blob_view_t){.borrowed_entries = fake->entries,
                                     .borrowed_samples = (const uint8_t *)fake->samples,
                                     .sound_count = AM_FAKE_SOUNDS,
                                     ._pad0 = {0}};
    return fake;
}

static void am_blob_view(test_context_t *t, sound_blob_view_t *view) {
    CHECK_EQ_U64(sound_blob_check(embed_assets_sound_blob, embed_assets_sound_blob_bytes, view), SOUND_BLOB_OK);
}

TEST(nothing_playing_is_silence_for_as_long_as_it_is_asked) {
    am_fake_t *const fake = am_fake_make(t);
    audio_mixer_t mixer;
    audio_mixer_start(&mixer, &fake->view, AM_SEED);
    int16_t out[AM_FRAMES * 2u];
    memset(out, 0x55, sizeof(out));
    audio_mixer_render(&mixer, out, AM_FRAMES, 100u);
    const int16_t zeros[AM_FRAMES * 2u] = {0};
    CHECK_MEM_EQ(out, zeros, sizeof(out));
    CHECK_EQ_U64(audio_mixer_playing(&mixer), 0u);
}

TEST(a_voice_at_gain_one_and_full_volume_is_exactly_its_samples_then_silence) {
    am_fake_t *const fake = am_fake_make(t);
    audio_mixer_t mixer;
    audio_mixer_start(&mixer, &fake->view, AM_SEED);
    REQUIRE(audio_mixer_play(&mixer, SOUND_FAMILY_WOOD, SOUND_KIND_HIT, AUDIO_MIXER_GAIN_ONE, AUDIO_MIXER_GAIN_ONE));
    CHECK_EQ_U64(audio_mixer_playing(&mixer), 1u);
    int16_t out[AM_FRAMES * 2u];
    audio_mixer_render(&mixer, out, AM_FRAMES, 100u);
    for (uint32_t i = 0u; i < AM_FAKE_SAMPLES; i++) {
        CHECK_EQ_I64(out[i * 2u], 1000 + (int32_t)i);
        CHECK_EQ_I64(out[i * 2u + 1u], 1000 + (int32_t)i);
    }
    for (uint32_t i = AM_FAKE_SAMPLES; i < AM_FRAMES; i++) {
        CHECK_EQ_I64(out[i * 2u], 0);
        CHECK_EQ_I64(out[i * 2u + 1u], 0);
    }
    CHECK_EQ_U64(audio_mixer_playing(&mixer), 0u);
    CHECK_EQ_U64(mixer.played_count, 1u);
}

TEST(a_sound_can_be_taken_in_pieces_of_any_size_with_the_same_samples) {
    am_fake_t *const fake = am_fake_make(t);
    audio_mixer_t mixer;
    audio_mixer_start(&mixer, &fake->view, AM_SEED);
    REQUIRE(audio_mixer_play(&mixer, SOUND_FAMILY_WOOD, SOUND_KIND_HIT, AUDIO_MIXER_GAIN_ONE, AUDIO_MIXER_GAIN_ONE));
    int16_t out[AM_FRAMES * 2u];
    uint32_t at = 0u;
    for (uint32_t size = 1u; at < AM_FRAMES / 2u; size = size % 7u + 1u) {
        audio_mixer_render(&mixer, &out[at * 2u], size, 100u);
        at += size;
    }
    for (uint32_t i = 0u; i < AM_FAKE_SAMPLES; i++) {
        CHECK_EQ_I64(out[i * 2u], 1000 + (int32_t)i);
    }
}

TEST(each_ear_has_its_own_gain) {
    am_fake_t *const fake = am_fake_make(t);
    audio_mixer_t mixer;
    audio_mixer_start(&mixer, &fake->view, AM_SEED);
    REQUIRE(audio_mixer_play(&mixer, SOUND_FAMILY_WOOD, SOUND_KIND_HIT, AUDIO_MIXER_GAIN_ONE, 0u));
    int16_t out[8];
    audio_mixer_render(&mixer, out, 4u, 100u);
    CHECK_EQ_I64(out[0], 1000);
    CHECK_EQ_I64(out[1], 0);
    audio_mixer_start(&mixer, &fake->view, AM_SEED);
    REQUIRE(
        audio_mixer_play(&mixer, SOUND_FAMILY_WOOD, SOUND_KIND_HIT, AUDIO_MIXER_GAIN_ONE / 2u, AUDIO_MIXER_GAIN_ONE));
    audio_mixer_render(&mixer, out, 4u, 100u);
    CHECK_EQ_I64(out[0], 500);
    CHECK_EQ_I64(out[1], 1000);
}

TEST(volume_is_a_square_of_the_slider_with_0_silent_and_past_100_held) {
    am_fake_t *const fake = am_fake_make(t);
    static const uint32_t percents[] = {0u, 10u, 50u, 70u, 100u, 250u};
    static const int32_t first_sample[] = {0, 10, 250, 490, 1000, 1000};
    for (uint32_t i = 0u; i < sizeof(percents) / sizeof(percents[0]); i++) {
        audio_mixer_t mixer;
        audio_mixer_start(&mixer, &fake->view, AM_SEED);
        REQUIRE(
            audio_mixer_play(&mixer, SOUND_FAMILY_WOOD, SOUND_KIND_HIT, AUDIO_MIXER_GAIN_ONE, AUDIO_MIXER_GAIN_ONE));
        int16_t out[AM_FAKE_SAMPLES * 2u];
        audio_mixer_render(&mixer, out, AM_FAKE_SAMPLES, percents[i]);
        CHECK_NEAR((float64_t)out[0], (float64_t)first_sample[i], 1.0f64);
        if (percents[i] == 0u) {
            const int16_t zeros[AM_FAKE_SAMPLES * 2u] = {0};
            CHECK_MEM_EQ(out, zeros, sizeof(out));
        }
    }
}

TEST(voices_add_up_and_what_goes_past_16_bits_is_held_and_counted) {
    am_fake_t *const fake = am_fake_make(t);
    audio_mixer_t mixer;
    audio_mixer_start(&mixer, &fake->view, AM_SEED);
    REQUIRE(audio_mixer_play(&mixer, SOUND_FAMILY_WOOD, SOUND_KIND_HIT, AUDIO_MIXER_GAIN_ONE, AUDIO_MIXER_GAIN_ONE));
    REQUIRE(audio_mixer_play(&mixer, SOUND_FAMILY_WOOD, SOUND_KIND_HIT, AUDIO_MIXER_GAIN_ONE, AUDIO_MIXER_GAIN_ONE));
    int16_t out[8];
    audio_mixer_render(&mixer, out, 4u, 100u);
    CHECK_EQ_I64(out[0], 2000);
    CHECK_EQ_U64(mixer.clipped_count, 0u);

    /* Glass's two variants never come twice in a row, so these are one of each, and each pair cancels; both of one are found by asking for it by hand. */
    static const int16_t limits[2] = {INT16_MAX, INT16_MIN};
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        audio_mixer_start(&mixer, &fake->view, AM_SEED);
        const uint32_t entry = pass == 0u ? 10u : 11u;
        for (uint32_t i = 0u; i < 3u; i++) {
            mixer.voices[i] = (audio_voice_t){.borrowed_samples = fake->samples[entry],
                                              .sample_count = AM_FAKE_SAMPLES,
                                              .at = 0u,
                                              .gain_left = AUDIO_MIXER_GAIN_ONE,
                                              .gain_right = AUDIO_MIXER_GAIN_ONE,
                                              ._pad0 = {0}};
        }
        audio_mixer_render(&mixer, out, 4u, 100u);
        for (uint32_t i = 0u; i < 8u; i++) {
            CHECK_EQ_I64(out[i], limits[pass]);
        }
        CHECK_EQ_U64(mixer.clipped_count, 8u);
    }
}

TEST(eight_play_and_a_ninth_takes_the_place_of_the_one_nearest_its_end) {
    am_fake_t *const fake = am_fake_make(t);
    audio_mixer_t mixer;
    audio_mixer_start(&mixer, &fake->view, AM_SEED);
    for (uint32_t i = 0u; i < AUDIO_MIXER_VOICES; i++) {
        REQUIRE(audio_mixer_play(&mixer, SOUND_FAMILY_WOOD, SOUND_KIND_HIT, AUDIO_MIXER_GAIN_ONE, 0u));
        mixer.voices[i].at = 10u + i;
    }
    CHECK_EQ_U64(mixer.stolen_count, 0u);
    mixer.voices[5].at = 60u;
    REQUIRE(audio_mixer_play(&mixer, SOUND_FAMILY_WOOD, SOUND_KIND_HIT, 0u, AUDIO_MIXER_GAIN_ONE));
    CHECK_EQ_U64(mixer.stolen_count, 1u);
    CHECK_EQ_U64(audio_mixer_playing(&mixer), AUDIO_MIXER_VOICES);
    CHECK_EQ_U64(mixer.voices[5].at, 0u);
    CHECK_EQ_U64(mixer.voices[5].gain_right, AUDIO_MIXER_GAIN_ONE);
    CHECK_EQ_U64(mixer.voices[4].at, 14u);
}

TEST(what_the_blob_has_not_got_is_refused_and_plays_nothing) {
    am_fake_t *const fake = am_fake_make(t);
    audio_mixer_t mixer;
    audio_mixer_start(&mixer, &fake->view, AM_SEED);
    CHECK(!audio_mixer_play(&mixer, SOUND_FAMILY_NONE, SOUND_KIND_HIT, 1u, 1u));
    CHECK(!audio_mixer_play(&mixer, SOUND_FAMILY_METAL, SOUND_KIND_HIT, 1u, 1u));
    CHECK(!audio_mixer_play(&mixer, SOUND_FAMILY_STONE, SOUND_KIND_PLACE, 1u, 1u));
    CHECK(!audio_mixer_play(&mixer, SOUND_FAMILY_COUNT, SOUND_KIND_HIT, 1u, 1u));
    CHECK(!audio_mixer_play(&mixer, SOUND_FAMILY_STONE, SOUND_KIND_COUNT, 1u, 1u));
    CHECK_EQ_U64(audio_mixer_playing(&mixer), 0u);
}

/* ---- With the real sounds ----------------------------------------------------------------------------------------- */

TEST(the_same_sound_does_not_come_twice_running_and_every_variant_comes) {
    sound_blob_view_t view;
    am_blob_view(t, &view);
    audio_mixer_t mixer;
    audio_mixer_start(&mixer, &view, AM_SEED);
    for (uint32_t family = 1u; family < SOUND_FAMILY_COUNT; family++) {
        for (uint32_t kind = 0u; kind < SOUND_KIND_COUNT; kind++) {
            uint32_t first = 0u;
            const uint32_t count = sound_blob_find(&view, family, kind, &first);
            REQUIRE(count > 1u);
            uint32_t seen = 0u;
            uint8_t last = 0u;
            for (uint32_t i = 0u; i < 300u; i++) {
                REQUIRE(audio_mixer_play(&mixer, family, kind, 1u, 1u));
                const uint8_t variant = mixer.last_variant[family][kind];
                CHECK(variant != last);
                last = variant;
                seen |= 1u << variant;
            }
            for (uint32_t v = 1u; v <= count; v++) {
                CHECK((seen & (1u << v)) != 0u);
            }
        }
    }
}

TEST(every_sound_of_every_family_comes_out_as_it_went_in) {
    sound_blob_view_t view;
    am_blob_view(t, &view);
    int16_t *const out = test_alloc(t, (uint64_t)SOUND_BLOB_MAX_SAMPLES * 4u);
    for (uint32_t i = 0u; i < view.sound_count; i++) {
        const sound_blob_entry_t *const entry = &view.borrowed_entries[i];
        audio_mixer_t mixer;
        audio_mixer_start(&mixer, &view, AM_SEED);
        mixer.voices[0] = (audio_voice_t){.borrowed_samples = sound_blob_samples(&view, entry),
                                          .sample_count = entry->sample_count,
                                          .at = 0u,
                                          .gain_left = AUDIO_MIXER_GAIN_ONE,
                                          .gain_right = AUDIO_MIXER_GAIN_ONE,
                                          ._pad0 = {0}};
        audio_mixer_render(&mixer, out, entry->sample_count, 100u);
        const int16_t *const samples = sound_blob_samples(&view, entry);
        bool same = true;
        uint32_t peak = 0u;
        for (uint32_t s = 0u; s < entry->sample_count; s++) {
            same = same && out[s * 2u] == samples[s] && out[s * 2u + 1u] == samples[s];
            const int32_t value = samples[s] < 0 ? -(int32_t)samples[s] : (int32_t)samples[s];
            peak = (uint32_t)value > peak ? (uint32_t)value : peak;
        }
        CHECK(same);
        CHECK_EQ_U64(peak, entry->peak);
        CHECK(peak > 0u);
        CHECK_EQ_U64(audio_mixer_playing(&mixer), 0u);
    }
}

TEST(the_same_seed_and_plays_give_the_same_samples_and_the_heap_is_not_touched) {
    sound_blob_view_t view;
    am_blob_view(t, &view);
    enum : uint32_t { FRAMES = 48000u };
    int16_t *const first = test_alloc(t, FRAMES * 4u);
    int16_t *const second = test_alloc(t, FRAMES * 4u);
    heap_counters_t before, after;
    heap_counters_read(&before);
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        int16_t *const out = pass == 0u ? first : second;
        audio_mixer_t mixer;
        audio_mixer_start(&mixer, &view, AM_SEED);
        for (uint32_t at = 0u; at < FRAMES; at += 480u) {
            if (at % 4800u == 0u) {
                const uint32_t family = 1u + (at / 4800u) % (SOUND_FAMILY_COUNT - 1u);
                (void)audio_mixer_play(&mixer, family, (at / 4800u) % SOUND_KIND_COUNT, 20000u, 30000u);
            }
            audio_mixer_render(&mixer, &out[at * 2u], 480u, 70u);
        }
    }
    heap_counters_read(&after);
    CHECK_MEM_EQ(&before, &after, sizeof(before));
    CHECK_MEM_EQ(first, second, FRAMES * 4u);
    bool any = false;
    for (uint32_t i = 0u; i < FRAMES * 2u; i++) {
        any = any || first[i] != 0;
    }
    CHECK(any);
}

/* What the standard asks about a NASM kernel: is this worth one? This says how much of the thread's time eight voices take. It is printed and not checked, as a time depends on
 * what else is running. */
TEST(eight_voices_for_twenty_seconds_take_how_long) {
    sound_blob_view_t view;
    am_blob_view(t, &view);
    enum : uint32_t { CHUNK = 480u };
    int16_t out[CHUNK * 2u];
    audio_mixer_t mixer;
    audio_mixer_start(&mixer, &view, AM_SEED);
    const uint32_t chunks = AM_BENCH_SECONDS * SOUND_BLOB_SAMPLE_RATE / CHUNK;
    int64_t spent_ns = 0;
    uint64_t sum = 0u;
    for (uint32_t c = 0u; c < chunks; c++) {
        while (audio_mixer_playing(&mixer) < AUDIO_MIXER_VOICES) {
            REQUIRE(audio_mixer_play(&mixer, SOUND_FAMILY_STONE, SOUND_KIND_BREAK, 8000u, 8000u));
        }
        const int64_t start_ns = time_now_ns();
        audio_mixer_render(&mixer, out, CHUNK, 100u);
        spent_ns += time_now_ns() - start_ns;
        sum += (uint16_t)out[0];
    }
    printf("eight voices, %" PRIu32 " s of sound: %.3f ms, %.4f%% of one thread (sum %" PRIu64 ")\n", AM_BENCH_SECONDS,
           (libc_double_t)time_ns_to_ms(spent_ns),
           (libc_double_t)(time_ns_to_seconds(spent_ns) * 100.0f64 / (float64_t)AM_BENCH_SECONDS), sum);
    CHECK(mixer.played_count > 0u);
}

/* ---- Where a sound is -------------------------------------------------------------------------------------------- */

TEST(a_sound_ahead_or_behind_is_even_and_to_a_side_is_louder_in_that_ear) {
    const float64_t ear[3] = {10.0f64, 20.0f64, 64.0f64};
    const float64_t forward[3] = {1.0f64, 0.0f64, 0.0f64};
    uint16_t left = 0u, right = 0u;
    const float64_t ahead[3] = {13.0f64, 20.0f64, 64.0f64};
    audio_mixer_gains(ear, forward, ahead, &left, &right);
    CHECK_EQ_U64(left, AUDIO_MIXER_GAIN_ONE);
    CHECK_EQ_U64(right, AUDIO_MIXER_GAIN_ONE);
    const float64_t behind[3] = {7.0f64, 20.0f64, 64.0f64};
    audio_mixer_gains(ear, forward, behind, &left, &right);
    CHECK_EQ_U64(left, AUDIO_MIXER_GAIN_ONE);
    CHECK_EQ_U64(right, AUDIO_MIXER_GAIN_ONE);
    /* Looking along +x with z up, -y is on the right. */
    const float64_t on_right[3] = {10.0f64, 17.0f64, 64.0f64};
    audio_mixer_gains(ear, forward, on_right, &left, &right);
    CHECK_EQ_U64(right, AUDIO_MIXER_GAIN_ONE);
    CHECK_EQ_U64(left, AUDIO_MIXER_GAIN_ONE / 2u);
    const float64_t on_left[3] = {10.0f64, 23.0f64, 64.0f64};
    audio_mixer_gains(ear, forward, on_left, &left, &right);
    CHECK_EQ_U64(left, AUDIO_MIXER_GAIN_ONE);
    CHECK_EQ_U64(right, AUDIO_MIXER_GAIN_ONE / 2u);
    /* Right on the ear, it has no side, and straight overhead neither. */
    audio_mixer_gains(ear, forward, ear, &left, &right);
    CHECK_EQ_U64(left, AUDIO_MIXER_GAIN_ONE);
    CHECK_EQ_U64(right, AUDIO_MIXER_GAIN_ONE);
    const float64_t over[3] = {10.0f64, 20.0f64, 67.0f64};
    audio_mixer_gains(ear, forward, over, &left, &right);
    CHECK_EQ_U64(left, AUDIO_MIXER_GAIN_ONE);
    CHECK_EQ_U64(right, AUDIO_MIXER_GAIN_ONE);
}

/* What the camera calls right, which the mouse says is a smaller yaw, is what the mixer calls right. */
TEST(the_mixers_right_is_the_right_of_the_way_the_camera_looks) {
    for (uint32_t step = 0u; step < 16u; step++) {
        const float32_t yaw = -3.0f32 + (float32_t)step * 0.4f32;
        float64_t forward[3], turned_right[3];
        player_camera_direction(yaw, 0.0f32, forward);
        player_camera_direction(yaw - 1.57f32, 0.0f32, turned_right);
        const float64_t ear[3] = {0.0f64, 0.0f64, 0.0f64};
        const float64_t source[3] = {3.0f64 * turned_right[0], 3.0f64 * turned_right[1], 0.0f64};
        uint16_t left = 0u, right = 0u;
        audio_mixer_gains(ear, forward, source, &left, &right);
        CHECK(right > left);
        CHECK_EQ_U64(right, AUDIO_MIXER_GAIN_ONE);
    }
}

TEST(what_is_in_reach_loses_nothing_and_it_fades_to_nothing_at_32_blocks) {
    const float64_t ear[3] = {0.0f64, 0.0f64, 0.0f64};
    const float64_t forward[3] = {1.0f64, 0.0f64, 0.0f64};
    static const float64_t distances[] = {1.0f64, 6.0f64, 19.0f64, 32.0f64, 500.0f64};
    static const uint32_t gains[] = {AUDIO_MIXER_GAIN_ONE, AUDIO_MIXER_GAIN_ONE, AUDIO_MIXER_GAIN_ONE / 2u, 0u, 0u};
    for (uint32_t i = 0u; i < sizeof(gains) / sizeof(gains[0]); i++) {
        const float64_t source[3] = {distances[i], 0.0f64, 0.0f64};
        uint16_t left = 1u, right = 1u;
        audio_mixer_gains(ear, forward, source, &left, &right);
        CHECK_EQ_U64(left, gains[i]);
        CHECK_EQ_U64(right, gains[i]);
    }
}

TEST_MAIN()
