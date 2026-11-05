/* A stand-in for a sound server, for the tests under tests/run/audio/: it speaks the protocol over two pipes on a thread of its own and keeps every sample it is sent, in memory. So
 * nothing here can ever be heard. Pipes, as sockets are allowed in one file of the game and no file of its tests. */
#ifndef MC_TESTS_RUN_AUDIO_FIXTURES_FAKE_PULSE_H
#define MC_TESTS_RUN_AUDIO_FIXTURES_FAKE_PULSE_H

#include "audio/pulse/wire/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    FAKE_PULSE_CHANNEL = 7u,
    FAKE_PULSE_STREAM_INDEX = 3u,
    FAKE_PULSE_OWED = 960u, /* 5 ms */
    FAKE_PULSE_TLENGTH = 3600u,
    FAKE_PULSE_PREBUF = 2404u,
    FAKE_PULSE_MINREQ = 1200u,
    FAKE_PULSE_MAXLENGTH = 4194304u,
    FAKE_PULSE_PACKET_BYTES = 1u << 16u,
    FAKE_PULSE_ERROR_ACCESS = 1u,
};

/* What it does in place of carrying on, once it has been sent its first samples, so that the hello is over by then. */
typedef enum fake_pulse_ending_t : uint8_t {
    FAKE_PULSE_GOES_ON = 0,
    FAKE_PULSE_CLOSES = 1,       /* the server going away */
    FAKE_PULSE_KILLS_STREAM = 2, /* takes the stream away */
    FAKE_PULSE_NONSENSE = 3,     /* a packet whose head is not one */
    FAKE_PULSE_TOO_LONG = 4,     /* a descriptor saying far more than any packet is */
} fake_pulse_ending_t;

typedef struct fake_pulse_t {
    thread_t thread;                        /* 0 */
    uint8_t *borrowed_pcm;                  /* 48: every sample sent to it */
    uint8_t *borrowed_packet;               /* 56 */
    uint64_t pcm_capacity;                  /* 64 */
    _Atomic uint64_t pcm_bytes;             /* 72: how much of borrowed_pcm is in */
    _Atomic uint32_t stop;                  /* 80 */
    _Atomic uint32_t streams;               /* 84: how many streams it has opened */
    int32_t from_client;                    /* 88 */
    int32_t to_client;                      /* 92 */
    int32_t client_read;                    /* 96: what the client is given */
    int32_t client_write;                   /* 100 */
    uint32_t create_bytes;                  /* 104 */
    uint8_t refuse_auth;                    /* 108: answers auth with an error */
    uint8_t silent;                         /* 109: reads everything and answers nothing */
    uint8_t ending;                         /* 110: fake_pulse_ending_t */
    uint8_t notices;                        /* 111: says STARTED and UNDERFLOW and OVERFLOW and something unknown before the first request */
    uint8_t cookie[PULSE_WIRE_COOKIE_BYTES]; /* 112: what auth sent */
    uint8_t create[PULSE_WIRE_COMMAND_MAX_BYTES]; /* 368: the command that opened the stream */
    uint8_t client_name_seen;               /* 1392 */
    uint8_t _pad0[7];                       /* 1393 */
} fake_pulse_t;
static_assert(sizeof(fake_pulse_t) == 1400);

static inline bool fake_pulse_write(fake_pulse_t *fake, const uint8_t *bytes, uint32_t size) {
    uint32_t sent = 0u;
    while (sent < size) {
        const int64_t now = write(fake->to_client, bytes + sent, size - sent);
        if (now <= 0) {
            return false;
        }
        sent += (uint32_t)now;
    }
    return true;
}

/* A packet on the control channel of nothing but whole numbers: which is all a server says here. */
static inline bool fake_pulse_say(fake_pulse_t *fake, const uint32_t *values, uint32_t count) {
    uint8_t packet[PULSE_WIRE_DESCRIPTOR_BYTES + 16u * 5u];
    uint32_t at = PULSE_WIRE_DESCRIPTOR_BYTES;
    for (uint32_t i = 0u; i < count; i++) {
        packet[at] = 'L';
        packet[at + 1u] = (uint8_t)(values[i] >> 24u);
        packet[at + 2u] = (uint8_t)(values[i] >> 16u);
        packet[at + 3u] = (uint8_t)(values[i] >> 8u);
        packet[at + 4u] = (uint8_t)values[i];
        at += 5u;
    }
    pulse_wire_descriptor(packet, at - PULSE_WIRE_DESCRIPTOR_BYTES, PULSE_WIRE_CHANNEL_CONTROL);
    return fake_pulse_write(fake, packet, at);
}

