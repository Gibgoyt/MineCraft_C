/* See run/src/audio/README.md. */
#ifndef MC_RUN_AUDIO_INDEX_H
#define MC_RUN_AUDIO_INDEX_H

#include "arena/index.h"
#include "audio/mixer/index.h"
#include "audio/pulse/index.h"
#include "file/index.h"
#include "heap/index.h"
#include "machine/index.h"
#include "thread/index.h"

#include <stdatomic.h>
#include <stdint.h>

enum : uint32_t {
    AUDIO_RING_EVENTS = 64u, /* Far more than a tick posts, and the thread looks a couple of hundred times a second */
    AUDIO_PATH_BYTES = 256u,
    /* X displays from this number up are a test's own Xvfb, by the rule agents on this repository work to, and no desktop has one. A game on one is never heard. */
    AUDIO_PRIVATE_DISPLAY_FIRST = 80u,
};

/* Where what is mixed goes. */
typedef enum audio_output_t : uint8_t {
    AUDIO_OUTPUT_NONE = 0, /* nothing starts, and a post does nothing */
    AUDIO_OUTPUT_SERVER =
        1, /* the desktop's sound server. With none there, silence and one log line, and it keeps looking */
    AUDIO_OUTPUT_FILE = 2, /* raw samples to a file, at the speed they would play, and no sound at all */
    AUDIO_OUTPUT_FDS = 3,  /* a stand-in for a server over two descriptors: a test's */
} audio_output_t;

/* What audio_init is told. Everything it needs is copied. */
typedef struct audio_options_t {
    const char *socket_path; /* 0: SERVER: where it is, or nullptr to find out from the environment */
    const char *cookie_path; /* 8: SERVER: its cookie, or nullptr to find out from the environment */
    const char *file_path;   /* 16: FILE */
    int32_t read_fd;         /* 24: FDS */
    int32_t write_fd;        /* 28: FDS */
    uint32_t volume_percent; /* 32 */
    uint32_t retry_ms;       /* 36: how long between looks for a server, or 0 for AUDIO_RETRY_MS */
    audio_output_t output;   /* 40 */
    uint8_t _pad0[7];        /* 41 */
} audio_options_t;
static_assert(sizeof(audio_options_t) == 48);

/* One sound for the thread to start. */
typedef struct audio_event_t {
    uint16_t gain_left;  /* 0 */
    uint16_t gain_right; /* 2 */
    uint8_t family;      /* 4: sound_family_t */
    uint8_t kind;        /* 5: sound_kind_t */
    uint8_t _pad0[2];    /* 6 */
} audio_event_t;
static_assert(sizeof(audio_event_t) == 8);

/* One producer, the sim or whichever one thread holds the map, and one consumer, the audio thread. Written by the two, so each end has its own line. */
typedef struct audio_ring_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) _Atomic uint32_t
        head;                                    /* 0: the producer's: release, as it vouches for the event written */
    uint32_t dropped_count;                      /* 4: the producer's: posts that found it full */
    uint8_t _pad0[MC_CPU_CACHE_LINE_BYTES - 8u]; /* 8 */
    _Atomic uint32_t tail;                       /* 64: the consumer's */
    uint8_t _pad1[MC_CPU_CACHE_LINE_BYTES - 4u]; /* 68 */
    audio_event_t events[AUDIO_RING_EVENTS];     /* 128 */
} audio_ring_t;
static_assert(sizeof(audio_ring_t) == 640);
static_assert(alignof(audio_ring_t) == MC_CPU_CACHE_LINE_BYTES);

/* Everything sound is, at the start of owned_region: too much for main's stack, which holds app_t and has no room. */
typedef struct audio_state_t {
    arena_t arena;       /* 0: over the rest of owned_region */
    thread_t thread;     /* 64 */
    audio_mixer_t mixer; /* 112: the thread's */
    audio_pulse_t pulse; /* 400: the thread's */
    uint8_t _pad2[56];   /* 712 */
    audio_ring_t ring;   /* 768 */
    /* Main to the thread, and each is a word on its own that vouches for nothing else, so relaxed. */
    _Atomic uint32_t volume_percent; /* 1408 */
    _Atomic uint32_t stop;           /* 1412 */
    _Atomic uint32_t preview;        /* 1416: the volume has changed: play one sound at it, for the ear to judge */
    /* The thread to anyone who asks. */
    _Atomic uint32_t connected;         /* 1420: a server is taking what is sent */
    file_t file;                        /* 1424: the thread's, in AUDIO_OUTPUT_FILE, from audio_init */
    int32_t read_fd;                    /* 1432 */
    int32_t write_fd;                   /* 1436 */
    uint32_t retry_ms;                  /* 1440 */
    audio_output_t output;              /* 1444 */
    uint8_t logged_silent;              /* 1445: the thread's: the one line has gone out */
    uint8_t _pad0[2];                   /* 1446 */
    char socket_path[AUDIO_PATH_BYTES]; /* 1448 */
    char cookie_path[AUDIO_PATH_BYTES]; /* 1704 */
    uint8_t has_cookie_path;            /* 1960 */
    uint8_t _pad1[87];                  /* 1961 */
} audio_state_t;
static_assert(sizeof(audio_state_t) == 2048);
static_assert(alignof(audio_state_t) == 64);

/* There is one, under app_t. All but this is in the region, and nothing is taken at all for a run with no sound. */
typedef struct audio_t {
    heap_region_t owned_region;    /* 0 */
    audio_state_t *borrowed_state; /* 16: from owned_region, or nullptr for no sound */
    uint64_t played_count;         /* 24: as of audio_deinit: sounds that started */
    uint64_t dropped_count;        /* 32: as of audio_deinit: posts that found the ring full */
} audio_t;
static_assert(sizeof(audio_t) == 40);

/* Starts the thread, on the spare CPU from config/machine.yaml. Everything that can go wrong here ends in silence and one log line, as nothing about sound is worth not
 * playing the game for: so this cannot fail, and every other call is safe on what it leaves. Call it with the options' output NONE for no sound at all. */
void audio_init(audio_t *audio, const audio_options_t *options, const thread_topology_t *topology);

/* Whether this value of DISPLAY is a test's private X server, which a person's speakers have nothing to do with. Not set is a text console, and no. */
bool audio_display_is_private(const char *display);

/* Stops the thread and lets go of everything. Safe on a zeroed audio_t. */
void audio_deinit(audio_t *audio);

/* Puts a sound on the ring. One thread at a time, the one holding the map. False if it did nothing: no output, or a full ring. Never waits and touches no heap. */
bool audio_post(audio_t *audio, uint32_t family, uint32_t kind, uint16_t gain_left, uint16_t gain_right);

/* The volume from now on, from 0 to AUDIO_MIXER_VOLUME_MAX. With preview, one sound plays at it, so that a person moving the slider hears where it is. Any thread. */
void audio_volume_set(audio_t *audio, uint32_t volume_percent, bool preview);

/* What it is now, and 0 with no sound. */
uint32_t audio_volume_get(const audio_t *audio);

/* Whether a server is taking what is sent, as of now. Any thread. */
bool audio_connected(const audio_t *audio);

#endif
