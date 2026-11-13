/* ctest run_audio. See README.md. The whole of sound, thread and all, to a stand-in or a file: every sample stays in memory or on disk, and nothing connects to anything real. */
#include "harness/index.h"

#include "run/audio/fixtures/fake_pulse.h"
#include "run/game/fixtures/ground.h"
#include "run/game/fixtures/report.h"

#include "audio/index.h"
#include "file/index.h"
#include "heap/index.h"
#include "log/index.h"
#include "main/index.h"
#include "simd/embed_assets/index.h"
#include "tables/blocks/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "types/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    AU_PCM_BYTES = 8u << 20u,
    AU_WAIT_MS = 15000u,
    AU_PROGRAM_TIMEOUT_MS = 240000u,
    AU_FILE_BYTES = 64u << 20u,
    AU_MATCH_FRAMES = 240u, /* 5 ms: short, as a later sound of a held button may come in on top after this */
    AU_MATCH_SKIP = 512u,
};

typedef struct au_rig_t {
    audio_t audio;              /* 0 */
    thread_topology_t topology; /* 40 */
    fake_pulse_t fake;          /* 560 */
} au_rig_t;
static_assert(sizeof(au_rig_t) == 1960);

static au_rig_t *au_rig(test_context_t *t) {
    au_rig_t *const rig = test_alloc(t, sizeof(au_rig_t));
    memset(rig, 0, sizeof(*rig));
    if (!thread_topology_read(&rig->topology)) {
        return nullptr;
    }
    rig->fake.borrowed_pcm = test_alloc(t, AU_PCM_BYTES);
    rig->fake.borrowed_packet = test_alloc(t, FAKE_PULSE_PACKET_BYTES);
    rig->fake.pcm_capacity = AU_PCM_BYTES;
    return rig;
}

/* The whole of sound, joined to a stand-in. */
static bool au_start(au_rig_t *rig, uint32_t volume) {
    if (!fake_pulse_start(&rig->fake, &rig->topology)) {
        return false;
    }
    const audio_options_t options = {.socket_path = nullptr,
                                     .cookie_path = nullptr,
                                     .file_path = nullptr,
                                     .read_fd = rig->fake.client_read,
                                     .write_fd = rig->fake.client_write,
                                     .volume_percent = volume,
                                     .retry_ms = 0u,
                                     .output = AUDIO_OUTPUT_FDS,
                                     ._pad0 = {0}};
    audio_init(&rig->audio, &options, &rig->topology);
    return true;
}

static bool au_wait_connected(const au_rig_t *rig) {
    const int64_t deadline = time_now_ns() + (int64_t)AU_WAIT_MS * 1000000;
    while (!audio_connected(&rig->audio)) {
        if (time_now_ns() > deadline) {
            return false;
        }
        time_sleep_ns(1000000);
    }
    return true;
}

static void au_finish(au_rig_t *rig) {
    audio_deinit(&rig->audio);
    fake_pulse_stop(&rig->fake);
}

static uint64_t au_frames_in(au_rig_t *rig) {
    return atomic_load_explicit(&rig->fake.pcm_bytes, memory_order_acquire) / PULSE_WIRE_FRAME_BYTES;
}

/* Where in frames the left channel first says anything, from a frame on, or UINT64_MAX. */
static uint64_t au_first_sound(au_rig_t *rig, uint64_t from, uint64_t frames) {
    const int16_t *const samples = (const int16_t *)(const void *)rig->fake.borrowed_pcm;
    for (uint64_t i = from; i < frames; i++) {
        if (samples[i * 2u] != 0 || samples[i * 2u + 1u] != 0) {
            return i;
        }
    }
    return UINT64_MAX;
}