static inline bool fake_pulse_read(fake_pulse_t *fake, uint8_t *to, uint32_t size) {
    uint32_t have = 0u;
    while (have < size) {
        if (atomic_load_explicit(&fake->stop, memory_order_relaxed) != 0u) {
            return false;
        }
        struct pollfd ready = {.fd = fake->from_client, .events = POLLIN, .revents = 0};
        if (poll(&ready, 1u, 5) <= 0) {
            continue;
        }
        const int64_t got = read(fake->from_client, to + have, size - have);
        if (got <= 0) {
            return false;
        }
        have += (uint32_t)got;
    }
    return true;
}

/* How the conversation ends if it was told to. True if it is to stop. */
static inline bool fake_pulse_end(fake_pulse_t *fake) {
    switch (fake->ending) {
    case FAKE_PULSE_CLOSES:
        return true;
    case FAKE_PULSE_KILLS_STREAM: {
        const uint32_t killed[] = {PULSE_WIRE_STREAM_KILLED, UINT32_MAX, FAKE_PULSE_CHANNEL};
        (void)fake_pulse_say(fake, killed, 3u);
        return false;
    }
    case FAKE_PULSE_NONSENSE: {
        uint8_t packet[PULSE_WIRE_DESCRIPTOR_BYTES + 4u] = {0};
        pulse_wire_descriptor(packet, 4u, PULSE_WIRE_CHANNEL_CONTROL);
        (void)fake_pulse_write(fake, packet, sizeof(packet));
        return false;
    }
    case FAKE_PULSE_TOO_LONG: {
        uint8_t packet[PULSE_WIRE_DESCRIPTOR_BYTES];
        pulse_wire_descriptor(packet, 1u << 24u, PULSE_WIRE_CHANNEL_CONTROL);
        (void)fake_pulse_write(fake, packet, sizeof(packet));
        return false;
    }
    default:
        return false;
    }
}

static inline bool fake_pulse_command(fake_pulse_t *fake, const uint8_t *packet, uint32_t size) {
    pulse_wire_head_t head;
    if (!pulse_wire_head_parse(packet, size, &head) || fake->silent != 0u) {
        return true;
    }
    if (head.command == PULSE_WIRE_AUTH) {
        /* After 'L' and the version, 'x' and the length. */
        if (size == head.after + 5u + 5u + PULSE_WIRE_COOKIE_BYTES) {
            memcpy(fake->cookie, packet + head.after + 5u + 5u, PULSE_WIRE_COOKIE_BYTES);
        }
        if (fake->refuse_auth != 0u) {
            const uint32_t refused[] = {PULSE_WIRE_ERROR, head.tag, FAKE_PULSE_ERROR_ACCESS};
            return fake_pulse_say(fake, refused, 3u);
        }
        const uint32_t reply[] = {PULSE_WIRE_REPLY, head.tag, PULSE_WIRE_PROTOCOL_VERSION};
        return fake_pulse_say(fake, reply, 3u);
    }
    if (head.command == PULSE_WIRE_SET_CLIENT_NAME) {
        fake->client_name_seen = 1u;
        const uint32_t reply[] = {PULSE_WIRE_REPLY, head.tag, 42u};
        return fake_pulse_say(fake, reply, 3u);
    }
    if (head.command == PULSE_WIRE_CREATE_PLAYBACK_STREAM) {
        fake->create_bytes = size < PULSE_WIRE_COMMAND_MAX_BYTES ? size : PULSE_WIRE_COMMAND_MAX_BYTES;
        memcpy(fake->create, packet, fake->create_bytes);
        const uint32_t reply[] = {PULSE_WIRE_REPLY,   head.tag,          FAKE_PULSE_CHANNEL,  FAKE_PULSE_STREAM_INDEX,
                                  FAKE_PULSE_OWED,    FAKE_PULSE_MAXLENGTH, FAKE_PULSE_TLENGTH, FAKE_PULSE_PREBUF,
                                  FAKE_PULSE_MINREQ};
        (void)fake_pulse_say(fake, reply, 9u);
        atomic_fetch_add_explicit(&fake->streams, 1u, memory_order_release);
        if (fake->notices != 0u) {
            const uint32_t started[] = {PULSE_WIRE_STARTED, UINT32_MAX, FAKE_PULSE_CHANNEL};
            const uint32_t underflow[] = {PULSE_WIRE_UNDERFLOW, UINT32_MAX, FAKE_PULSE_CHANNEL};
            const uint32_t overflow[] = {PULSE_WIRE_OVERFLOW, UINT32_MAX, FAKE_PULSE_CHANNEL};
            const uint32_t unknown[] = {9999u, UINT32_MAX, 1u, 2u};
            (void)fake_pulse_say(fake, started, 3u);
            (void)fake_pulse_say(fake, underflow, 3u);
            (void)fake_pulse_say(fake, overflow, 3u);
            (void)fake_pulse_say(fake, unknown, 4u);
        }
        return true;
    }
    return true;
}

