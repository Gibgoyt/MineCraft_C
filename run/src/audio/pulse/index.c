/* See README.md. A client of the sound server, with no libpulse: bytes from audio/pulse/wire over a Unix socket. This is the second of the two sockets
 * plans/CODING_STANDARD.md section 1 allows, and the only file with this one. */
#include "audio/pulse/index.h"

#include "file/index.h"
#include "time/index.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

enum : int32_t {
    PULSE_POLL_MS = 20,
};

enum : int64_t {
    PULSE_REPLY_WAIT_NS = 2000000000ll,
    /* The server asked for these bytes, so a socket with no room for them for this long is one that is stuck. */
    PULSE_SEND_WAIT_NS = 200000000ll,
};

static bool pulse_fail(audio_pulse_t *pulse, const char *what, int32_t error) {
    if (error != 0) {
        (void)snprintf(pulse->why, sizeof(pulse->why), "%s: %s", what, strerror(error));
    } else {
        (void)snprintf(pulse->why, sizeof(pulse->why), "%s", what);
    }
    return false;
}

void audio_pulse_start(audio_pulse_t *pulse, arena_t *arena) {
    memset(pulse, 0, sizeof(*pulse));
    pulse->read_fd = -1;
    pulse->write_fd = -1;
    pulse->borrowed_inbox = arena_alloc(arena, AUDIO_PULSE_INBOX_BYTES, 16u);
    pulse->borrowed_outbox = arena_alloc(arena, AUDIO_PULSE_OUTBOX_BYTES, 16u);
    pulse->borrowed_command = arena_alloc(arena, PULSE_WIRE_COMMAND_MAX_BYTES, 16u);
}

void audio_pulse_close(audio_pulse_t *pulse) {
    if (pulse->attached != 0u && pulse->is_socket != 0u) {
        (void)close(pulse->read_fd);
    }
    pulse->read_fd = -1;
    pulse->write_fd = -1;
    pulse->attached = 0u;
    pulse->is_socket = 0u;
    pulse->inbox_bytes = 0u;
    pulse->owed = 0u;
}

/* ---- Bytes ------------------------------------------------------------------------------------------------------- */

static int64_t pulse_write_some(const audio_pulse_t *pulse, const uint8_t *bytes, uint64_t length) {
    return pulse->is_socket != 0u ? send(pulse->write_fd, bytes, length, MSG_NOSIGNAL)
                                  : write(pulse->write_fd, bytes, length);
}

/* One packet goes in one piece or the connection is over, so that a half-sent packet can never have samples read as a header. */
static bool pulse_write_all(audio_pulse_t *pulse, const uint8_t *bytes, uint64_t length) {
    const int64_t deadline = time_now_ns() + PULSE_SEND_WAIT_NS;
    uint64_t sent = 0u;
    while (sent < length) {
        const int64_t now = pulse_write_some(pulse, bytes + sent, length - sent);
        if (now > 0) {
            sent += (uint64_t)now;
            continue;
        }
        if (now < 0 && errno == EINTR) {
            continue;
        }
        if (now < 0 && errno != EAGAIN) {
            return pulse_fail(pulse, "sending to the sound server", errno);
        }
        if (time_now_ns() > deadline) {
            return pulse_fail(pulse, "the sound server has stopped taking what it asked for", 0);
        }
        struct pollfd ready = {.fd = pulse->write_fd, .events = POLLOUT, .revents = 0};
        (void)poll(&ready, 1u, PULSE_POLL_MS);
    }
    return true;
}

static bool pulse_send_command(audio_pulse_t *pulse, uint32_t length) {
    if (length == 0u) {
        return pulse_fail(pulse, "a command too long for its buffer", 0);
    }
    uint8_t descriptor[PULSE_WIRE_DESCRIPTOR_BYTES];
    pulse_wire_descriptor(descriptor, length, PULSE_WIRE_CHANNEL_CONTROL);
    return pulse_write_all(pulse, descriptor, sizeof(descriptor)) &&
           pulse_write_all(pulse, pulse->borrowed_command, length);
}

