/* ctest run_audio_pulse. See README.md. The client against a stand-in over pipes, whose samples stay in memory. */
#include "harness/index.h"

#include "run/audio/fixtures/fake_pulse.h"

#include "arena/index.h"
#include "audio/pulse/index.h"
#include "file/index.h"
#include "heap/index.h"
#include "thread/index.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    AP_PCM_BYTES = 1u << 20u,
    AP_ARENA_BYTES = AUDIO_PULSE_ARENA_BYTES,
    AP_WAIT_MS = 5000u,
    AP_REAL_SECOND_BYTES = PULSE_WIRE_BYTES_PER_SECOND,
};

static const char AP_REAL_SERVER_FLAG[] = "--real-server-silence";

typedef struct ap_rig_t {
    thread_topology_t topology; /* 0 */
    fake_pulse_t fake;          /* 520 */
    arena_t arena;              /* 1920 */
    audio_pulse_t pulse;        /* 1984 */
    _Atomic uint32_t stop;      /* 2296 */
    uint8_t _pad0[4];           /* 2300 */
} ap_rig_t;
static_assert(sizeof(ap_rig_t) == 2304);

/* A client with its buffers, and a stand-in not yet started: a test says what it should do first. */
static ap_rig_t *ap_rig(test_context_t *t) {
    /* test_alloc lines up on 16, and this on a cache line. */
    const uintptr_t start = (uintptr_t)test_alloc(t, sizeof(ap_rig_t) + 64u);
    ap_rig_t *const rig = (ap_rig_t *)((start + 63u) & ~(uintptr_t)63u);
    memset(rig, 0, sizeof(*rig));
    if (!thread_topology_read(&rig->topology)) {
        return nullptr;
    }
    rig->fake.borrowed_pcm = test_alloc(t, AP_PCM_BYTES);
    rig->fake.borrowed_packet = test_alloc(t, FAKE_PULSE_PACKET_BYTES);
    rig->fake.pcm_capacity = AP_PCM_BYTES;
    arena_init(&rig->arena, "audio test", test_alloc(t, AP_ARENA_BYTES), AP_ARENA_BYTES);
    audio_pulse_start(&rig->pulse, &rig->arena);
    return rig;
}

static audio_pulse_open_t ap_open(const ap_rig_t *rig, const char *cookie, bool muted) {
    return (audio_pulse_open_t){.cookie_path = cookie,
                                .name = "ap test",
                                .borrowed_stop = (_Atomic uint32_t *)&rig->stop,
                                .muted = muted ? 1u : 0u,
                                ._pad0 = {0}};
}

static bool ap_attach(ap_rig_t *rig, const char *cookie, bool muted) {
    const audio_pulse_open_t open = ap_open(rig, cookie, muted);
    return audio_pulse_attach(&rig->pulse, rig->fake.client_read, rig->fake.client_write, &open);
}

/* Takes what it says for as long as it takes for this to come true. */
static bool ap_service_until_owed(ap_rig_t *rig, uint32_t frames) {
    const int64_t deadline = time_now_ns() + (int64_t)AP_WAIT_MS * 1000000;
    while (audio_pulse_frames_owed(&rig->pulse) < frames) {
        if (time_now_ns() > deadline || !audio_pulse_service(&rig->pulse, 5)) {
            return false;
        }
    }
    return true;
}

/* What sets off a stand-in's ending. */
static bool ap_send_what_is_owed(ap_rig_t *rig) {
    const uint32_t frames = audio_pulse_frames_owed(&rig->pulse);
    memset(audio_pulse_samples(&rig->pulse), 0, frames * PULSE_WIRE_FRAME_BYTES);
    return audio_pulse_send(&rig->pulse, frames);
}

/* False once the connection has said it is over, and why it did. */
static bool ap_service_until_over(ap_rig_t *rig) {
    const int64_t deadline = time_now_ns() + (int64_t)AP_WAIT_MS * 1000000;
    while (time_now_ns() < deadline) {
        if (!audio_pulse_service(&rig->pulse, 5)) {
            return true;
        }
    }
    return false;
}

