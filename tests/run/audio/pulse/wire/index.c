/* ctest run_audio_pulse_wire. See README.md. The protocol as bytes: no server, no socket and no sound. */
#include "harness/index.h"

#include "audio/pulse/wire/index.h"

#include <stdint.h>
#include <string.h>

/* ---- Where it is ------------------------------------------------------------------------------------------------- */

TEST(the_socket_is_where_the_environment_says_and_never_on_another_machine) {
    char path[128];
    REQUIRE(pulse_wire_socket_path(path, sizeof(path), nullptr, "/run/user/1000", 1000u));
    CHECK_STR_EQ(path, "/run/user/1000/pulse/native");
    REQUIRE(pulse_wire_socket_path(path, sizeof(path), "", "", 1234u));
    CHECK_STR_EQ(path, "/run/user/1234/pulse/native");
    REQUIRE(pulse_wire_socket_path(path, sizeof(path), nullptr, nullptr, 7u));
    CHECK_STR_EQ(path, "/run/user/7/pulse/native");
    REQUIRE(pulse_wire_socket_path(path, sizeof(path), "unix:/tmp/a/native", "/run/user/1000", 1000u));
    CHECK_STR_EQ(path, "/tmp/a/native");
    REQUIRE(pulse_wire_socket_path(path, sizeof(path), "/tmp/b/native", nullptr, 1000u));
    CHECK_STR_EQ(path, "/tmp/b/native");
    CHECK(!pulse_wire_socket_path(path, sizeof(path), "tcp:192.168.1.5:4713", "/run/user/1000", 1000u));
    CHECK(!pulse_wire_socket_path(path, sizeof(path), "othermachine", "/run/user/1000", 1000u));
    CHECK(!pulse_wire_socket_path(path, sizeof(path), "unix:relative", "/run/user/1000", 1000u));
    CHECK(!pulse_wire_socket_path(path, sizeof(path), "/tmp/a tcp:other", "/run/user/1000", 1000u));
    char small[8];
    CHECK(!pulse_wire_socket_path(small, sizeof(small), nullptr, "/run/user/1000", 1000u));
}

TEST(the_cookie_is_looked_for_where_libpulse_looks) {
    char path[128];
    REQUIRE(pulse_wire_cookie_path(path, sizeof(path), "/x/cookie", "/config", "/home/a"));
    CHECK_STR_EQ(path, "/x/cookie");
    REQUIRE(pulse_wire_cookie_path(path, sizeof(path), nullptr, "/config", "/home/a"));
    CHECK_STR_EQ(path, "/config/pulse/cookie");
    REQUIRE(pulse_wire_cookie_path(path, sizeof(path), "", "", "/home/a"));
    CHECK_STR_EQ(path, "/home/a/.config/pulse/cookie");
    CHECK(!pulse_wire_cookie_path(path, sizeof(path), nullptr, nullptr, nullptr));
    char small[8];
    CHECK(!pulse_wire_cookie_path(small, sizeof(small), nullptr, nullptr, "/home/a"));
}

/* ---- What goes out, byte by byte --------------------------------------------------------------------------------- */

