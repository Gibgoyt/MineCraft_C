/* See README.md. A window on the local X server, with no libX11: bytes from display/x11/wire over a Unix socket, and pixels
 * in shared memory. */
#include "display/x11/index.h"

#include "log/index.h"
#include "time/index.h"

#include <errno.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

enum : uint64_t {
    X11_INBOX_BYTES = 1ull << 18,
    X11_SETUP_BYTES = (1ull << 18) + 8u, /* the most a setup reply's 16-bit length can say */
    X11_EVENT_CAPACITY = 4096u,
    X11_AUTH_BYTES = 1ull << 16,
    X11_ID_WINDOW = 1u, /* offsets into the range of ids the server gave */
    X11_ID_GC = 2u,
    X11_ID_SEGMENT = 3u,
    X11_ID_PIXMAP = 4u,
    X11_ID_PIXMAP_GC = 5u,
    X11_ID_CURSOR = 6u,
    X11_ID_SEGMENT_SECOND = 7u, /* plan 008 phase 02: the second shared segment */
    X11_ATOM_STRING = 31u,      /* predefined by the protocol */
    X11_ATOM_ATOM = 4u,
    X11_ATOM_WM_NAME = 39u,
    X11_ATOM_WM_NORMAL_HINTS = 40u,
    X11_ATOM_WM_SIZE_HINTS = 41u,
    X11_POLLS_BETWEEN_GRABS = 30u,
};
enum : int32_t {
    X11_REPLY_TIMEOUT_MS = 5000,
};
enum : int64_t {
    X11_PUT_STALL_NS = 5000000000,
};

static const char X11_TITLE[] = "MineCraft_C";

void display_x11_completion_take(display_x11_t *x11, uint32_t segment, int64_t now_ns) {
    /* Each completion names its segment, so each is freed by its own. */
    for (uint32_t i = 0u; i < DISPLAY_X11_SEGMENTS; i++) {
        if (x11->segment[i] == segment && x11->put_pending[i] != 0u) {
            x11->put_pending[i] = 0u;
            x11->put_done_ns = i == x11->last_put ? now_ns : x11->put_done_ns;
        }
    }
}

void display_x11_put_note(display_x11_t *x11, uint32_t index, int64_t now_ns) {
    x11->put_pending[index] = 1u;
    x11->last_put = (uint8_t)index;
    x11->put_sent_ns = now_ns;
    x11->segment_sent_ns[index] = now_ns;
    x11->put_done_ns = 0;
    x11->frames++;
}

static bool x11_fail(display_x11_t *x11, const char *what, int32_t error) {
    if (x11->failed_call == nullptr) {
        x11->failed_call = what;
        x11->failed_errno = error;
    }
    return false;
}

static bool x11_send(display_x11_t *x11, const uint8_t *bytes, uint64_t length) {
    uint64_t sent = 0;
    while (sent < length) {
        /* MSG_NOSIGNAL: a server that has gone is a failure to report, not a SIGPIPE. */
        const int64_t now = send(x11->fd, bytes + sent, length - sent, MSG_NOSIGNAL);
        if (now < 0) {
            if (errno == EINTR) {
                continue;
            }
            return x11_fail(x11, "sending to the X server", errno);
        }
        sent += (uint64_t)now;
    }
    return true;
}

static void x11_event_push(display_x11_t *x11, const uint8_t *packet) {
    if (x11->event_count == X11_EVENT_CAPACITY) {
        x11->events_lost++;
        return;
    }
    const uint32_t slot = (x11->event_head + x11->event_count) % (uint32_t)X11_EVENT_CAPACITY;
    memcpy(x11->borrowed_events + (uint64_t)slot * WIRE_PACKET_BYTES, packet, WIRE_PACKET_BYTES);
    x11->event_count++;
}