/* Whether what is at this frame is, in both ears and whole, this sound. A sound may start with zeros, so it is tried at each place up to where the first sample that is not lands. */
static bool au_matches(const int16_t *pcm, uint64_t first_sound, const sound_blob_view_t *view,
                       const sound_blob_entry_t *entry) {
    const int16_t *const samples = sound_blob_samples(view, entry);
    uint32_t lead = 0u;
    while (lead < entry->sample_count && samples[lead] == 0) {
        lead++;
    }
    if (lead == entry->sample_count || first_sound < lead) {
        return false;
    }
    const uint64_t start = first_sound - lead;
    for (uint32_t s = 0u; s < entry->sample_count; s++) {
        if (pcm[(start + s) * 2u] != samples[s] || pcm[(start + s) * 2u + 1u] != samples[s]) {
            return false;
        }
    }
    return true;
}

TEST(with_nothing_posted_it_opens_a_stream_and_sends_only_zeros) {
    au_rig_t *const rig = au_rig(t);
    REQUIRE(rig != nullptr);
    REQUIRE(au_start(rig, 100u));
    REQUIRE(au_wait_connected(rig));
    CHECK(fake_pulse_wait_bytes(&rig->fake, 19200u, AU_WAIT_MS));
    au_finish(rig);
    const uint64_t frames = au_frames_in(rig);
    CHECK(frames >= 4800u);
    CHECK_EQ_U64(au_first_sound(rig, 0u, frames), UINT64_MAX);
}

TEST(a_sound_posted_comes_out_whole_in_both_ears_as_one_of_its_familys) {
    au_rig_t *const rig = au_rig(t);
    REQUIRE(rig != nullptr);
    REQUIRE(au_start(rig, 100u));
    REQUIRE(au_wait_connected(rig));
    REQUIRE(fake_pulse_wait_bytes(&rig->fake, 1920u, AU_WAIT_MS));
    const uint64_t posted_at = au_frames_in(rig);
    REQUIRE(audio_post(&rig->audio, SOUND_FAMILY_STONE, SOUND_KIND_BREAK, AUDIO_MIXER_GAIN_ONE, AUDIO_MIXER_GAIN_ONE));

    sound_blob_view_t view;
    REQUIRE(sound_blob_check(embed_assets_sound_blob, embed_assets_sound_blob_bytes, &view) == SOUND_BLOB_OK);
    uint32_t first_entry = 0u;
    const uint32_t count = sound_blob_find(&view, SOUND_FAMILY_STONE, SOUND_KIND_BREAK, &first_entry);
    REQUIRE(count > 0u);
    uint32_t longest = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        const uint32_t samples = view.borrowed_entries[first_entry + i].sample_count;
        longest = samples > longest ? samples : longest;
    }
    REQUIRE(fake_pulse_wait_bytes(&rig->fake, (posted_at + longest + 4800u) * PULSE_WIRE_FRAME_BYTES, AU_WAIT_MS));
    au_finish(rig);

    const uint64_t frames = au_frames_in(rig);
    const uint64_t first_sound = au_first_sound(rig, 0u, frames);
    REQUIRE(first_sound != UINT64_MAX);
    /* Soon: what is sent next is all that comes before it. A bit over 2 of these is about 10 ms. */
    CHECK(first_sound >= posted_at);
    CHECK(first_sound - posted_at <= 4800u);
    const int16_t *const pcm = (const int16_t *)(const void *)rig->fake.borrowed_pcm;
    bool matched = false;
    uint64_t end = 0u;
    for (uint32_t i = 0u; i < count && !matched; i++) {
        const sound_blob_entry_t *const entry = &view.borrowed_entries[first_entry + i];
        matched = au_matches(pcm, first_sound, &view, entry);
        end = first_sound + entry->sample_count;
    }
    CHECK(matched);
    /* And silence again, past it. */
    CHECK_EQ_U64(au_first_sound(rig, end, frames), UINT64_MAX);
    CHECK_EQ_U64(rig->audio.played_count, 1u);
}

TEST(at_volume_0_a_sound_posted_is_all_zeros) {
    au_rig_t *const rig = au_rig(t);
    REQUIRE(rig != nullptr);
    REQUIRE(au_start(rig, 0u));
    REQUIRE(au_wait_connected(rig));
    REQUIRE(audio_post(&rig->audio, SOUND_FAMILY_STONE, SOUND_KIND_BREAK, AUDIO_MIXER_GAIN_ONE, AUDIO_MIXER_GAIN_ONE));
    CHECK(fake_pulse_wait_bytes(&rig->fake, 48000u, AU_WAIT_MS));
    au_finish(rig);
    CHECK_EQ_U64(au_first_sound(rig, 0u, au_frames_in(rig)), UINT64_MAX);
    CHECK_EQ_U64(rig->audio.played_count, 1u);
}