TEST(hello_opens_a_stream_on_the_servers_numbers_and_asks_for_what_was_decided) {
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
    REQUIRE(ap_attach(rig, nullptr, false));
    CHECK_STR_EQ(rig->pulse.why, "");
    CHECK_EQ_U64(rig->pulse.stream.channel, FAKE_PULSE_CHANNEL);
    CHECK_EQ_U64(rig->pulse.stream.tlength, FAKE_PULSE_TLENGTH);
    CHECK_EQ_U64(rig->pulse.stream.minreq, FAKE_PULSE_MINREQ);
    CHECK_EQ_U64(audio_pulse_frames_owed(&rig->pulse), FAKE_PULSE_OWED / PULSE_WIRE_FRAME_BYTES);
    CHECK_EQ_U64(rig->fake.client_name_seen, 1u);
    uint8_t expected[PULSE_WIRE_COMMAND_MAX_BYTES];
    const uint32_t expected_bytes =
        pulse_wire_create_stream(expected, sizeof(expected), 3u, "ap test", AUDIO_PULSE_TLENGTH_BYTES, false);
    CHECK_EQ_U64(rig->fake.create_bytes, expected_bytes);
    CHECK_MEM_EQ(rig->fake.create, expected, expected_bytes);
    audio_pulse_close(&rig->pulse);
    fake_pulse_stop(&rig->fake);
}

TEST(muted_is_said_to_the_server) {
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
    REQUIRE(ap_attach(rig, nullptr, true));
    uint8_t expected[PULSE_WIRE_COMMAND_MAX_BYTES];
    const uint32_t expected_bytes =
        pulse_wire_create_stream(expected, sizeof(expected), 3u, "ap test", AUDIO_PULSE_TLENGTH_BYTES, true);
    CHECK_EQ_U64(rig->fake.create_bytes, expected_bytes);
    CHECK_MEM_EQ(rig->fake.create, expected, expected_bytes);
    audio_pulse_close(&rig->pulse);
    fake_pulse_stop(&rig->fake);
}

TEST(the_cookie_file_is_sent_whole_and_with_none_or_a_short_one_zeros_are) {
    char path[] = "/tmp/mc_c_audio_cookie_XXXXXX";
    const int32_t fd = mkstemp(path);
    REQUIRE(fd >= 0);
    (void)close(fd);
    uint8_t cookie[PULSE_WIRE_COOKIE_BYTES];
    for (uint32_t i = 0u; i < sizeof(cookie); i++) {
        cookie[i] = (uint8_t)(i ^ 0x5Au);
    }
    REQUIRE(file_write_atomic(path, cookie, sizeof(cookie)) == FILE_OK);
    const uint8_t zeros[PULSE_WIRE_COOKIE_BYTES] = {0};
    for (uint32_t pass = 0u; pass < 3u; pass++) {
        ap_rig_t *const rig = ap_rig(t);
        REQUIRE(rig != nullptr);
        REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
        if (pass == 1u) {
            REQUIRE(file_write_atomic(path, cookie, sizeof(cookie) / 2u) == FILE_OK);
        }
        REQUIRE(ap_attach(rig, pass == 2u ? "/nonexistent/mc_c_cookie" : path, false));
        CHECK_MEM_EQ(rig->fake.cookie, pass == 0u ? cookie : zeros, sizeof(cookie));
        /* And it is not left in a buffer. */
        bool cleared = false;
        for (uint32_t i = 0u; i + 8u <= PULSE_WIRE_COMMAND_MAX_BYTES && !cleared; i++) {
            cleared = memcmp(rig->pulse.borrowed_command + i, cookie, 8u) != 0;
        }
        CHECK(cleared);
        audio_pulse_close(&rig->pulse);
        fake_pulse_stop(&rig->fake);
    }
    (void)unlink(path);
}