/* One whole packet from the server. Input waits for a poll; the rest is dealt with here, as it can come at any time. */
static void x11_packet_take(display_x11_t *x11, const uint8_t *packet) {
    const uint8_t type = packet[0] & 0x7Fu;
    if (type == WIRE_PACKET_ERROR) {
        /* Always a bug here, not something to carry on from: a request that failed leaves later ones wrong. */
        LOG_ERROR("x11: the server refused a request: error %u, major opcode %u, minor %u", packet[1], packet[10],
                  (uint32_t)packet[8] | (uint32_t)packet[9] << 8);
        (void)x11_fail(x11, "a request the X server refused", 0);
    } else if (type == WIRE_PACKET_REPLY) {
        memcpy(x11->reply, packet, WIRE_PACKET_BYTES);
        x11->reply_ready = 1u;
    } else if (x11->shm_completion != 0u && type == x11->shm_completion) {
        display_x11_completion_take(x11, wire_shm_completion_segment(packet), time_now_ns());
    } else if (type == WIRE_PACKET_CLIENT_MESSAGE) {
        uint32_t message_type = 0;
        uint32_t data = 0;
        memcpy(&message_type, packet + 8, sizeof(message_type));
        memcpy(&data, packet + 12, sizeof(data));
        if (message_type == x11->wm_protocols && data == x11->wm_delete_window) {
            x11->close_requested = 1u;
        }
    } else if (type >= WIRE_PACKET_KEY_PRESS && type <= WIRE_PACKET_FOCUS_OUT) {
        x11_event_push(x11, packet);
    }
}

/* Takes what the socket has into the inbox, and every whole packet out of it. Waits up to timeout_ms if nothing is there. */
static bool x11_read(display_x11_t *x11, int32_t timeout_ms) {
    struct pollfd wait = {.fd = x11->fd, .events = POLLIN, .revents = 0};
    const int32_t ready = poll(&wait, 1u, timeout_ms);
    if (ready < 0) {
        return errno == EINTR ? true : x11_fail(x11, "waiting for the X server", errno);
    }
    if (ready == 0) {
        return true;
    }
    const int64_t got =
        recv(x11->fd, x11->borrowed_inbox + x11->inbox_bytes, X11_INBOX_BYTES - x11->inbox_bytes, MSG_DONTWAIT);
    if (got == 0) {
        return x11_fail(x11, "the X server closed the connection", 0);
    }
    if (got < 0) {
        if (errno == EAGAIN || errno == EINTR) {
            return true;
        }
        return x11_fail(x11, "reading from the X server", errno);
    }
    x11->inbox_bytes += (uint64_t)got;
    uint64_t at = 0;
    while (x11->inbox_bytes - at >= WIRE_PACKET_BYTES) {
        const uint64_t bytes = wire_packet_bytes(x11->borrowed_inbox + at);
        if (bytes > X11_INBOX_BYTES) {
            return x11_fail(x11, "a reply from the X server too long to hold", 0);
        }
        if (x11->inbox_bytes - at < bytes) {
            break;
        }
        x11_packet_take(x11, x11->borrowed_inbox + at);
        at += bytes;
    }
    memmove(x11->borrowed_inbox, x11->borrowed_inbox + at, x11->inbox_bytes - at);
    x11->inbox_bytes -= at;
    return x11->failed_call == nullptr;
}

/* Sends a request that has a reply and waits for it, in x11->reply. Events that come first are kept. */
static bool x11_round_trip(display_x11_t *x11, const uint8_t *request, uint32_t length) {
    x11->reply_ready = 0u;
    if (!x11_send(x11, request, length)) {
        return false;
    }
    const int64_t started_ns = time_now_ns();
    while (x11->reply_ready == 0u) {
        if (!x11_read(x11, X11_REPLY_TIMEOUT_MS)) {
            return false;
        }
        if (time_now_ns() - started_ns > (int64_t)X11_REPLY_TIMEOUT_MS * 1000000) {
            return x11_fail(x11, "no reply from the X server within five seconds", 0);
        }
    }
    return true;
}

static uint32_t x11_atom(display_x11_t *x11, const char *name) {
    uint8_t request[WIRE_REQUEST_MAX_BYTES];
    const uint32_t length = wire_intern_atom(request, name);
    if (!x11_round_trip(x11, request, length)) {
        return 0u;
    }
    uint32_t atom = 0;
    memcpy(&atom, x11->reply + 8, sizeof(atom));
    return atom;
}

static bool x11_connect(display_x11_t *x11, uint32_t display_number) {
    x11->fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (x11->fd < 0) {
        return x11_fail(x11, "making a Unix socket", errno);
    }
    x11->connected = 1u;
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "/tmp/.X11-unix/X%u", display_number);
    if (connect(x11->fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        return x11_fail(x11, "connecting to the X server's socket", errno);
    }
    return true;
}