TEST(changing_the_volume_holds_it_to_its_range_and_can_play_one_sound_to_judge_it_by) {
    au_rig_t *const rig = au_rig(t);
    REQUIRE(rig != nullptr);
    REQUIRE(au_start(rig, 100u));
    REQUIRE(au_wait_connected(rig));
    audio_volume_set(&rig->audio, 40u, false);
    CHECK_EQ_U64(audio_volume_get(&rig->audio), 40u);
    audio_volume_set(&rig->audio, 500u, false);
    CHECK_EQ_U64(audio_volume_get(&rig->audio), AUDIO_MIXER_VOLUME_MAX);
    REQUIRE(fake_pulse_wait_bytes(&rig->fake, 4800u, AU_WAIT_MS));
    const uint64_t quiet_to = au_frames_in(rig);
    CHECK_EQ_U64(au_first_sound(rig, 0u, quiet_to), UINT64_MAX);
    audio_volume_set(&rig->audio, 50u, true);
    CHECK(fake_pulse_wait_bytes(&rig->fake, (quiet_to + 9600u) * PULSE_WIRE_FRAME_BYTES, AU_WAIT_MS));
    au_finish(rig);
    CHECK(au_first_sound(rig, quiet_to, au_frames_in(rig)) != UINT64_MAX);
    CHECK_EQ_U64(rig->audio.played_count, 1u);
}

/* The one log line goes to a file for the time it takes to count them. */
typedef struct au_log_t {
    char path[64]; /* 0 */
    int32_t saved; /* 64 */
    int32_t fd;    /* 68 */
} au_log_t;
static_assert(sizeof(au_log_t) == 72);

static bool au_log_start(au_log_t *log) {
    (void)snprintf(log->path, sizeof(log->path), "/tmp/mc_c_audio_log_XXXXXX");
    log->fd = mkstemp(log->path);
    if (log->fd < 0) {
        return false;
    }
    log->saved = log_set_fd(log->fd);
    return true;
}

static uint32_t au_log_lines_with(test_context_t *t, au_log_t *log, const char *needle) {
    (void)log_set_fd(log->saved);
    (void)close(log->fd);
    char *const text = test_alloc(t, 65536u);
    uint64_t bytes = 0u;
    if (file_read_whole(log->path, text, 65535u, &bytes) != FILE_OK) {
        return UINT32_MAX;
    }
    text[bytes] = '\0';
    (void)unlink(log->path);
    uint32_t count = 0u;
    for (const char *at = strstr(text, needle); at != nullptr; at = strstr(at + 1, needle)) {
        count++;
    }
    return count;
}

TEST(with_no_server_there_is_one_log_line_however_long_and_what_is_posted_is_never_stuck) {
    au_rig_t *const rig = au_rig(t);
    REQUIRE(rig != nullptr);
    au_log_t log;
    REQUIRE(au_log_start(&log));
    const audio_options_t options = {.socket_path = "/nonexistent/mc_c_audio/native",
                                     .cookie_path = nullptr,
                                     .file_path = nullptr,
                                     .read_fd = -1,
                                     .write_fd = -1,
                                     .volume_percent = 100u,
                                     .retry_ms = 5u,
                                     .output = AUDIO_OUTPUT_SERVER,
                                     ._pad0 = {0}};
    audio_init(&rig->audio, &options, &rig->topology);
    /* More than the ring holds, slowly enough for it to throw them away as they come, while it looks again and again. */
    uint32_t posted = 0u;
    for (uint32_t i = 0u; i < 3u * AUDIO_RING_EVENTS; i++) {
        posted += audio_post(&rig->audio, SOUND_FAMILY_STONE, SOUND_KIND_HIT, 1u, 1u) ? 1u : 0u;
        time_sleep_ns(1000000);
    }
    time_sleep_ns(100000000);
    CHECK_EQ_U64(posted, 3u * AUDIO_RING_EVENTS);
    CHECK(!audio_connected(&rig->audio));
    audio_deinit(&rig->audio);
    CHECK_EQ_U64(au_log_lines_with(t, &log, "no sound server"), 1u);
    /* Nothing was heard, and nothing queued up to be. */
    CHECK_EQ_U64(rig->audio.played_count, 0u);
    CHECK_EQ_U64(rig->audio.dropped_count, 0u);
}

