/* See README.md. The PulseAudio native protocol as bytes. No socket and no state: what goes over the socket is run/src/audio/pulse's. */
#include "audio/pulse/wire/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    WIRE_SAMPLE_S16LE = 3u,
    WIRE_FRONT_LEFT = 1u,
    WIRE_FRONT_RIGHT = 2u,
    WIRE_NO_INDEX = 0xFFFFFFFFu,
    WIRE_DEFAULT_ATTRIBUTE = 0xFFFFFFFFu,
    WIRE_VOLUME_NORMAL = 0x10000u,
    /* From no_remap_channels to variable_rate, none of which a stream of the server's own rate wants. */
    WIRE_PLAIN_FLAGS = 7u,
};

/* The tags that say what kind of value comes next. */
static const uint8_t TAG_U32 = 'L', TAG_U8 = 'B', TAG_STRING = 't', TAG_NULL = 'N', TAG_ARBITRARY = 'x', TAG_TRUE = '1',
                     TAG_FALSE = '0', TAG_SPEC = 'a', TAG_MAP = 'm', TAG_VOLUME = 'v', TAG_PROPERTIES = 'P';

typedef struct wire_writer_t {
    uint8_t *borrowed_bytes; /* 0 */
    uint32_t capacity;       /* 8 */
    uint32_t used;           /* 12 */
    uint8_t failed;          /* 16 */
    uint8_t _pad0[7];        /* 17 */
} wire_writer_t;
static_assert(sizeof(wire_writer_t) == 24);

static void wire_put_be32(uint8_t *to, uint32_t value) {
    to[0] = (uint8_t)(value >> 24u);
    to[1] = (uint8_t)(value >> 16u);
    to[2] = (uint8_t)(value >> 8u);
    to[3] = (uint8_t)value;
}

static uint32_t wire_get_be32(const uint8_t *from) {
    return ((uint32_t)from[0] << 24u) | ((uint32_t)from[1] << 16u) | ((uint32_t)from[2] << 8u) | (uint32_t)from[3];
}

static void wire_raw(wire_writer_t *writer, const void *data, uint32_t size) {
    if (writer->failed != 0u || size > writer->capacity - writer->used) {
        writer->failed = 1u;
        return;
    }
    memcpy(writer->borrowed_bytes + writer->used, data, size);
    writer->used += size;
}

static void wire_tag(wire_writer_t *writer, uint8_t tag) {
    wire_raw(writer, &tag, 1u);
}

static void wire_be32(wire_writer_t *writer, uint32_t value) {
    uint8_t bytes[4];
    wire_put_be32(bytes, value);
    wire_raw(writer, bytes, 4u);
}

static void wire_u32(wire_writer_t *writer, uint32_t value) {
    wire_tag(writer, TAG_U32);
    wire_be32(writer, value);
}

static void wire_bool(wire_writer_t *writer, bool value) {
    wire_tag(writer, value ? TAG_TRUE : TAG_FALSE);
}

static void wire_string(wire_writer_t *writer, const char *text) {
    wire_tag(writer, TAG_STRING);
    wire_raw(writer, text, (uint32_t)strlen(text) + 1u);
}

static void wire_arbitrary(wire_writer_t *writer, const void *data, uint32_t size) {
    wire_tag(writer, TAG_ARBITRARY);
    wire_be32(writer, size);
    wire_raw(writer, data, size);
}

/* One key and its text, with its NUL, and the end of the list. */
static void wire_properties(wire_writer_t *writer, const char *key, const char *value) {
    const uint32_t value_bytes = (uint32_t)strlen(value) + 1u;
    wire_tag(writer, TAG_PROPERTIES);
    wire_string(writer, key);
    wire_u32(writer, value_bytes);
    wire_arbitrary(writer, value, value_bytes);
    wire_tag(writer, TAG_NULL);
}

static wire_writer_t wire_command(uint8_t *out, uint32_t capacity, uint32_t command, uint32_t tag) {
    wire_writer_t writer = {.borrowed_bytes = out, .capacity = capacity, .used = 0u, .failed = 0u, ._pad0 = {0}};
    wire_u32(&writer, command);
    wire_u32(&writer, tag);
    return writer;
}

static uint32_t wire_finish(const wire_writer_t *writer) {
    return writer->failed != 0u ? 0u : writer->used;
}