/* Reads exactly this many bytes, for the handshake, which comes before any framing. */
static bool x11_read_exact(display_x11_t *x11, uint8_t *out, uint64_t length) {
    uint64_t got = 0;
    while (got < length) {
        struct pollfd wait = {.fd = x11->fd, .events = POLLIN, .revents = 0};
        const int32_t ready = poll(&wait, 1u, X11_REPLY_TIMEOUT_MS);
        if (ready == 0) {
            return x11_fail(x11, "no answer from the X server within five seconds", 0);
        }
        const int64_t now = ready < 0 ? -1 : recv(x11->fd, out + got, length - got, 0);
        if (now == 0) {
            return x11_fail(x11, "the X server closed the connection", 0);
        }
        if (now < 0) {
            if (errno == EINTR) {
                continue;
            }
            return x11_fail(x11, "reading from the X server", errno);
        }
        got += (uint64_t)now;
    }
    return true;
}

/* Reads the cookie if there is one. A file with none is no failure: a server with no authentication wants none. */
static bool x11_cookie_find(arena_t *arena, const char *auth_path, uint32_t display_number, uint8_t *out_cookie) {
    if (auth_path == nullptr) {
        return false;
    }
    FILE *const file = fopen(auth_path, "rb");
    if (file == nullptr) {
        return false;
    }
    uint8_t *const bytes = arena_alloc(arena, X11_AUTH_BYTES, 1u);
    const uint64_t length = fread(bytes, 1u, X11_AUTH_BYTES, file);
    fclose(file);
    const bool found = wire_xauth_find(bytes, length, display_number, out_cookie);
    memset(bytes, 0, X11_AUTH_BYTES); /* it is a password, and this memory lasts as long as the program */
    return found;
}

static bool x11_handshake(display_x11_t *x11, arena_t *arena, uint32_t display_number, const char *auth_path,
                          wire_setup_t *out_setup) {
    uint8_t cookie[WIRE_COOKIE_BYTES];
    const bool has_cookie = x11_cookie_find(arena, auth_path, display_number, cookie);
    uint8_t request[WIRE_SETUP_REQUEST_BYTES];
    const uint32_t request_bytes = wire_setup_request(request, has_cookie ? cookie : nullptr);
    const bool sent = x11_send(x11, request, request_bytes);
    memset(cookie, 0, sizeof(cookie));
    memset(request, 0, sizeof(request));
    if (!sent) {
        return false;
    }
    uint8_t *const reply = arena_alloc(arena, X11_SETUP_BYTES, 8u);
    if (!x11_read_exact(x11, reply, 8u)) {
        return false;
    }
    const uint64_t total = wire_setup_reply_bytes(reply);
    if (!x11_read_exact(x11, reply + 8, total - 8u)) {
        return false;
    }
    if (reply[0] != 1u) {
        const uint64_t reason_bytes = reply[0] == 0u ? reply[1] : total - 8u;
        LOG_ERROR("x11: the server said no: %.*s", (int32_t)reason_bytes, (const char *)reply + 8);
        return x11_fail(x11, "the X server refused the connection", 0);
    }
    if (!wire_setup_parse(reply, total, out_setup)) {
        return x11_fail(x11, "a setup reply from the X server that makes no sense", 0);
    }
    if (out_setup->usable == 0u) {
        return x11_fail(x11, "this X server's screen is not 24-bit colour in 32-bit pixels", 0);
    }
    return true;
}

static bool x11_extension_shm(display_x11_t *x11) {
    uint8_t request[WIRE_REQUEST_MAX_BYTES];
    const uint32_t length = wire_query_extension(request, "MIT-SHM");
    if (!x11_round_trip(x11, request, length)) {
        return false;
    }
    if (x11->reply[8] == 0u) {
        return x11_fail(x11, "this X server has no MIT-SHM, so frames cannot be shared with it", 0);
    }
    x11->shm_major = x11->reply[9];
    x11->shm_completion = x11->reply[10];
    return true;
}