TEST(a_socket_on_another_machine_is_silence_too) {
    au_rig_t *const rig = au_rig(t);
    REQUIRE(rig != nullptr);
    const char *const before = getenv("PULSE_SERVER");
    REQUIRE(before == nullptr || setenv("PULSE_SERVER_SAVED_BY_TEST", before, 1) == 0);
    REQUIRE(setenv("PULSE_SERVER", "tcp:192.0.2.1:4713", 1) == 0);
    const audio_options_t options = {.socket_path = nullptr,
                                     .cookie_path = nullptr,
                                     .file_path = nullptr,
                                     .read_fd = -1,
                                     .write_fd = -1,
                                     .volume_percent = 100u,
                                     .retry_ms = 0u,
                                     .output = AUDIO_OUTPUT_SERVER,
                                     ._pad0 = {0}};
    audio_init(&rig->audio, &options, &rig->topology);
    if (before != nullptr) {
        REQUIRE(setenv("PULSE_SERVER", getenv("PULSE_SERVER_SAVED_BY_TEST"), 1) == 0);
    } else {
        REQUIRE(unsetenv("PULSE_SERVER") == 0);
    }
    CHECK(rig->audio.borrowed_state == nullptr);
    CHECK(!audio_post(&rig->audio, SOUND_FAMILY_STONE, SOUND_KIND_HIT, 1u, 1u));
    audio_deinit(&rig->audio);
}

TEST(a_server_going_away_is_silence_and_one_line_and_the_game_goes_on) {
    au_rig_t *const rig = au_rig(t);
    REQUIRE(rig != nullptr);
    au_log_t log;
    REQUIRE(au_log_start(&log));
    rig->fake.ending = FAKE_PULSE_KILLS_STREAM;
    REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
    const audio_options_t options = {.socket_path = nullptr,
                                     .cookie_path = nullptr,
                                     .file_path = nullptr,
                                     .read_fd = rig->fake.client_read,
                                     .write_fd = rig->fake.client_write,
                                     .volume_percent = 100u,
                                     .retry_ms = 5u,
                                     .output = AUDIO_OUTPUT_FDS,
                                     ._pad0 = {0}};
    audio_init(&rig->audio, &options, &rig->topology);
    const int64_t deadline = time_now_ns() + (int64_t)AU_WAIT_MS * 1000000;
    while (atomic_load_explicit(&rig->fake.streams, memory_order_acquire) == 0u && time_now_ns() < deadline) {
        time_sleep_ns(1000000);
    }
    CHECK_EQ_U64(atomic_load_explicit(&rig->fake.streams, memory_order_acquire), 1u);
    time_sleep_ns(100000000);
    /* It goes back to looking, and a post goes nowhere with no harm. */
    for (uint32_t i = 0u; i < 100u; i++) {
        (void)audio_post(&rig->audio, SOUND_FAMILY_STONE, SOUND_KIND_HIT, 1u, 1u);
        time_sleep_ns(1000000);
    }
    audio_deinit(&rig->audio);
    fake_pulse_stop(&rig->fake);
    CHECK_EQ_U64(au_log_lines_with(t, &log, "no sound server"), 1u);
}

TEST(a_tests_private_display_is_told_from_a_persons) {
    CHECK(!audio_display_is_private(nullptr));
    CHECK(!audio_display_is_private(""));
    CHECK(!audio_display_is_private(":0"));
    CHECK(!audio_display_is_private(":0.0"));
    CHECK(!audio_display_is_private(":1"));
    CHECK(!audio_display_is_private(":79"));
    CHECK(audio_display_is_private(":80"));
    CHECK(audio_display_is_private(":99"));
    CHECK(audio_display_is_private(":99.0"));
    CHECK(audio_display_is_private(":1001"));
    CHECK(!audio_display_is_private("othermachine:99"));
}