/* What came goes in the memory, and it asks for as much again after as long as that takes to play, as a real one does. */
static inline bool fake_pulse_samples(fake_pulse_t *fake, const uint8_t *packet, uint32_t size) {
    const uint64_t have = atomic_load_explicit(&fake->pcm_bytes, memory_order_relaxed);
    const uint64_t room = fake->pcm_capacity - have;
    const uint64_t take = size < room ? size : room;
    memcpy(fake->borrowed_pcm + have, packet, take);
    atomic_store_explicit(&fake->pcm_bytes, have + take, memory_order_release);
    if (fake->ending != FAKE_PULSE_GOES_ON) {
        return !fake_pulse_end(fake);
    }
    time_sleep_ns((int64_t)size * 1000000000ll / PULSE_WIRE_BYTES_PER_SECOND);
    const uint32_t request[] = {PULSE_WIRE_REQUEST, UINT32_MAX, FAKE_PULSE_CHANNEL, size};
    return fake_pulse_say(fake, request, 4u);
}

static inline void fake_pulse_run(void *borrowed_argument) {
    fake_pulse_t *const fake = borrowed_argument;
    for (;;) {
        uint8_t descriptor[PULSE_WIRE_DESCRIPTOR_BYTES];
        if (!fake_pulse_read(fake, descriptor, sizeof(descriptor))) {
            break;
        }
        uint32_t size = 0u, channel = 0u;
        pulse_wire_descriptor_parse(descriptor, &size, &channel);
        if (size > FAKE_PULSE_PACKET_BYTES || !fake_pulse_read(fake, fake->borrowed_packet, size)) {
            break;
        }
        const bool go_on = channel == PULSE_WIRE_CHANNEL_CONTROL ? fake_pulse_command(fake, fake->borrowed_packet, size)
                                                                  : fake_pulse_samples(fake, fake->borrowed_packet, size);
        if (!go_on) {
            break;
        }
    }
    /* Which is what a client sees of a server that went away. */
    (void)close(fake->to_client);
    (void)close(fake->from_client);
    fake->to_client = -1;
    fake->from_client = -1;
}

/* The caller zeroes it, sets what it should do, and gives it borrowed_pcm and borrowed_packet. A write to a pipe with no reader would otherwise end the test. */
static inline bool fake_pulse_start(fake_pulse_t *fake, const thread_topology_t *topology) {
    (void)signal(SIGPIPE, SIG_IGN);
    int32_t client_to_server[2], server_to_client[2];
    if (pipe(client_to_server) != 0) {
        return false;
    }
    if (pipe(server_to_client) != 0) {
        (void)close(client_to_server[0]);
        (void)close(client_to_server[1]);
        return false;
    }
    fake->from_client = client_to_server[0];
    fake->client_write = client_to_server[1];
    fake->client_read = server_to_client[0];
    fake->to_client = server_to_client[1];
    return thread_start(&fake->thread, topology, THREAD_ROLE_SIM, 0u, fake_pulse_run, fake);
}

static inline void fake_pulse_stop(fake_pulse_t *fake) {
    atomic_store_explicit(&fake->stop, 1u, memory_order_relaxed);
    thread_join(&fake->thread);
    (void)close(fake->client_read);
    (void)close(fake->client_write);
}

/* Until this much is in or timeout_ms has gone. */
static inline bool fake_pulse_wait_bytes(fake_pulse_t *fake, uint64_t bytes, uint32_t timeout_ms) {
    const int64_t deadline = time_now_ns() + (int64_t)timeout_ms * 1000000;
    while (atomic_load_explicit(&fake->pcm_bytes, memory_order_acquire) < bytes) {
        if (time_now_ns() > deadline) {
            return false;
        }
        time_sleep_ns(1000000);
    }
    return true;
}

#endif