/* One of buffers: shared memory, attached to the server under segment unless it is the spare, which only this process maps. */
static bool x11_shm_one(display_x11_t *x11, uint32_t index, uint32_t width, uint32_t height, uint32_t segment) {
    const uint64_t bytes = (uint64_t)width * height * DISPLAY_FLIP_BYTES_PER_PIXEL;
    x11->shm_id[index] = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
    if (x11->shm_id[index] < 0) {
        return x11_fail(x11, "making shared memory for the frame", errno);
    }
    void *const map = shmat(x11->shm_id[index], nullptr, 0);
    if (map == (void *)-1) {
        const int32_t error = errno;
        shmctl(x11->shm_id[index], IPC_RMID, nullptr);
        x11->shm_id[index] = -1;
        return x11_fail(x11, "mapping shared memory for the frame", error);
    }
    x11->owned_shm[index] = map;
    memset(x11->owned_shm[index], 0, bytes);
    x11->buffers[index] = (display_buffer_t){.owned_map = x11->owned_shm[index],
                                             .map_bytes = bytes,
                                             .pitch = width * DISPLAY_FLIP_BYTES_PER_PIXEL,
                                             .width = width,
                                             .height = height};
    if (index == DISPLAY_X11_SPARE) {
        shmctl(x11->shm_id[index], IPC_RMID, nullptr); /* only ours: gone when we let go */
        return true;
    }
    x11->segment[index] = segment;
    uint8_t request[WIRE_REQUEST_MAX_BYTES];
    uint32_t length = wire_shm_attach(request, x11->shm_major, segment, (uint32_t)x11->shm_id[index]);
    if (!x11_send(x11, request, length)) {
        return false;
    }
    /* Once the server has it, nothing else needs its name, and marked for removal it goes when both have let go, even after a crash. */
    length = wire_get_input_focus(request);
    const bool attached = x11_round_trip(x11, request, length);
    shmctl(x11->shm_id[index], IPC_RMID, nullptr);
    if (!attached) {
        return false;
    }
    x11->segment_attached[index] = 1u;
    return true;
}

/* Plan 008 phase 02: two segments used in turn, and the spare: 8.3 MB each at 1920x1080. */
static bool x11_shm_start(display_x11_t *x11, uint32_t width, uint32_t height, uint32_t id_base) {
    return x11_shm_one(x11, 0u, width, height, id_base + X11_ID_SEGMENT) &&
           x11_shm_one(x11, 1u, width, height, id_base + X11_ID_SEGMENT_SECOND) &&
           x11_shm_one(x11, DISPLAY_X11_SPARE, width, height, 0u);
}

/* A cursor that shows nothing, for while the pointer is held: the game draws its own. */
static bool x11_blank_cursor(display_x11_t *x11, uint32_t id_base) {
    const uint32_t pixmap = id_base + X11_ID_PIXMAP;
    const uint32_t pixmap_gc = id_base + X11_ID_PIXMAP_GC;
    x11->blank_cursor = id_base + X11_ID_CURSOR;
    uint8_t request[WIRE_REQUEST_MAX_BYTES];
    uint32_t length = wire_create_pixmap(request, pixmap, x11->window, 1u, 1u, 1u);
    bool ok = x11_send(x11, request, length);
    length = wire_create_gc(request, pixmap_gc, pixmap, 0u);
    ok = ok && x11_send(x11, request, length);
    length = wire_fill_rectangle(request, pixmap, pixmap_gc, 1u, 1u);
    ok = ok && x11_send(x11, request, length);
    length = wire_create_cursor(request, x11->blank_cursor, pixmap, pixmap);
    return ok && x11_send(x11, request, length);
}