TEST(with_no_output_nothing_starts_and_a_zeroed_one_can_be_let_go_of) {
    au_rig_t *const rig = au_rig(t);
    REQUIRE(rig != nullptr);
    audio_deinit(&rig->audio);
    heap_counters_t before, after;
    heap_counters_read(&before);
    const audio_options_t options = {.socket_path = nullptr,
                                     .cookie_path = nullptr,
                                     .file_path = nullptr,
                                     .read_fd = -1,
                                     .write_fd = -1,
                                     .volume_percent = 100u,
                                     .retry_ms = 0u,
                                     .output = AUDIO_OUTPUT_NONE,
                                     ._pad0 = {0}};
    audio_init(&rig->audio, &options, &rig->topology);
    CHECK(!audio_post(&rig->audio, SOUND_FAMILY_STONE, SOUND_KIND_HIT, 1u, 1u));
    CHECK(!audio_connected(&rig->audio));
    audio_deinit(&rig->audio);
    heap_counters_read(&after);
    CHECK_MEM_EQ(&before, &after, sizeof(before));
}

TEST(a_full_ring_refuses_counts_and_takes_more_once_there_is_room) {
    au_rig_t *const rig = au_rig(t);
    REQUIRE(rig != nullptr);
    /* No thread: as if it had stopped looking. */
    const uintptr_t start = (uintptr_t)test_alloc(t, sizeof(audio_state_t) + 64u);
    audio_state_t *const state = (audio_state_t *)((start + 63u) & ~(uintptr_t)63u);
    rig->audio.borrowed_state = state;
    for (uint32_t i = 0u; i < AUDIO_RING_EVENTS; i++) {
        CHECK(audio_post(&rig->audio, (uint32_t)SOUND_FAMILY_WOOD, (uint32_t)SOUND_KIND_PLACE, (uint16_t)i, 5u));
    }
    CHECK(!audio_post(&rig->audio, SOUND_FAMILY_WOOD, SOUND_KIND_PLACE, 1u, 1u));
    CHECK_EQ_U64(state->ring.dropped_count, 1u);
    atomic_store(&state->ring.tail, 1u);
    CHECK(audio_post(&rig->audio, SOUND_FAMILY_WOOD, SOUND_KIND_PLACE, 7u, 1u));
    CHECK_EQ_U64(state->ring.events[0].gain_left, 7u);
    CHECK_EQ_U64(state->ring.events[1].gain_left, 1u);
    CHECK_EQ_U64(state->ring.events[2].gain_left, 2u);
    CHECK_EQ_U64(state->ring.events[0].family, SOUND_FAMILY_WOOD);
    CHECK_EQ_U64(state->ring.events[0].kind, SOUND_KIND_PLACE);
}

TEST(what_it_takes_is_given_back_and_a_posting_touches_no_heap) {
    au_rig_t *const rig = au_rig(t);
    REQUIRE(rig != nullptr);
    heap_counters_t start;
    heap_counters_read(&start);
    REQUIRE(au_start(rig, 100u));
    REQUIRE(au_wait_connected(rig));
    heap_counters_t before, after;
    heap_counters_read(&before);
    for (uint32_t i = 0u; i < 50u; i++) {
        (void)audio_post(&rig->audio, SOUND_FAMILY_DIRT, SOUND_KIND_HIT, 1000u, 1000u);
        time_sleep_ns(1000000);
    }
    heap_counters_read(&after);
    CHECK_MEM_EQ(&before, &after, sizeof(before));
    au_finish(rig);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, start.live_bytes);
}

