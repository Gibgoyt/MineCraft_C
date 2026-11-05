/* See run/src/audio/pulse/README.md. */
#ifndef MC_RUN_AUDIO_PULSE_INDEX_H
#define MC_RUN_AUDIO_PULSE_INDEX_H

#include "arena/index.h"
#include "audio/pulse/wire/index.h"

#include <stdatomic.h>
#include <stdint.h>

enum : uint32_t {
    AUDIO_PULSE_WHY_BYTES = 128u,
    AUDIO_PULSE_REPLY_BYTES = 64u, /* more than the part of any reply that is read */
    /* A packet from the server past this is not one a playback connection gets. */
    AUDIO_PULSE_INBOX_BYTES = (1u << 16u) + PULSE_WIRE_DESCRIPTOR_BYTES,
    /* How many frames audio_pulse_send takes at once. A sixth of a second: the server asks for far less. */
    AUDIO_PULSE_PCM_FRAMES = 8192u,
    AUDIO_PULSE_OUTBOX_BYTES = PULSE_WIRE_DESCRIPTOR_BYTES + AUDIO_PULSE_PCM_FRAMES * PULSE_WIRE_FRAME_BYTES,
    AUDIO_PULSE_ARENA_BYTES = AUDIO_PULSE_INBOX_BYTES + AUDIO_PULSE_OUTBOX_BYTES + PULSE_WIRE_COMMAND_MAX_BYTES + 128u,
    /* 40 ms asked for: the server gave 75% of what was asked for in stage 1, and 30 ms is well under what an ear ties to a click. */
    AUDIO_PULSE_TLENGTH_BYTES = PULSE_WIRE_BYTES_PER_SECOND / 25u,
};

/* What a client knows. The audio thread's alone. */
typedef struct audio_pulse_t {
    uint8_t *borrowed_inbox;    /* 0: from the arena */
    uint8_t *borrowed_outbox;   /* 8: a descriptor, then the samples, in one piece */
    uint8_t *borrowed_command;  /* 16 */
    uint64_t request_count;     /* 24 */
    uint64_t underflow_count;   /* 32 */
    uint64_t overflow_count;    /* 40 */
    uint64_t sent_bytes;        /* 48 */
    pulse_wire_stream_t stream; /* 56: as the server has it */
    int32_t read_fd;            /* 84 */
    int32_t write_fd;           /* 88 */
    uint32_t inbox_bytes;       /* 92 */
    uint32_t next_tag;          /* 96 */
    uint32_t wanted_tag;        /* 100: the reply being waited for */
    uint32_t reply_bytes;       /* 104: of reply */
    uint32_t owed;              /* 108: bytes the server has asked for and not had */
    uint8_t is_socket;    /* 112: read_fd and write_fd are one socket of ours to close, so send can say no signal */
    uint8_t attached;     /* 113 */
    uint8_t reply_came;   /* 114 */
    uint8_t reply_failed; /* 115 */
    uint8_t _pad0[4];     /* 116 */
    uint8_t reply[AUDIO_PULSE_REPLY_BYTES]; /* 120 */
    char why[AUDIO_PULSE_WHY_BYTES];        /* 184: what went wrong last, for one line in a log */
} audio_pulse_t;
static_assert(sizeof(audio_pulse_t) == 312);

/* What a stream is asked for, and where its server is. */
typedef struct audio_pulse_open_t {
    const char *cookie_path; /* 0: nullptr or a file that is not there sends zeros, which pipewire-pulse takes */
    const char *name;        /* 8: what the desktop's mixer calls it */
    _Atomic uint32_t *borrowed_stop; /* 16: not 0 gives up on waiting, or nullptr to wait it out */
    uint8_t muted;                   /* 24: volume 0 and muted at the server. The caller still sends what it sends */
    uint8_t _pad0[7];                /* 25 */
} audio_pulse_open_t;
static_assert(sizeof(audio_pulse_open_t) == 32);

/* Takes its buffers: AUDIO_PULSE_ARENA_BYTES. Nothing is open yet. */
void audio_pulse_start(audio_pulse_t *pulse, arena_t *arena);

/* Connects to a sound server on this machine and opens a stream. False with why set, and nothing left open: no server there is what this looks like most often. Up to a
 * couple of seconds if the server has stopped answering: the audio thread's to spend, and never a frame's. */
[[nodiscard]] bool audio_pulse_connect(audio_pulse_t *pulse, const char *socket_path, const audio_pulse_open_t *open);

/* The same over what is already joined to a server: a stand-in in a test, over two pipes, which is why there are two. They stay the caller's to close. */
[[nodiscard]] bool audio_pulse_attach(audio_pulse_t *pulse, int32_t read_fd, int32_t write_fd,
                                      const audio_pulse_open_t *open);

/* Lets go of it: the server takes the stream away with the connection. Safe on one not open. */
void audio_pulse_close(audio_pulse_t *pulse);

/* Takes what the server has said, waiting up to wait_ms if it has said nothing. False with why set if the connection is over. */
[[nodiscard]] bool audio_pulse_service(audio_pulse_t *pulse, int32_t wait_ms);

/* Whole frames the server has asked for, up to AUDIO_PULSE_PCM_FRAMES: what to write to audio_pulse_samples. */
uint32_t audio_pulse_frames_owed(const audio_pulse_t *pulse);

/* Where to put the next frames: left, right, left, right. */
int16_t *audio_pulse_samples(audio_pulse_t *pulse);

/* Sends this many frames from there. False with why set if the connection is over. */
[[nodiscard]] bool audio_pulse_send(audio_pulse_t *pulse, uint32_t frames);

#endif