static bool x11_window_start(display_x11_t *x11, const wire_setup_t *setup, uint32_t width, uint32_t height,
                             bool fullscreen) {
    x11->window = setup->id_base + X11_ID_WINDOW;
    x11->gc = setup->id_base + X11_ID_GC;
    x11->depth = setup->depth;
    uint8_t request[WIRE_REQUEST_MAX_BYTES];
    uint32_t length = wire_create_window(request, x11->window, setup->root, setup->visual, setup->depth,
                                         (uint16_t)width, (uint16_t)height);
    if (!x11_send(x11, request, length)) {
        return false;
    }
    length = wire_create_gc(request, x11->gc, x11->window, 0u);
    if (!x11_send(x11, request, length)) {
        return false;
    }
    length = wire_change_property(request, x11->window, X11_ATOM_WM_NAME, X11_ATOM_STRING, 8u, X11_TITLE,
                                  (uint32_t)sizeof(X11_TITLE) - 1u);
    if (!x11_send(x11, request, length)) {
        return false;
    }
    /* The frame has one size, so the window has too: a window manager that drew a bigger one would show a black edge. */
    uint32_t hints[18];
    wire_size_hints_fixed(hints, width, height);
    length =
        wire_change_property(request, x11->window, X11_ATOM_WM_NORMAL_HINTS, X11_ATOM_WM_SIZE_HINTS, 32u, hints, 18u);
    if (!x11_send(x11, request, length)) {
        return false;
    }
    x11->wm_protocols = x11_atom(x11, "WM_PROTOCOLS");
    x11->wm_delete_window = x11_atom(x11, "WM_DELETE_WINDOW");
    if (x11->failed_call != nullptr) {
        return false;
    }
    length =
        wire_change_property(request, x11->window, x11->wm_protocols, X11_ATOM_ATOM, 32u, &x11->wm_delete_window, 1u);
    if (!x11_send(x11, request, length)) {
        return false;
    }
    if (fullscreen) {
        const uint32_t state = x11_atom(x11, "_NET_WM_STATE");
        const uint32_t state_fullscreen = x11_atom(x11, "_NET_WM_STATE_FULLSCREEN");
        if (x11->failed_call != nullptr) {
            return false;
        }
        length = wire_change_property(request, x11->window, state, X11_ATOM_ATOM, 32u, &state_fullscreen, 1u);
        if (!x11_send(x11, request, length)) {
            return false;
        }
    }
    if (!x11_blank_cursor(x11, setup->id_base)) {
        return false;
    }
    length = wire_map_window(request, x11->window);
    return x11_send(x11, request, length);
}

uint64_t display_x11_arena_bytes(void) {
    return X11_INBOX_BYTES + X11_SETUP_BYTES + X11_AUTH_BYTES + X11_EVENT_CAPACITY * WIRE_PACKET_BYTES + 256u;
}

/* Says what went wrong while the terminal can show it. errno is 0 for a failure that is not a failed call. */
static void x11_log_failure(const display_x11_t *x11) {
    LOG_ERROR("x11: %s%s%s", x11->failed_call, x11->failed_errno != 0 ? ": " : "",
              x11->failed_errno != 0 ? strerror(x11->failed_errno) : "");
}

bool display_x11_init(display_x11_t *x11, arena_t *arena, const display_x11_config_t *config, const char *display_name,
                      const char *auth_path) {
    memset(x11, 0, sizeof(*x11));
    x11->fd = -1;
    for (uint32_t i = 0u; i < DISPLAY_X11_BUFFERS; i++) {
        x11->shm_id[i] = -1;
    }
    uint32_t display_number = 0;
    if (!wire_display_number(display_name, &display_number)) {
        LOG_ERROR("x11: DISPLAY=%s is not a server on this machine, and that is all this speaks to",
                  display_name != nullptr ? display_name : "");
        return false;
    }
    x11->borrowed_inbox = arena_alloc(arena, X11_INBOX_BYTES, 8u);
    x11->borrowed_events = arena_alloc(arena, X11_EVENT_CAPACITY * WIRE_PACKET_BYTES, 8u);
    wire_setup_t setup;
    if (!x11_connect(x11, display_number) || !x11_handshake(x11, arena, display_number, auth_path, &setup)) {
        goto cleanup;
    }
    const uint32_t width = config->width < setup.screen_width ? config->width : setup.screen_width;
    const uint32_t height = config->height < setup.screen_height ? config->height : setup.screen_height;
    if (!x11_extension_shm(x11) || !x11_window_start(x11, &setup, width, height, config->fullscreen != 0u) ||
        !x11_shm_start(x11, width, height, setup.id_base)) {
        goto cleanup;
    }
    wire_pointer_start(&x11->pointer, width, height);
    x11->running = 1u;
    return true;

cleanup:
    x11_log_failure(x11);
    x11->running = 1u;
    display_x11_deinit(x11);
    return false;
}