TEST(what_is_sent_arrives_exactly_and_what_it_asks_for_is_counted) {
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
    REQUIRE(ap_attach(rig, nullptr, false));
    const uint32_t frames = audio_pulse_frames_owed(&rig->pulse);
    REQUIRE(frames == FAKE_PULSE_OWED / PULSE_WIRE_FRAME_BYTES);
    int16_t *const samples = audio_pulse_samples(&rig->pulse);
    for (uint32_t i = 0u; i < frames * 2u; i++) {
        samples[i] = (int16_t)(i * 37u);
    }
    REQUIRE(audio_pulse_send(&rig->pulse, frames));
    CHECK_EQ_U64(audio_pulse_frames_owed(&rig->pulse), 0u);
    CHECK_EQ_U64(rig->pulse.sent_bytes, FAKE_PULSE_OWED);
    REQUIRE(fake_pulse_wait_bytes(&rig->fake, FAKE_PULSE_OWED, AP_WAIT_MS));
    CHECK_MEM_EQ(rig->fake.borrowed_pcm, samples, FAKE_PULSE_OWED);
    /* It asks for as much again. */
    REQUIRE(ap_service_until_owed(rig, frames));
    CHECK_EQ_U64(rig->pulse.request_count, 1u);
    CHECK_EQ_U64(audio_pulse_frames_owed(&rig->pulse), frames);
    audio_pulse_close(&rig->pulse);
    fake_pulse_stop(&rig->fake);
}

TEST(a_big_request_is_sent_in_pieces_none_over_the_buffer) {
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    rig->pulse.owed = AUDIO_PULSE_PCM_FRAMES * PULSE_WIRE_FRAME_BYTES * 3u + 2u;
    CHECK_EQ_U64(audio_pulse_frames_owed(&rig->pulse), AUDIO_PULSE_PCM_FRAMES);
    rig->pulse.owed = 7u;
    CHECK_EQ_U64(audio_pulse_frames_owed(&rig->pulse), 1u);
    rig->pulse.owed = 3u;
    CHECK_EQ_U64(audio_pulse_frames_owed(&rig->pulse), 0u);
}

TEST(notices_it_has_no_use_for_change_nothing) {
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    rig->fake.notices = 1u;
    REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
    REQUIRE(ap_attach(rig, nullptr, false));
    const int64_t deadline = time_now_ns() + (int64_t)AP_WAIT_MS * 1000000;
    while (rig->pulse.overflow_count == 0u && time_now_ns() < deadline) {
        REQUIRE(audio_pulse_service(&rig->pulse, 5));
    }
    CHECK_EQ_U64(rig->pulse.underflow_count, 1u);
    CHECK_EQ_U64(rig->pulse.overflow_count, 1u);
    CHECK_EQ_U64(audio_pulse_frames_owed(&rig->pulse), FAKE_PULSE_OWED / PULSE_WIRE_FRAME_BYTES);
    audio_pulse_close(&rig->pulse);
    fake_pulse_stop(&rig->fake);
}

TEST(a_server_that_refuses_is_a_failure_that_says_so) {
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    rig->fake.refuse_auth = 1u;
    REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
    CHECK(!ap_attach(rig, nullptr, false));
    CHECK_CONTAINS(rig->pulse.why, "refused");
    CHECK_EQ_U64(rig->pulse.attached, 0u);
    fake_pulse_stop(&rig->fake);
}

TEST(a_server_that_never_answers_gives_up_when_told_to) {
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    rig->fake.silent = 1u;
    REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
    atomic_store(&rig->stop, 1u);
    const int64_t started_ns = time_now_ns();
    CHECK(!ap_attach(rig, nullptr, false));
    CHECK(time_now_ns() - started_ns < 500000000ll);
    CHECK_CONTAINS(rig->pulse.why, "stopped");
    fake_pulse_stop(&rig->fake);
}