TEST(a_descriptor_is_big_endian_with_the_rest_zero_and_reads_back) {
    uint8_t bytes[PULSE_WIRE_DESCRIPTOR_BYTES];
    memset(bytes, 0xAA, sizeof(bytes));
    pulse_wire_descriptor(bytes, 0x01020304u, 0xFFFFFFFFu);
    static const uint8_t expected[PULSE_WIRE_DESCRIPTOR_BYTES] = {
        0x01u, 0x02u, 0x03u, 0x04u, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    CHECK_MEM_EQ(bytes, expected, sizeof(expected));
    uint32_t length = 0u, channel = 0u;
    pulse_wire_descriptor_parse(bytes, &length, &channel);
    CHECK_EQ_U64(length, 0x01020304u);
    CHECK_EQ_U64(channel, 0xFFFFFFFFu);
}

TEST(auth_says_the_version_with_no_shared_memory_and_the_whole_cookie) {
    uint8_t cookie[PULSE_WIRE_COOKIE_BYTES];
    for (uint32_t i = 0u; i < PULSE_WIRE_COOKIE_BYTES; i++) {
        cookie[i] = (uint8_t)i;
    }
    uint8_t out[PULSE_WIRE_COMMAND_MAX_BYTES];
    const uint32_t bytes = pulse_wire_auth(out, sizeof(out), 0x11223344u, cookie);
    CHECK_EQ_U64(bytes, 5u + 5u + 5u + 5u + PULSE_WIRE_COOKIE_BYTES);
    static const uint8_t head[] = {'L', 0u,    0u,    0u,    8u,    /* AUTH */
                                   'L', 0x11u, 0x22u, 0x33u, 0x44u, /* tag */
                                   'L', 0u,    0u,    0u,    35u,   /* version 35, no bits above */
                                   'x', 0u,    0u,    1u,    0u};   /* 256 bytes follow */
    CHECK_MEM_EQ(out, head, sizeof(head));
    CHECK_MEM_EQ(out + sizeof(head), cookie, sizeof(cookie));
}

TEST(a_client_name_is_a_property_list_of_one) {
    uint8_t out[PULSE_WIRE_COMMAND_MAX_BYTES];
    const uint32_t bytes = pulse_wire_client_name(out, sizeof(out), 2u, "ab");
    static const uint8_t expected[] = {'L', 0u,  0u,  0u,  9u, /* SET_CLIENT_NAME */
                                       'L', 0u,  0u,  0u,  2u, /* tag */
                                       'P',                    /* a list */
                                       't', 'a', 'p', 'p', 'l', 'i', 'c', 'a', 't', 'i', 'o', 'n',
                                       '.', 'n', 'a', 'm', 'e', 0u,  'L', 0u,  0u,  0u,  3u, /* its length */
                                       'x', 0u,  0u,  0u,  3u,  'a', 'b', 0u, /* its bytes, with their NUL */
                                       'N'};                                  /* the end */
    CHECK_EQ_U64(bytes, sizeof(expected));
    CHECK_MEM_EQ(out, expected, sizeof(expected));
}

/* The layout the stage 1 spike proved on the real pipewire-pulse, field by field, either way the stream is opened. */
static uint32_t wire_stream_expected(uint8_t *out, bool muted) {
    const uint8_t volume_high = (uint8_t)(muted ? 0u : 1u);
    const uint8_t flag = muted ? (uint8_t)'1' : (uint8_t)'0';
    const uint8_t expected[] = {
        'L',  0u,    0u,    0u,          3u,                                        /* CREATE_PLAYBACK_STREAM */
        'L',  0u,    0u,    0u,          5u,                                        /* tag */
        'a',  3u,    2u,    0u,          0u,    0xBBu, 0x80u,                       /* s16le, 2 channels, 48000 Hz */
        'm',  2u,    1u,    2u,                                                     /* front left, front right */
        'L',  0xFFu, 0xFFu, 0xFFu,       0xFFu,                                     /* no sink index */
        'N',                                                                        /* the default sink */
        'L',  0xFFu, 0xFFu, 0xFFu,       0xFFu,                                     /* maxlength: the server's */
        '0',                                                                        /* not corked */
        'L',  0u,    0u,    0x1Eu,       0u,                                        /* tlength 7680 */
        'L',  0xFFu, 0xFFu, 0xFFu,       0xFFu,                                     /* prebuf */
        'L',  0xFFu, 0xFFu, 0xFFu,       0xFFu,                                     /* minreq */
        'L',  0u,    0u,    0u,          0u,                                        /* sync id */
        'v',  2u,    0u,    volume_high, 0u,    0u,    0u,    volume_high, 0u,  0u, /* both channels */
        '0',  '0',   '0',   '0',         '0',   '0',   '0',                         /* no remapping and the rest */
        flag,                                                                       /* muted */
        '1',                                                                        /* adjust_latency */
        'P',  't',   'm',   'e',         'd',   'i',   'a',   '.',         'n', 'a', 'm', 'e', 0u, /* media.name */
        'L',  0u,    0u,    0u,          2u,    'x',   0u,    0u,          0u,  2u,  'g', 0u,      /* "g" */
        'N',                     /* the end of the list */
        flag,                    /* volume_set */
        '0',                     /* early_requests */
        flag,                    /* muted_set */
        '0',  '0',   '0',   '0', /* the rest */
        'B',  0u,                /* no format list */
    };
    memcpy(out, expected, sizeof(expected));
    return (uint32_t)sizeof(expected);
}

TEST(a_stream_is_what_was_proved_on_the_real_server) {
    uint8_t out[PULSE_WIRE_COMMAND_MAX_BYTES], expected[PULSE_WIRE_COMMAND_MAX_BYTES];
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        const bool muted = pass == 1u;
        const uint32_t bytes = pulse_wire_create_stream(out, sizeof(out), 5u, "g", 7680u, muted);
        const uint32_t expected_bytes = wire_stream_expected(expected, muted);
        CHECK_EQ_U64(bytes, expected_bytes);
        CHECK_MEM_EQ(out, expected, expected_bytes);
    }
}