void display_x11_deinit(display_x11_t *x11) {
    if (x11->running == 0u) {
        return;
    }
    for (uint32_t i = 0u; i < DISPLAY_X11_SEGMENTS; i++) {
        if (x11->segment_attached[i] != 0u && x11->failed_call == nullptr) {
            uint8_t request[WIRE_REQUEST_MAX_BYTES];
            const uint32_t length = wire_shm_detach(request, x11->shm_major, x11->segment[i]);
            (void)x11_send(x11, request, length);
        }
    }
    /* Closing lets the server drop the window, the grab and all else that is ours. */
    if (x11->connected != 0u) {
        close(x11->fd);
    }
    for (uint32_t i = 0u; i < DISPLAY_X11_BUFFERS; i++) {
        if (x11->owned_shm[i] != nullptr) {
            shmdt(x11->owned_shm[i]);
        }
    }
    memset(x11, 0, sizeof(*x11));
}

bool display_x11_service(display_x11_t *x11) {
    if (!x11_read(x11, 0)) {
        return false;
    }
    return x11->close_requested == 0u;
}

bool display_x11_free_buffer(display_x11_t *x11, uint32_t *out_index) {
    x11->wait_from_ns = time_now_ns();
    *out_index = DISPLAY_X11_SPARE;
    /* What has come, never waiting: a completion for either is enough. */
    const bool read = x11_read(x11, 0);
    x11->wait_ns = time_now_ns() - x11->wait_from_ns;
    if (!read || x11->failed_call != nullptr || x11->close_requested != 0u) {
        return false;
    }
    const uint32_t other = x11->last_put ^ 1u;
    if (x11->put_pending[other] == 0u) {
        *out_index = other;
        return true;
    }
    if (x11->put_pending[x11->last_put] == 0u) {
        *out_index = x11->last_put;
        return true;
    }
    /* Both with the server. The older has had five seconds: it is stuck, and skipping would only hide it. */
    if (x11->wait_from_ns - x11->segment_sent_ns[other] > X11_PUT_STALL_NS) {
        return x11_fail(x11, "the X server has not shown a frame for five seconds", 0);
    }
    x11->puts_skipped++;
    return true;
}

bool display_x11_present(display_x11_t *x11, uint32_t index) {
    if (index >= DISPLAY_X11_SEGMENTS || x11->put_pending[index] != 0u) {
        return x11_fail(x11, "a frame put from a buffer the X server may still be reading", 0);
    }
    uint8_t request[WIRE_REQUEST_MAX_BYTES];
    const uint32_t length =
        wire_shm_put_image(request, x11->shm_major, x11->window, x11->gc, (uint16_t)x11->buffers[index].width,
                           (uint16_t)x11->buffers[index].height, x11->depth, x11->segment[index]);
    display_x11_put_note(x11, index, time_now_ns());
    return x11_send(x11, request, length);
}

/* ---- Input ------------------------------------------------------------------------------------------------------- */

static void x11_pointer_capture(display_x11_t *x11) {
    if (x11->polls_until_grab != 0u) {
        x11->polls_until_grab--;
        return;
    }
    uint8_t request[WIRE_REQUEST_MAX_BYTES];
    uint32_t length = wire_grab_pointer(request, x11->window, x11->blank_cursor);
    if (!x11_round_trip(x11, request, length)) {
        return;
    }
    if (x11->reply[1] != 0u) {
        x11->polls_until_grab = X11_POLLS_BETWEEN_GRABS; /* someone holds it, or the window is not on screen yet */
        return;
    }
    x11->captured = 1u;
    wire_pointer_start(&x11->pointer, x11->buffers[0].width, x11->buffers[0].height);
    length = wire_warp_pointer(request, x11->window, (int16_t)x11->pointer.center_x, (int16_t)x11->pointer.center_y);
    (void)x11_send(x11, request, length);
}

static void x11_pointer_release(display_x11_t *x11) {
    x11->polls_until_grab = 0u;
    if (x11->captured == 0u) {
        return;
    }
    x11->captured = 0u;
    uint8_t request[WIRE_REQUEST_MAX_BYTES];
    const uint32_t length = wire_ungrab_pointer(request);
    (void)x11_send(x11, request, length);
}

static const uint8_t *x11_event_at(const display_x11_t *x11, uint32_t index) {
    const uint32_t slot = (x11->event_head + index) % (uint32_t)X11_EVENT_CAPACITY;
    return x11->borrowed_events + (uint64_t)slot * WIRE_PACKET_BYTES;
}

