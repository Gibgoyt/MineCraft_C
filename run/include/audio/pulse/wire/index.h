/* See run/src/audio/pulse/wire/README.md. */
#ifndef MC_RUN_AUDIO_PULSE_WIRE_INDEX_H
#define MC_RUN_AUDIO_PULSE_WIRE_INDEX_H

#include <stdint.h>

enum : uint32_t {
    PULSE_WIRE_DESCRIPTOR_BYTES = 20u, /* before every packet: length, channel, an offset and flags, big-endian */
    PULSE_WIRE_COOKIE_BYTES = 256u,
    PULSE_WIRE_COMMAND_MAX_BYTES = 1024u, /* the longest any encoder below writes */
    PULSE_WIRE_CHANNEL_CONTROL = 0xFFFFFFFFu,
    PULSE_WIRE_PROTOCOL_VERSION = 35u, /* no shm or memfd bits: samples travel in the socket */
    PULSE_WIRE_VERSION_MASK = 0x0000FFFFu,
    PULSE_WIRE_RATE = 48000u,
    PULSE_WIRE_CHANNELS = 2u,
    PULSE_WIRE_FRAME_BYTES = 4u, /* s16le, left then right */
    PULSE_WIRE_BYTES_PER_SECOND = PULSE_WIRE_RATE * PULSE_WIRE_FRAME_BYTES,
};

/* The numbers of src/pulsecore/native-common.h in PulseAudio's source, read as reference. Those that are not here are ignored when they come. */
typedef enum pulse_wire_command_t : uint32_t {
    PULSE_WIRE_ERROR = 0u,
    PULSE_WIRE_TIMEOUT = 1u,
    PULSE_WIRE_REPLY = 2u,
    PULSE_WIRE_CREATE_PLAYBACK_STREAM = 3u,
    PULSE_WIRE_DELETE_PLAYBACK_STREAM = 4u,
    PULSE_WIRE_AUTH = 8u,
    PULSE_WIRE_SET_CLIENT_NAME = 9u,
    PULSE_WIRE_REQUEST = 61u,
    PULSE_WIRE_OVERFLOW = 62u,
    PULSE_WIRE_UNDERFLOW = 63u,
    PULSE_WIRE_STREAM_KILLED = 64u,
    PULSE_WIRE_STARTED = 86u,
} pulse_wire_command_t;

/* What every command and reply starts with, and where what follows it starts. */
typedef struct pulse_wire_head_t {
    uint32_t command; /* 0 */
    uint32_t tag;     /* 4 */
    uint32_t after;   /* 8: into the packet */
} pulse_wire_head_t;
static_assert(sizeof(pulse_wire_head_t) == 12);

/* A stream as the reply to creating it says. The server's numbers are the ones that hold, and not those asked for. */
typedef struct pulse_wire_stream_t {
    uint32_t channel;   /* 0 */
    uint32_t index;     /* 4 */
    uint32_t owed;      /* 8: bytes it asks for at once */
    uint32_t maxlength; /* 12 */
    uint32_t tlength;   /* 16 */
    uint32_t prebuf;    /* 20 */
    uint32_t minreq;    /* 24 */
} pulse_wire_stream_t;
static_assert(sizeof(pulse_wire_stream_t) == 28);

/* Where a sound server's socket is: PULSE_SERVER if it says a local one, and otherwise runtime_dir, and otherwise /run/user/<uid>. False if it names a
 * machine, as there is no TCP here, or the path does not fit. A server name or a dir of nullptr, or an empty one, is the same as not being set. */
[[nodiscard]] bool pulse_wire_socket_path(char *out, uint64_t capacity, const char *pulse_server,
                                          const char *runtime_dir, uint32_t uid);

/* Where its cookie is, in the order libpulse looks: PULSE_COOKIE, $XDG_CONFIG_HOME/pulse/cookie, $HOME/.config/pulse/cookie. False if none of these says, or it does
 * not fit. */
[[nodiscard]] bool pulse_wire_cookie_path(char *out, uint64_t capacity, const char *pulse_cookie,
                                          const char *config_home, const char *home);

/* A packet's descriptor, for a packet of length bytes on a channel. */
void pulse_wire_descriptor(uint8_t out[PULSE_WIRE_DESCRIPTOR_BYTES], uint32_t length, uint32_t channel);
void pulse_wire_descriptor_parse(const uint8_t bytes[PULSE_WIRE_DESCRIPTOR_BYTES], uint32_t *out_length,
                                 uint32_t *out_channel);

/* Encoders: each writes one whole command, without its descriptor, and returns how many bytes, or 0 if capacity is too small. */
uint32_t pulse_wire_auth(uint8_t *out, uint32_t capacity, uint32_t tag, const uint8_t cookie[PULSE_WIRE_COOKIE_BYTES]);
uint32_t pulse_wire_client_name(uint8_t *out, uint32_t capacity, uint32_t tag, const char *name);
/* s16le stereo at 48000 Hz on the default sink, asking for tlength bytes of buffer. Muted opens it at volume 0 and muted, which is what a run that must stay
 * silent asks for. Otherwise the volume is the server's to remember, so the desktop's mixer keeps its say. */
uint32_t pulse_wire_create_stream(uint8_t *out, uint32_t capacity, uint32_t tag, const char *name, uint32_t tlength,
                                  bool muted);
uint32_t pulse_wire_delete_stream(uint8_t *out, uint32_t capacity, uint32_t tag, uint32_t channel);

/* Decoders: false, touching nothing else, on anything malformed or cut short. */
[[nodiscard]] bool pulse_wire_head_parse(const uint8_t *bytes, uint32_t size, pulse_wire_head_t *out);
/* Reads what follows a head, from after. */
[[nodiscard]] bool pulse_wire_u32_parse(const uint8_t *bytes, uint32_t size, uint32_t after, uint32_t *out);
[[nodiscard]] bool pulse_wire_stream_parse(const uint8_t *bytes, uint32_t size, uint32_t after,
                                           pulse_wire_stream_t *out);
[[nodiscard]] bool pulse_wire_request_parse(const uint8_t *bytes, uint32_t size, uint32_t after, uint32_t *out_channel,
                                            uint32_t *out_bytes);

#endif