/* One whole packet from the server. Everything it says unasked is here, and a reply waits in pulse->reply for whoever wants it. */
static bool pulse_take_packet(audio_pulse_t *pulse, const uint8_t *packet, uint32_t size) {
    pulse_wire_head_t head;
    if (!pulse_wire_head_parse(packet, size, &head)) {
        return pulse_fail(pulse, "the sound server sent something that makes no sense", 0);
    }
    uint32_t channel = 0u, bytes = 0u;
    switch (head.command) {
    case PULSE_WIRE_REPLY:
    case PULSE_WIRE_ERROR:
    case PULSE_WIRE_TIMEOUT:
        if (head.tag == pulse->wanted_tag) {
            const uint32_t rest = size - head.after;
            pulse->reply_bytes = rest < AUDIO_PULSE_REPLY_BYTES ? rest : AUDIO_PULSE_REPLY_BYTES;
            memcpy(pulse->reply, packet + head.after, pulse->reply_bytes);
            pulse->reply_failed = (uint8_t)(head.command == PULSE_WIRE_REPLY ? 0u : 1u);
            pulse->reply_came = 1u;
        }
        break;
    case PULSE_WIRE_REQUEST:
        if (pulse_wire_request_parse(packet, size, head.after, &channel, &bytes) && channel == pulse->stream.channel) {
            pulse->request_count++;
            pulse->owed = bytes > UINT32_MAX - pulse->owed ? UINT32_MAX : pulse->owed + bytes;
        }
        break;
    case PULSE_WIRE_UNDERFLOW:
        pulse->underflow_count++;
        break;
    case PULSE_WIRE_OVERFLOW:
        pulse->overflow_count++;
        break;
    case PULSE_WIRE_STREAM_KILLED:
        return pulse_fail(pulse, "the sound server took the stream away", 0);
    default:
        /* STARTED, a stream moved to another sink, a sink suspended: none needs anything from us. */
        break;
    }
    return true;
}

/* Everything whole in the inbox, and what is left of a packet moved to its start. */
static bool pulse_take_packets(audio_pulse_t *pulse) {
    uint32_t at = 0u;
    while (pulse->inbox_bytes - at >= PULSE_WIRE_DESCRIPTOR_BYTES) {
        uint32_t size = 0u, channel = 0u;
        pulse_wire_descriptor_parse(pulse->borrowed_inbox + at, &size, &channel);
        if (size > AUDIO_PULSE_INBOX_BYTES - PULSE_WIRE_DESCRIPTOR_BYTES) {
            return pulse_fail(pulse, "the sound server sent a packet too long to be real", 0);
        }
        if (pulse->inbox_bytes - at - PULSE_WIRE_DESCRIPTOR_BYTES < size) {
            break;
        }
        /* A playback connection gets no samples, so a packet on a channel of its own is nothing to read. */
        if (channel == PULSE_WIRE_CHANNEL_CONTROL &&
            !pulse_take_packet(pulse, pulse->borrowed_inbox + at + PULSE_WIRE_DESCRIPTOR_BYTES, size)) {
            return false;
        }
        at += PULSE_WIRE_DESCRIPTOR_BYTES + size;
    }
    if (at != 0u) {
        memmove(pulse->borrowed_inbox, pulse->borrowed_inbox + at, pulse->inbox_bytes - at);
        pulse->inbox_bytes -= at;
    }
    return true;
}