TEST(to_a_file_it_keeps_what_would_have_played_at_the_speed_it_would_play) {
    au_rig_t *const rig = au_rig(t);
    REQUIRE(rig != nullptr);
    char path[] = "/tmp/mc_c_audio_file_XXXXXX";
    const int32_t fd = mkstemp(path);
    REQUIRE(fd >= 0);
    (void)close(fd);
    const audio_options_t options = {.socket_path = nullptr,
                                     .cookie_path = nullptr,
                                     .file_path = path,
                                     .read_fd = -1,
                                     .write_fd = -1,
                                     .volume_percent = 100u,
                                     .retry_ms = 0u,
                                     .output = AUDIO_OUTPUT_FILE,
                                     ._pad0 = {0}};
    const int64_t started_ns = time_now_ns();
    audio_init(&rig->audio, &options, &rig->topology);
    CHECK(!audio_connected(&rig->audio));
    REQUIRE(audio_post(&rig->audio, SOUND_FAMILY_GLASS, SOUND_KIND_BREAK, AUDIO_MIXER_GAIN_ONE, AUDIO_MIXER_GAIN_ONE));
    time_sleep_ns(400000000);
    audio_deinit(&rig->audio);
    const float64_t seconds = time_ns_to_seconds(time_now_ns() - started_ns);
    uint8_t *const bytes = test_alloc(t, AU_FILE_BYTES);
    uint64_t size = 0u;
    REQUIRE(file_read_whole(path, bytes, AU_FILE_BYTES, &size) == FILE_OK);
    (void)unlink(path);
    const float64_t frames = (float64_t)(size / PULSE_WIRE_FRAME_BYTES);
    CHECK_NEAR(frames / (float64_t)PULSE_WIRE_RATE, seconds, 0.2f64);
    bool any = false;
    for (uint64_t i = 0u; i < size; i++) {
        any = any || bytes[i] != 0u;
    }
    CHECK(any);
}

TEST(a_file_that_cannot_be_written_is_no_more_than_silence) {
    au_rig_t *const rig = au_rig(t);
    REQUIRE(rig != nullptr);
    const audio_options_t options = {.socket_path = nullptr,
                                     .cookie_path = nullptr,
                                     .file_path = "/nonexistent/mc_c_audio/file",
                                     .read_fd = -1,
                                     .write_fd = -1,
                                     .volume_percent = 100u,
                                     .retry_ms = 0u,
                                     .output = AUDIO_OUTPUT_FILE,
                                     ._pad0 = {0}};
    audio_init(&rig->audio, &options, &rig->topology);
    CHECK(rig->audio.borrowed_state == nullptr);
    CHECK(!audio_post(&rig->audio, SOUND_FAMILY_STONE, SOUND_KIND_HIT, 1u, 1u));
    audio_deinit(&rig->audio);
}

/* Never by ctest: only by hand, as in the plan's Verification. The real thread on the real server, for three seconds, with nothing posted and the volume at 0, so that every
 * sample is a zero twice over. Unmuted, as the game's own stream is, to see that it stays up and that the server is kept fed. */
TEST(with_its_flag_the_real_server_is_sent_three_seconds_of_silence_by_the_real_thread) {
    if (t->arg_count < 2 || strcmp(t->borrowed_args[1], "--real-server-silence") != 0) {
        return;
    }
    au_rig_t *const rig = au_rig(t);
    REQUIRE(rig != nullptr);
    const audio_options_t options = {.socket_path = nullptr,
                                     .cookie_path = nullptr,
                                     .file_path = nullptr,
                                     .read_fd = -1,
                                     .write_fd = -1,
                                     .volume_percent = 0u,
                                     .retry_ms = 0u,
                                     .output = AUDIO_OUTPUT_SERVER,
                                     ._pad0 = {0}};
    audio_init(&rig->audio, &options, &rig->topology);
    REQUIRE(rig->audio.borrowed_state != nullptr);
    REQUIRE(au_wait_connected(rig));
    time_sleep_ns(3000000000ll);
    CHECK(audio_connected(&rig->audio));
    const audio_pulse_t *const pulse = &rig->audio.borrowed_state->pulse;
    printf("sent %" PRIu64 " bytes in %" PRIu64 " requests: %" PRIu64 " underflows, %" PRIu64
           " overflows, tlength %" PRIu32 "\n",
           pulse->sent_bytes, pulse->request_count, pulse->underflow_count, pulse->overflow_count,
           pulse->stream.tlength);
    CHECK(pulse->sent_bytes > 2u * PULSE_WIRE_BYTES_PER_SECOND);
    CHECK_EQ_U64(rig->audio.borrowed_state->mixer.played_count, 0u);
    audio_deinit(&rig->audio);
}