/* ---- Where it is ------------------------------------------------------------------------------------------------- */

static bool wire_is_set(const char *text) {
    return text != nullptr && text[0] != '\0';
}

static bool wire_fits(int32_t wrote, uint64_t capacity) {
    return wrote > 0 && (uint64_t)wrote < capacity;
}

bool pulse_wire_socket_path(char *out, uint64_t capacity, const char *pulse_server, const char *runtime_dir,
                            uint32_t uid) {
    if (wire_is_set(pulse_server)) {
        const char *const path = strncmp(pulse_server, "unix:", 5u) == 0 ? pulse_server + 5 : pulse_server;
        /* Anything but a path is another machine, or a list of them. */
        if (path[0] != '/' || strchr(path, ' ') != nullptr) {
            return false;
        }
        return wire_fits(snprintf(out, capacity, "%s", path), capacity);
    }
    if (wire_is_set(runtime_dir)) {
        return wire_fits(snprintf(out, capacity, "%s/pulse/native", runtime_dir), capacity);
    }
    return wire_fits(snprintf(out, capacity, "/run/user/%" PRIu32 "/pulse/native", uid), capacity);
}

bool pulse_wire_cookie_path(char *out, uint64_t capacity, const char *pulse_cookie, const char *config_home,
                            const char *home) {
    if (wire_is_set(pulse_cookie)) {
        return wire_fits(snprintf(out, capacity, "%s", pulse_cookie), capacity);
    }
    if (wire_is_set(config_home)) {
        return wire_fits(snprintf(out, capacity, "%s/pulse/cookie", config_home), capacity);
    }
    if (wire_is_set(home)) {
        return wire_fits(snprintf(out, capacity, "%s/.config/pulse/cookie", home), capacity);
    }
    return false;
}

/* ---- What goes out ----------------------------------------------------------------------------------------------- */

void pulse_wire_descriptor(uint8_t out[PULSE_WIRE_DESCRIPTOR_BYTES], uint32_t length, uint32_t channel) {
    memset(out, 0, PULSE_WIRE_DESCRIPTOR_BYTES);
    wire_put_be32(&out[0], length);
    wire_put_be32(&out[4], channel);
}

void pulse_wire_descriptor_parse(const uint8_t bytes[PULSE_WIRE_DESCRIPTOR_BYTES], uint32_t *out_length,
                                 uint32_t *out_channel) {
    *out_length = wire_get_be32(&bytes[0]);
    *out_channel = wire_get_be32(&bytes[4]);
}

uint32_t pulse_wire_auth(uint8_t *out, uint32_t capacity, uint32_t tag, const uint8_t cookie[PULSE_WIRE_COOKIE_BYTES]) {
    wire_writer_t writer = wire_command(out, capacity, PULSE_WIRE_AUTH, tag);
    wire_u32(&writer, PULSE_WIRE_PROTOCOL_VERSION);
    wire_arbitrary(&writer, cookie, PULSE_WIRE_COOKIE_BYTES);
    return wire_finish(&writer);
}

uint32_t pulse_wire_client_name(uint8_t *out, uint32_t capacity, uint32_t tag, const char *name) {
    wire_writer_t writer = wire_command(out, capacity, PULSE_WIRE_SET_CLIENT_NAME, tag);
    wire_properties(&writer, "application.name", name);
    return wire_finish(&writer);
}