bool audio_pulse_service(audio_pulse_t *pulse, int32_t wait_ms) {
    struct pollfd ready = {.fd = pulse->read_fd, .events = POLLIN, .revents = 0};
    const int32_t polled = poll(&ready, 1u, wait_ms);
    if (polled < 0) {
        return errno == EINTR ? true : pulse_fail(pulse, "waiting on the sound server", errno);
    }
    if (polled == 0) {
        return true;
    }
    while (pulse->inbox_bytes < AUDIO_PULSE_INBOX_BYTES) {
        const int64_t got = read(pulse->read_fd, pulse->borrowed_inbox + pulse->inbox_bytes,
                                 AUDIO_PULSE_INBOX_BYTES - pulse->inbox_bytes);
        if (got == 0) {
            return pulse_fail(pulse, "the sound server closed the connection", 0);
        }
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN) {
                break;
            }
            return pulse_fail(pulse, "reading from the sound server", errno);
        }
        pulse->inbox_bytes += (uint32_t)got;
        if (!pulse_take_packets(pulse)) {
            return false;
        }
    }
    return pulse_take_packets(pulse);
}

/* ---- Saying hello ------------------------------------------------------------------------------------------------ */

/* Sends what is in the command buffer and waits for its reply. */
static bool pulse_ask(audio_pulse_t *pulse, uint32_t tag, uint32_t length, const char *what,
                      const audio_pulse_open_t *open) {
    pulse->wanted_tag = tag;
    pulse->reply_came = 0u;
    if (!pulse_send_command(pulse, length)) {
        return false;
    }
    const int64_t deadline = time_now_ns() + PULSE_REPLY_WAIT_NS;
    while (pulse->reply_came == 0u) {
        if (open->borrowed_stop != nullptr && atomic_load_explicit(open->borrowed_stop, memory_order_relaxed) != 0u) {
            return pulse_fail(pulse, "stopped while waiting for the sound server", 0);
        }
        if (time_now_ns() > deadline) {
            char line[AUDIO_PULSE_WHY_BYTES];
            (void)snprintf(line, sizeof(line), "the sound server did not answer: %s", what);
            return pulse_fail(pulse, line, 0);
        }
        if (!audio_pulse_service(pulse, PULSE_POLL_MS)) {
            return false;
        }
    }
    if (pulse->reply_failed != 0u) {
        uint32_t code = 0u;
        (void)pulse_wire_u32_parse(pulse->reply, pulse->reply_bytes, 0u, &code);
        char line[AUDIO_PULSE_WHY_BYTES];
        (void)snprintf(line, sizeof(line), "the sound server refused: %s, error %" PRIu32, what, code);
        return pulse_fail(pulse, line, 0);
    }
    return true;
}

/* The server's own file, as libpulse sends it. Never logged, and cleared once sent. Zeros without it, as pipewire-pulse does not look. */
static void pulse_cookie_read(const char *path, uint8_t cookie[PULSE_WIRE_COOKIE_BYTES]) {
    memset(cookie, 0, PULSE_WIRE_COOKIE_BYTES);
    uint64_t bytes = 0u;
    if (path == nullptr || file_read_whole(path, cookie, PULSE_WIRE_COOKIE_BYTES, &bytes) != FILE_OK ||
        bytes != PULSE_WIRE_COOKIE_BYTES) {
        memset(cookie, 0, PULSE_WIRE_COOKIE_BYTES);
    }
}

static bool pulse_hello(audio_pulse_t *pulse, const audio_pulse_open_t *open) {
    uint8_t cookie[PULSE_WIRE_COOKIE_BYTES];
    pulse_cookie_read(open->cookie_path, cookie);
    uint32_t tag = ++pulse->next_tag;
    uint32_t length = pulse_wire_auth(pulse->borrowed_command, PULSE_WIRE_COMMAND_MAX_BYTES, tag, cookie);
    memset(cookie, 0, sizeof(cookie));
    const bool answered = pulse_ask(pulse, tag, length, "who we are", open);
    memset(pulse->borrowed_command, 0, PULSE_WIRE_COMMAND_MAX_BYTES);
    if (!answered) {
        return false;
    }
    tag = ++pulse->next_tag;
    length = pulse_wire_client_name(pulse->borrowed_command, PULSE_WIRE_COMMAND_MAX_BYTES, tag, open->name);
    if (!pulse_ask(pulse, tag, length, "our name", open)) {
        return false;
    }
    tag = ++pulse->next_tag;
    length = pulse_wire_create_stream(pulse->borrowed_command, PULSE_WIRE_COMMAND_MAX_BYTES, tag, open->name,
                                      AUDIO_PULSE_TLENGTH_BYTES, open->muted != 0u);
    if (!pulse_ask(pulse, tag, length, "a stream", open)) {
        return false;
    }
    if (!pulse_wire_stream_parse(pulse->reply, pulse->reply_bytes, 0u, &pulse->stream)) {
        return pulse_fail(pulse, "the sound server's answer about the stream makes no sense", 0);
    }
    /* It may have asked for more before this came, on a channel not known yet: the reply's number is the whole of it. */
    pulse->owed = pulse->stream.owed;
    return true;
}