/* ---- The program ------------------------------------------------------------------------------------------------- */

/* As run_player_interaction's script: looks straight down, mines through grass to dirt, mines a wall of the hole, and places one back. */
static const char au_mine_script[] = "1 key w down\n"
                                     "2 key w up\n"
                                     "22 move 0 500\n"
                                     "31 button left down\n"
                                     "75 button left up\n"
                                     "90 move 1256 -620\n"
                                     "92 button left down\n"
                                     "115 button left up\n"
                                     "118 button right down\n"
                                     "119 button right up\n"
                                     "125 end\n";

/* The dig script, after a /tp to the nearest even ground of seed 11 (tests/run/game/fixtures/ground.h): what lies at the start
 * is the land's, and since plan 007 phase 04 it is the edge of grass and sand, so the top beside the hole was sand. */
static bool au_dig_script_write(test_context_t *t, const char *path, const char *dig) {
    terrain_t *const terrain = test_alloc(t, sizeof(terrain_t));
    if (!CHECK(terrain_init(terrain, 11u))) {
        return false;
    }
    int32_t x = 0, y = 0;
    const bool found = ground_even_find(terrain, &x, &y);
    terrain_deinit(terrain);
    if (!CHECK(found)) {
        return false;
    }
    printf("  the dig is at %" PRId32 ", %" PRId32 "\n", x, y);
    enum : uint32_t { BYTES = 4096u };
    char *const text = test_alloc(t, BYTES);
    uint64_t length = 0u;
    uint32_t tick = 0u;
    ground_tp_keys(text, BYTES, &length, x, y, &tick);
    ground_script_after(text, BYTES, &length, dig, tick);
    return CHECK_EQ_U64(file_write_atomic(path, text, length), FILE_OK);
}

/* Which sound the frames from here on are, in the left ear, as its gain leaves the shape alone: the one whose start is most like them, from 1 down, and how like. A
 * quiet first sample rounds to nothing, so what is heard first may be a little way into the sound. */
static const sound_blob_entry_t *au_most_like(const int16_t *stereo, const sound_blob_view_t *view,
                                              float64_t *out_likeness) {
    const sound_blob_entry_t *best = nullptr;
    *out_likeness = -1.0f64;
    float64_t heard_energy = 0.0f64;
    for (uint32_t i = 0u; i < AU_MATCH_FRAMES; i++) {
        heard_energy += (float64_t)stereo[i * 2u] * (float64_t)stereo[i * 2u];
    }
    for (uint32_t e = 0u; e < view->sound_count; e++) {
        const sound_blob_entry_t *const entry = &view->borrowed_entries[e];
        const int16_t *const samples = sound_blob_samples(view, entry);
        for (uint32_t skip = 0u; skip < AU_MATCH_SKIP && skip + AU_MATCH_FRAMES <= entry->sample_count; skip++) {
            float64_t both = 0.0f64, energy = 0.0f64;
            for (uint32_t i = 0u; i < AU_MATCH_FRAMES; i++) {
                both += (float64_t)stereo[i * 2u] * (float64_t)samples[skip + i];
                energy += (float64_t)samples[skip + i] * (float64_t)samples[skip + i];
            }
            const float64_t likeness =
                energy > 0.0f64 && heard_energy > 0.0f64 ? both / sqrt(energy * heard_energy) : 0.0f64;
            if (likeness > *out_likeness) {
                *out_likeness = likeness;
                best = entry;
            }
        }
    }
    return best;
}