TEST(an_encoder_with_too_little_room_says_so_and_stays_inside_it) {
    uint8_t cookie[PULSE_WIRE_COOKIE_BYTES] = {0};
    uint8_t out[64];
    for (uint32_t capacity = 0u; capacity < 40u; capacity++) {
        memset(out, 0x5Au, sizeof(out));
        CHECK_EQ_U64(pulse_wire_auth(out, capacity, 1u, cookie), 0u);
        CHECK_EQ_U64(pulse_wire_client_name(out, capacity, 1u, "name"), 0u);
        CHECK_EQ_U64(pulse_wire_create_stream(out, capacity, 1u, "name", 7680u, false), 0u);
        for (uint32_t i = capacity; i < sizeof(out); i++) {
            CHECK_EQ_U64(out[i], 0x5Au);
        }
    }
}

/* ---- What comes in ----------------------------------------------------------------------------------------------- */

TEST(a_head_reads_and_a_cut_or_wrong_one_does_not) {
    static const uint8_t packet[] = {'L', 0u, 0u, 0u, 2u, 'L', 0u, 0u, 1u, 0u, 'L', 0u, 0u, 0u, 35u};
    pulse_wire_head_t head;
    REQUIRE(pulse_wire_head_parse(packet, sizeof(packet), &head));
    CHECK_EQ_U64(head.command, PULSE_WIRE_REPLY);
    CHECK_EQ_U64(head.tag, 256u);
    CHECK_EQ_U64(head.after, 10u);
    uint32_t value = 0u;
    REQUIRE(pulse_wire_u32_parse(packet, sizeof(packet), head.after, &value));
    CHECK_EQ_U64(value, 35u);
    for (uint32_t size = 0u; size < 10u; size++) {
        CHECK(!pulse_wire_head_parse(packet, size, &head));
    }
    static const uint8_t wrong[] = {'t', 0u, 0u, 0u, 2u, 'L', 0u, 0u, 1u, 0u};
    CHECK(!pulse_wire_head_parse(wrong, sizeof(wrong), &head));
    CHECK(!pulse_wire_u32_parse(packet, sizeof(packet), sizeof(packet), &value));
    CHECK(!pulse_wire_u32_parse(packet, sizeof(packet), sizeof(packet) + 100u, &value));
    CHECK(!pulse_wire_u32_parse(packet, sizeof(packet), UINT32_MAX - 2u, &value));
}

TEST(a_stream_reply_gives_the_servers_numbers) {
    /* The numbers the real server gave in stage 1, with what the client does not read after them. */
    static const uint8_t reply[] = {'L', 0u, 0u,    0u,    1u,    /* channel */
                                    'L', 0u, 0u,    0u,    77u,   /* index */
                                    'L', 0u, 0u,    0x0Eu, 0x10u, /* owed: 3600 */
                                    'L', 0u, 0x40u, 0u,    0u,    /* maxlength: 4194304 */
                                    'L', 0u, 0u,    0x0Eu, 0x10u, /* tlength: 3600 */
                                    'L', 0u, 0u,    0x09u, 0x64u, /* prebuf: 2404 */
                                    'L', 0u, 0u,    0x04u, 0xB0u, /* minreq: 1200 */
                                    'a', 3u, 2u,    0u,    0u,    0xBBu, 0x80u};
    pulse_wire_stream_t stream;
    REQUIRE(pulse_wire_stream_parse(reply, sizeof(reply), 0u, &stream));
    CHECK_EQ_U64(stream.channel, 1u);
    CHECK_EQ_U64(stream.index, 77u);
    CHECK_EQ_U64(stream.owed, 3600u);
    CHECK_EQ_U64(stream.maxlength, 4194304u);
    CHECK_EQ_U64(stream.tlength, 3600u);
    CHECK_EQ_U64(stream.prebuf, 2404u);
    CHECK_EQ_U64(stream.minreq, 1200u);
    for (uint32_t size = 0u; size < 35u; size++) {
        CHECK(!pulse_wire_stream_parse(reply, size, 0u, &stream));
    }
}

TEST(a_request_says_which_channel_and_how_many_bytes) {
    static const uint8_t request[] = {'L', 0u, 0u, 0u, 61u, 'L', 0xFFu, 0xFFu, 0xFFu, 0xFFu,
                                      'L', 0u, 0u, 0u, 1u,  'L', 0u,    0u,    8u,    16u};
    pulse_wire_head_t head;
    REQUIRE(pulse_wire_head_parse(request, sizeof(request), &head));
    CHECK_EQ_U64(head.command, PULSE_WIRE_REQUEST);
    uint32_t channel = 0u, bytes = 0u;
    REQUIRE(pulse_wire_request_parse(request, sizeof(request), head.after, &channel, &bytes));
    CHECK_EQ_U64(channel, 1u);
    CHECK_EQ_U64(bytes, 2064u);
    CHECK(!pulse_wire_request_parse(request, sizeof(request) - 1u, head.after, &channel, &bytes));
}

TEST_MAIN()