static void pulse_start(audio_pulse_t *pulse) {
    pulse->inbox_bytes = 0u;
    pulse->owed = 0u;
    pulse->stream = (pulse_wire_stream_t){.channel = PULSE_WIRE_CHANNEL_CONTROL};
    pulse->attached = 1u;
    pulse->why[0] = '\0';
}

bool audio_pulse_attach(audio_pulse_t *pulse, int32_t read_fd, int32_t write_fd, const audio_pulse_open_t *open) {
    pulse->read_fd = read_fd;
    pulse->write_fd = write_fd;
    pulse->is_socket = 0u;
    pulse_start(pulse);
    if (fcntl(read_fd, F_SETFL, O_NONBLOCK) != 0 || fcntl(write_fd, F_SETFL, O_NONBLOCK) != 0) {
        const bool failed = pulse_fail(pulse, "setting a descriptor not to wait", errno);
        audio_pulse_close(pulse);
        return failed;
    }
    if (!pulse_hello(pulse, open)) {
        audio_pulse_close(pulse);
        return false;
    }
    return true;
}

bool audio_pulse_connect(audio_pulse_t *pulse, const char *socket_path, const audio_pulse_open_t *open) {
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    const uint64_t path_bytes = strlen(socket_path);
    if (path_bytes >= sizeof(address.sun_path)) {
        return pulse_fail(pulse, "the sound server's socket path is too long", 0);
    }
    memcpy(address.sun_path, socket_path, path_bytes);
    const int32_t fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return pulse_fail(pulse, "making a Unix socket", errno);
    }
    pulse->read_fd = fd;
    pulse->write_fd = fd;
    pulse->is_socket = 1u;
    pulse_start(pulse);
    /* Non-blocking, a Unix socket either joins now or says the server's queue is full. */
    if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        (void)pulse_fail(pulse, socket_path, errno);
        audio_pulse_close(pulse);
        return false;
    }
    if (!pulse_hello(pulse, open)) {
        audio_pulse_close(pulse);
        return false;
    }
    return true;
}

/* ---- Samples ----------------------------------------------------------------------------------------------------- */

uint32_t audio_pulse_frames_owed(const audio_pulse_t *pulse) {
    const uint32_t frames = pulse->owed / PULSE_WIRE_FRAME_BYTES;
    return frames < AUDIO_PULSE_PCM_FRAMES ? frames : AUDIO_PULSE_PCM_FRAMES;
}

int16_t *audio_pulse_samples(audio_pulse_t *pulse) {
    return (int16_t *)(void *)(pulse->borrowed_outbox + PULSE_WIRE_DESCRIPTOR_BYTES);
}

bool audio_pulse_send(audio_pulse_t *pulse, uint32_t frames) {
    const uint32_t bytes = frames * PULSE_WIRE_FRAME_BYTES;
    pulse_wire_descriptor(pulse->borrowed_outbox, bytes, pulse->stream.channel);
    if (!pulse_write_all(pulse, pulse->borrowed_outbox, (uint64_t)PULSE_WIRE_DESCRIPTOR_BYTES + bytes)) {
        return false;
    }
    pulse->owed = bytes < pulse->owed ? pulse->owed - bytes : 0u;
    pulse->sent_bytes += bytes;
    return true;
}