uint32_t pulse_wire_create_stream(uint8_t *out, uint32_t capacity, uint32_t tag, const char *name, uint32_t tlength,
                                  bool muted) {
    wire_writer_t writer = wire_command(out, capacity, PULSE_WIRE_CREATE_PLAYBACK_STREAM, tag);
    uint8_t spec[6];
    spec[0] = WIRE_SAMPLE_S16LE;
    spec[1] = PULSE_WIRE_CHANNELS;
    wire_put_be32(&spec[2], PULSE_WIRE_RATE);
    wire_tag(&writer, TAG_SPEC);
    wire_raw(&writer, spec, sizeof(spec));
    const uint8_t map[3] = {PULSE_WIRE_CHANNELS, WIRE_FRONT_LEFT, WIRE_FRONT_RIGHT};
    wire_tag(&writer, TAG_MAP);
    wire_raw(&writer, map, sizeof(map));
    wire_u32(&writer, WIRE_NO_INDEX);
    wire_tag(&writer, TAG_NULL);               /* the default sink, whichever it is by then */
    wire_u32(&writer, WIRE_DEFAULT_ATTRIBUTE); /* maxlength */
    wire_bool(&writer, false);                 /* corked */
    wire_u32(&writer, tlength);
    wire_u32(&writer, WIRE_DEFAULT_ATTRIBUTE); /* prebuf */
    wire_u32(&writer, WIRE_DEFAULT_ATTRIBUTE); /* minreq */
    wire_u32(&writer, 0u);                     /* sync id */
    const uint32_t volume = muted ? 0u : WIRE_VOLUME_NORMAL;
    uint8_t cvolume[1u + PULSE_WIRE_CHANNELS * 4u];
    cvolume[0] = PULSE_WIRE_CHANNELS;
    wire_put_be32(&cvolume[1], volume);
    wire_put_be32(&cvolume[5], volume);
    wire_tag(&writer, TAG_VOLUME);
    wire_raw(&writer, cvolume, sizeof(cvolume));
    for (uint32_t i = 0u; i < WIRE_PLAIN_FLAGS; i++) {
        wire_bool(&writer, false);
    }
    wire_bool(&writer, muted); /* muted */
    wire_bool(&writer, true);  /* adjust_latency */
    wire_properties(&writer, "media.name", name);
    wire_bool(&writer, muted); /* volume_set */
    wire_bool(&writer, false); /* early_requests */
    wire_bool(&writer, muted); /* muted_set */
    wire_bool(&writer, false); /* dont_inhibit_auto_suspend */
    wire_bool(&writer, false); /* fail_on_suspend */
    wire_bool(&writer, false); /* relative_volume */
    wire_bool(&writer, false); /* passthrough */
    wire_tag(&writer, TAG_U8); /* no format list */
    const uint8_t no_formats = 0u;
    wire_raw(&writer, &no_formats, 1u);
    return wire_finish(&writer);
}

uint32_t pulse_wire_delete_stream(uint8_t *out, uint32_t capacity, uint32_t tag, uint32_t channel) {
    wire_writer_t writer = wire_command(out, capacity, PULSE_WIRE_DELETE_PLAYBACK_STREAM, tag);
    wire_u32(&writer, channel);
    return wire_finish(&writer);
}

/* ---- What comes in ----------------------------------------------------------------------------------------------- */

bool pulse_wire_u32_parse(const uint8_t *bytes, uint32_t size, uint32_t after, uint32_t *out) {
    if (after > size || size - after < 5u || bytes[after] != TAG_U32) {
        return false;
    }
    *out = wire_get_be32(&bytes[after + 1u]);
    return true;
}

bool pulse_wire_head_parse(const uint8_t *bytes, uint32_t size, pulse_wire_head_t *out) {
    uint32_t command = 0u, tag = 0u;
    if (!pulse_wire_u32_parse(bytes, size, 0u, &command) || !pulse_wire_u32_parse(bytes, size, 5u, &tag)) {
        return false;
    }
    *out = (pulse_wire_head_t){.command = command, .tag = tag, .after = 10u};
    return true;
}

bool pulse_wire_stream_parse(const uint8_t *bytes, uint32_t size, uint32_t after, pulse_wire_stream_t *out) {
    uint32_t values[7];
    for (uint32_t i = 0u; i < 7u; i++) {
        if (!pulse_wire_u32_parse(bytes, size, after + i * 5u, &values[i])) {
            return false;
        }
    }
    *out = (pulse_wire_stream_t){.channel = values[0],
                                 .index = values[1],
                                 .owed = values[2],
                                 .maxlength = values[3],
                                 .tlength = values[4],
                                 .prebuf = values[5],
                                 .minreq = values[6]};
    return true;
}

bool pulse_wire_request_parse(const uint8_t *bytes, uint32_t size, uint32_t after, uint32_t *out_channel,
                              uint32_t *out_bytes) {
    uint32_t channel = 0u, owed = 0u;
    if (!pulse_wire_u32_parse(bytes, size, after, &channel) || !pulse_wire_u32_parse(bytes, size, after + 5u, &owed)) {
        return false;
    }
    *out_channel = channel;
    *out_bytes = owed;
    return true;
}