TEST(a_server_that_never_answers_is_given_up_on_in_the_end) {
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    rig->fake.silent = 1u;
    REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
    const int64_t started_ns = time_now_ns();
    CHECK(!ap_attach(rig, nullptr, false));
    CHECK(time_now_ns() - started_ns < 5000000000ll);
    CHECK_CONTAINS(rig->pulse.why, "did not answer");
    fake_pulse_stop(&rig->fake);
}

TEST(a_server_going_away_ends_it_and_says_so_with_no_signal) {
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    rig->fake.ending = FAKE_PULSE_CLOSES;
    REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
    REQUIRE(ap_attach(rig, nullptr, false));
    REQUIRE(ap_send_what_is_owed(rig));
    CHECK(ap_service_until_over(rig));
    CHECK_CONTAINS(rig->pulse.why, "closed");
    audio_pulse_close(&rig->pulse);
    fake_pulse_stop(&rig->fake);
}

TEST(sending_to_a_server_that_went_away_ends_it_too) {
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    rig->fake.ending = FAKE_PULSE_CLOSES;
    REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
    REQUIRE(ap_attach(rig, nullptr, false));
    REQUIRE(ap_send_what_is_owed(rig));
    thread_join(&rig->fake.thread); /* so that it is really gone */
    CHECK(!audio_pulse_send(&rig->pulse, 16u));
    CHECK_CONTAINS(rig->pulse.why, "sending");
    audio_pulse_close(&rig->pulse);
    fake_pulse_stop(&rig->fake);
}

TEST(a_stream_taken_away_ends_it) {
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    rig->fake.ending = FAKE_PULSE_KILLS_STREAM;
    REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
    REQUIRE(ap_attach(rig, nullptr, false));
    REQUIRE(ap_send_what_is_owed(rig));
    CHECK(ap_service_until_over(rig));
    CHECK_CONTAINS(rig->pulse.why, "took the stream away");
    audio_pulse_close(&rig->pulse);
    fake_pulse_stop(&rig->fake);
}

TEST(nonsense_ends_it_and_so_does_a_packet_too_long_to_be_real) {
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        ap_rig_t *const rig = ap_rig(t);
        REQUIRE(rig != nullptr);
        rig->fake.ending = pass == 0u ? FAKE_PULSE_NONSENSE : FAKE_PULSE_TOO_LONG;
        REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
        REQUIRE(ap_attach(rig, nullptr, false));
        REQUIRE(ap_send_what_is_owed(rig));
        CHECK(ap_service_until_over(rig));
        CHECK_CONTAINS(rig->pulse.why, pass == 0u ? "makes no sense" : "too long");
        audio_pulse_close(&rig->pulse);
        fake_pulse_stop(&rig->fake);
    }
}

TEST(no_server_at_the_socket_is_a_plain_failure_that_leaves_nothing_open) {
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    const audio_pulse_open_t open = ap_open(rig, nullptr, false);
    CHECK(!audio_pulse_connect(&rig->pulse, "/nonexistent/mc_c_audio/native", &open));
    CHECK_CONTAINS(rig->pulse.why, "/nonexistent/mc_c_audio/native");
    CHECK_CONTAINS(rig->pulse.why, "No such file");
    CHECK_EQ_U64(rig->pulse.attached, 0u);
    CHECK_EQ_I64(rig->pulse.read_fd, -1);
    /* Too long to be a socket's path is the same. */
    char path[256];
    memset(path, 'a', sizeof(path) - 1u);
    path[0] = '/';
    path[sizeof(path) - 1u] = '\0';
    CHECK(!audio_pulse_connect(&rig->pulse, path, &open));
    CHECK_CONTAINS(rig->pulse.why, "too long");
    /* And closing what is not open does nothing. */
    audio_pulse_close(&rig->pulse);
}