static void x11_event_drop(display_x11_t *x11) {
    x11->event_head = (x11->event_head + 1u) % (uint32_t)X11_EVENT_CAPACITY;
    x11->event_count--;
}

static void x11_take_key(const display_x11_sink_t *sink, const uint8_t *packet, int32_t value) {
    uint32_t code = 0;
    if (wire_key_code(packet[1], &code)) {
        sink->key(sink->context, code, value);
    }
}

static void x11_take_button(display_x11_t *x11, const display_x11_sink_t *sink, const uint8_t *packet, bool down) {
    if (x11->captured == 0u) {
        return; /* the click that gives the window focus is for the window manager, not for mining */
    }
    const wire_button_t button = wire_button(packet[1]);
    if (button.kind == WIRE_BUTTON_KEY) {
        sink->key(sink->context, button.code, down ? 1 : 0);
    } else if (button.kind == WIRE_BUTTON_WHEEL && down) {
        sink->wheel(sink->context, button.value, 0);
    } else if (button.kind == WIRE_BUTTON_HWHEEL && down) {
        sink->wheel(sink->context, 0, button.value);
    }
}

static void x11_take_motion(display_x11_t *x11, const display_x11_sink_t *sink, const uint8_t *packet) {
    if (x11->captured == 0u) {
        return;
    }
    int16_t x = 0;
    int16_t y = 0;
    memcpy(&x, packet + 24, sizeof(x));
    memcpy(&y, packet + 26, sizeof(y));
    int32_t dx = 0;
    int32_t dy = 0;
    if (wire_pointer_take(&x11->pointer, x, y, &dx, &dy)) {
        sink->motion(sink->context, dx, dy);
    }
}

static void x11_take_focus(display_x11_t *x11, const display_x11_sink_t *sink, const uint8_t *packet, bool in) {
    if (!wire_focus_is_real(packet)) {
        return; /* someone's grab for a moment, such as a menu: the window has not been left */
    }
    x11->focused = in ? 1u : 0u;
    if (!in) {
        x11->focus_losses++;
        x11_pointer_release(x11);
        sink->release_all(sink->context);
    }
}

void display_x11_poll(display_x11_t *x11, const display_x11_sink_t *sink) {
    (void)x11_read(x11, 0);
    if (x11->focused != 0u && x11->captured == 0u) {
        x11_pointer_capture(x11);
    }
    while (x11->event_count != 0u) {
        const uint8_t *const packet = x11_event_at(x11, 0u);
        const uint8_t type = packet[0] & 0x7Fu;
        if (type == WIRE_PACKET_KEY_RELEASE) {
            /* A repeat's press comes in the same write as its release, and so nearly always in the same read. Held back for one
             * poll, which is what is left of the rest. */
            if (x11->event_count == 1u && x11->release_held == 0u) {
                x11->release_held = 1u;
                break;
            }
            x11->release_held = 0u;
            if (x11->event_count >= 2u && wire_is_auto_repeat(packet, x11_event_at(x11, 1u))) {
                x11_take_key(sink, packet, 2);
                x11_event_drop(x11);
                x11_event_drop(x11);
                continue;
            }
            x11_take_key(sink, packet, 0);
        } else if (type == WIRE_PACKET_KEY_PRESS) {
            x11_take_key(sink, packet, 1);
        } else if (type == WIRE_PACKET_BUTTON_PRESS || type == WIRE_PACKET_BUTTON_RELEASE) {
            x11_take_button(x11, sink, packet, type == WIRE_PACKET_BUTTON_PRESS);
        } else if (type == WIRE_PACKET_MOTION) {
            x11_take_motion(x11, sink, packet);
        } else if (type == WIRE_PACKET_FOCUS_IN || type == WIRE_PACKET_FOCUS_OUT) {
            x11_take_focus(x11, sink, packet, type == WIRE_PACKET_FOCUS_IN);
        }
        x11_event_drop(x11);
    }
    if (x11->captured != 0u && wire_pointer_wants_warp(&x11->pointer)) {
        uint8_t request[WIRE_REQUEST_MAX_BYTES];
        const uint32_t length =
            wire_warp_pointer(request, x11->window, (int16_t)x11->pointer.center_x, (int16_t)x11->pointer.center_y);
        (void)x11_send(x11, request, length);
    }
}