/* stdin from /dev/null, as the harness always does, and --headless, so this can make no sound, whatever else is wrong. The file proves it reached the mixer. */
TEST(the_program_with_its_sound_in_a_file_mines_and_what_would_have_played_is_there) {
    if (t->arg_count < 1) {
        printf("no program to run\n");
        CHECK(false);
        return;
    }
    char dir[] = "/tmp/mc_c_audio_game_XXXXXX";
    REQUIRE(mkdtemp(dir) != nullptr);
    char script[128], sound[128];
    (void)snprintf(script, sizeof(script), "%s/mine.script", dir);
    (void)snprintf(sound, sizeof(sound), "%s/sound.raw", dir);
    REQUIRE(au_dig_script_write(t, script, au_mine_script));
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--test-world",
                          (char *)"--seed",
                          (char *)"11",
                          (char *)"--walk",
                          (char *)"--render-distance",
                          (char *)"4",
                          (char *)"--script",
                          (char *)script,
                          (char *)"--sound-file",
                          (char *)sound,
                          (char *)"--width",
                          (char *)"160",
                          (char *)"--height",
                          (char *)"90",
                          nullptr};
    test_process_t run;
    test_process_run(t, argv, AU_PROGRAM_TIMEOUT_MS, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, "the player mined 3 blocks and placed 1");
    CHECK_CONTAINS(run.output, "0 bytes live");
    uint8_t *const bytes = test_alloc(t, AU_FILE_BYTES);
    uint64_t size = 0u;
    REQUIRE(file_read_whole(sound, bytes, AU_FILE_BYTES, &size) == FILE_OK);
    (void)unlink(sound);
    (void)unlink(script);
    (void)rmdir(dir);
    const int16_t *const samples = (const int16_t *)(const void *)bytes;
    uint64_t loud = 0u, differ = 0u, first_loud = UINT64_MAX;
    for (uint64_t i = 0u; i + 1u < size / sizeof(int16_t); i += 2u) {
        const bool heard = samples[i] != 0 || samples[i + 1u] != 0;
        loud += heard ? 1u : 0u;
        first_loud = heard && first_loud == UINT64_MAX ? i : first_loud;
        differ += samples[i] != samples[i + 1u] ? 1u : 0u;
    }
    printf("%" PRIu64 " frames, %" PRIu64 " not silent, %" PRIu64 " with the ears apart\n", size / 4u, loud, differ);
    CHECK(size >= 4u);
    REQUIRE(loud > 0u);
    REQUIRE(first_loud + AU_MATCH_FRAMES * 2u <= size / sizeof(int16_t));
    /* Heard, and heard as what was dug. The ground at seed 11's spawn is whatever its biome lays, and it moves with each plan
     * that moves the land (grass, then podzol in plan 006 phase 03, then sand on plan 007 phase 03's land, then the edge of
     * grass and sand on phase 04's), so the dig is on the nearest even ground (au_dig_script_write), and the test reads which
     * block the report says was last mined, the top of the ground beside the hole, as the dig tests do, and wants its sound
     * family. This run's ticks go faster than the file's 10 ms, so a few hits can start on one frame, and their sum is
     * only so like any one of them. Which of the family it is, or whether a hit or a break of the same recording, it cannot
     * say. */
    report_mined_t mined;
    REQUIRE(report_mined_read(run.output, &mined));
    uint32_t dug = BLOCK_COUNT;
    for (uint32_t b = 0u; b < BLOCK_COUNT; b++) {
        dug = strcmp(block_defs[b].name, mined.last) == 0 ? b : dug;
    }
    printf("the last mined was %s\n", mined.last);
    REQUIRE(dug < BLOCK_COUNT);
    sound_blob_view_t view;
    REQUIRE(sound_blob_check(embed_assets_sound_blob, embed_assets_sound_blob_bytes, &view) == SOUND_BLOB_OK);
    float64_t likeness = 0.0f64;
    const sound_blob_entry_t *const best = au_most_like(&samples[first_loud], &view, &likeness);
    REQUIRE(best != nullptr);
    printf("first sound: family %" PRIu32 ", kind %" PRIu32 ", likeness %" PRId64 " in 1000\n", (uint32_t)best->family,
           (uint32_t)best->kind, (int64_t)(likeness * 1000.0f64));
    CHECK_EQ_U64(best->family, block_defs[dug].sound);
    CHECK(likeness > 0.5f64);
}

TEST_MAIN()