TEST(sending_and_taking_touch_no_heap) {
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    REQUIRE(fake_pulse_start(&rig->fake, &rig->topology));
    REQUIRE(ap_attach(rig, nullptr, false));
    heap_counters_t before, after;
    heap_counters_read(&before);
    for (uint32_t i = 0u; i < 5u; i++) {
        REQUIRE(ap_service_until_owed(rig, 16u));
        const uint32_t frames = audio_pulse_frames_owed(&rig->pulse);
        memset(audio_pulse_samples(&rig->pulse), 0, frames * PULSE_WIRE_FRAME_BYTES);
        REQUIRE(audio_pulse_send(&rig->pulse, frames));
    }
    heap_counters_read(&after);
    CHECK_MEM_EQ(&before, &after, sizeof(before));
    audio_pulse_close(&rig->pulse);
    fake_pulse_stop(&rig->fake);
}

/* Never by ctest, which gives no argument: only by hand, as in the plan's Verification, to show the real client talks to the real server. Muted at the server, and every
 * buffer is looked at before it goes, so it can only ever carry zeros. */
TEST(with_its_flag_the_real_server_is_sent_one_second_of_silence_and_nothing_else) {
    if (t->arg_count < 1 || strcmp(t->borrowed_args[0], AP_REAL_SERVER_FLAG) != 0) {
        return;
    }
    ap_rig_t *const rig = ap_rig(t);
    REQUIRE(rig != nullptr);
    char path[AUDIO_PULSE_WHY_BYTES];
    REQUIRE(pulse_wire_socket_path(path, sizeof(path), getenv("PULSE_SERVER"), getenv("XDG_RUNTIME_DIR"), getuid()));
    char cookie[AUDIO_PULSE_WHY_BYTES];
    const bool has_cookie = pulse_wire_cookie_path(cookie, sizeof(cookie), getenv("PULSE_COOKIE"),
                                                   getenv("XDG_CONFIG_HOME"), getenv("HOME"));
    const audio_pulse_open_t open = ap_open(rig, has_cookie ? cookie : nullptr, true);
    if (!audio_pulse_connect(&rig->pulse, path, &open)) {
        printf("no real server: %s\n", rig->pulse.why);
        CHECK(false);
        return;
    }
    printf("socket %s, channel %" PRIu32 ", tlength %" PRIu32 ", prebuf %" PRIu32 ", minreq %" PRIu32
           ", first request %" PRIu32 "\n",
           path, rig->pulse.stream.channel, rig->pulse.stream.tlength, rig->pulse.stream.prebuf,
           rig->pulse.stream.minreq, rig->pulse.stream.owed);
    uint32_t left = AP_REAL_SECOND_BYTES;
    const int64_t deadline = time_now_ns() + (int64_t)AP_WAIT_MS * 1000000;
    while (left != 0u && time_now_ns() < deadline) {
        REQUIRE(audio_pulse_service(&rig->pulse, 5));
        uint32_t frames = audio_pulse_frames_owed(&rig->pulse);
        frames = frames * PULSE_WIRE_FRAME_BYTES < left ? frames : left / PULSE_WIRE_FRAME_BYTES;
        if (frames == 0u) {
            continue;
        }
        int16_t *const samples = audio_pulse_samples(&rig->pulse);
        memset(samples, 0, frames * PULSE_WIRE_FRAME_BYTES);
        for (uint32_t i = 0u; i < frames * 2u; i++) {
            REQUIRE(samples[i] == 0);
        }
        REQUIRE(audio_pulse_send(&rig->pulse, frames));
        left -= frames * PULSE_WIRE_FRAME_BYTES;
    }
    CHECK_EQ_U64(left, 0u);
    printf("sent %" PRIu64 " zero bytes in %" PRIu64 " requests: %" PRIu64 " underflows, %" PRIu64 " overflows\n",
           rig->pulse.sent_bytes, rig->pulse.request_count, rig->pulse.underflow_count, rig->pulse.overflow_count);
    audio_pulse_close(&rig->pulse);
}

TEST_MAIN()
