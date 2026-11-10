/* See README.md. The X11 protocol as bytes: nothing here touches a socket, a file or memory it was not given. */
#include "display/x11/wire/index.h"

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    WIRE_SETUP_FIXED_BYTES = 40u,
    WIRE_FORMAT_BYTES = 8u,
    WIRE_SCREEN_BYTES = 40u,
    WIRE_DEPTH_BYTES = 8u,
    WIRE_VISUAL_BYTES = 24u,
    WIRE_VISUAL_TRUE_COLOR = 4u,
    WIRE_CW_BACK_PIXEL = 0x2u,
    WIRE_CW_EVENT_MASK = 0x800u,
    WIRE_GC_FOREGROUND = 0x4u,
    WIRE_EVENT_KEY_PRESS = 0x1u,
    WIRE_EVENT_KEY_RELEASE = 0x2u,
    WIRE_EVENT_BUTTON_PRESS = 0x4u,
    WIRE_EVENT_BUTTON_RELEASE = 0x8u,
    WIRE_EVENT_POINTER_MOTION = 0x40u,
    WIRE_EVENT_FOCUS_CHANGE = 0x200000u,
    WIRE_SHM_MINOR_ATTACH = 1u,
    WIRE_SHM_MINOR_DETACH = 2u,
    WIRE_SHM_MINOR_PUT_IMAGE = 3u,
    WIRE_FORMAT_ZPIXMAP = 2u,
    WIRE_HINT_MIN_SIZE = 16u,
    WIRE_HINT_MAX_SIZE = 32u,
    WIRE_GENERIC_EVENT = 35u,
};

static const char WIRE_COOKIE_NAME[] = "MIT-MAGIC-COOKIE-1";

static void put16(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *out, uint32_t value) {
    put16(out, value);
    put16(out + 2, value >> 16);
}

static uint32_t get16(const uint8_t *in) {
    return (uint32_t)in[0] | (uint32_t)in[1] << 8;
}

static uint32_t get32(const uint8_t *in) {
    return get16(in) | get16(in + 2) << 16;
}

static uint32_t wire_pad4(uint32_t bytes) {
    return (bytes + 3u) & ~3u;
}

/* The fixed head of every request: its opcode, one byte for its own use, and its length in 4-byte units. */
static uint32_t wire_head(uint8_t *out, uint8_t opcode, uint8_t data, uint32_t bytes) {
    memset(out, 0, bytes);
    out[0] = opcode;
    out[1] = data;
    put16(out + 2, bytes / 4u);
    return bytes;
}

bool wire_display_number(const char *display, uint32_t *out_number) {
    if (display == nullptr) {
        return false;
    }
    const char *const colon = strrchr(display, ':');
    if (colon == nullptr) {
        return false;
    }
    const uint64_t host_bytes = (uint64_t)(colon - display);
    if (host_bytes != 0u && !(host_bytes == 4u && memcmp(display, "unix", 4u) == 0)) {
        return false;
    }
    if (colon[1] < '0' || colon[1] > '9') {
        return false;
    }
    char *end = nullptr;
    const uint64_t number = strtoul(colon + 1, &end, 10);
    if ((*end != '\0' && *end != '.') || number > UINT16_MAX) {
        return false;
    }
    *out_number = (uint32_t)number;
    return true;
}

/* A counted string in an Xauthority file, whose lengths alone are big-endian. */
static bool wire_xauth_field(const uint8_t *bytes, uint64_t length, uint64_t *at, const uint8_t **out,
                             uint32_t *out_bytes) {
    if (length - *at < 2u) {
        return false;
    }
    const uint32_t count = (uint32_t)bytes[*at] << 8 | (uint32_t)bytes[*at + 1u];
    *at += 2u;
    if (length - *at < count) {
        return false;
    }
    *out = bytes + *at;
    *out_bytes = count;
    *at += count;
    return true;
}

bool wire_xauth_find(const uint8_t *bytes, uint64_t length, uint32_t display_number,
                     uint8_t out_cookie[WIRE_COOKIE_BYTES]) {
    uint64_t at = 0;
    while (length - at >= 2u) {
        at += 2u; /* the family: a machine this runs on has one name, so which one it says does not narrow it */
        const uint8_t *field = nullptr;
        uint32_t field_bytes = 0;
        const uint8_t *number = nullptr;
        uint32_t number_bytes = 0;
        const uint8_t *name = nullptr;
        uint32_t name_bytes = 0;
        if (!wire_xauth_field(bytes, length, &at, &field, &field_bytes) ||
            !wire_xauth_field(bytes, length, &at, &number, &number_bytes) ||
            !wire_xauth_field(bytes, length, &at, &name, &name_bytes) ||
            !wire_xauth_field(bytes, length, &at, &field, &field_bytes)) {
            return false;
        }
        uint32_t parsed = 0;
        for (uint32_t i = 0; i < number_bytes && parsed <= UINT16_MAX; i++) {
            parsed = parsed * 10u + (uint32_t)(number[i] - (uint8_t)'0');
        }
        const bool is_cookie =
            name_bytes == sizeof(WIRE_COOKIE_NAME) - 1u && memcmp(name, WIRE_COOKIE_NAME, name_bytes) == 0;
        if (is_cookie && field_bytes == WIRE_COOKIE_BYTES && (number_bytes == 0u || parsed == display_number)) {
            memcpy(out_cookie, field, WIRE_COOKIE_BYTES);
            return true;
        }
    }
    return false;
}

uint32_t wire_setup_request(uint8_t *out, const uint8_t *cookie) {
    const uint32_t name_bytes = cookie != nullptr ? (uint32_t)sizeof(WIRE_COOKIE_NAME) - 1u : 0u;
    const uint32_t data_bytes = cookie != nullptr ? WIRE_COOKIE_BYTES : 0u;
    const uint32_t total = 12u + wire_pad4(name_bytes) + data_bytes;
    memset(out, 0, total);
    out[0] = (uint8_t)'l'; /* little-endian, from here on and in both directions */
    put16(out + 2, 11u);
    put16(out + 6, name_bytes);
    put16(out + 8, data_bytes);
    if (cookie != nullptr) {
        memcpy(out + 12, WIRE_COOKIE_NAME, name_bytes);
        memcpy(out + 12 + wire_pad4(name_bytes), cookie, data_bytes);
    }
    return total;
}

uint64_t wire_setup_reply_bytes(const uint8_t header[8]) {
    return 8u + (uint64_t)get16(header + 6) * 4u;
}

/* Looks through the root's depths for its visual, which must have the three colours where XRGB8888 has them. */
static bool wire_visual_usable(const uint8_t *screen, const uint8_t *end, uint32_t visual, uint8_t depth) {
    const uint32_t depth_count = screen[39];
    const uint8_t *at = screen + WIRE_SCREEN_BYTES;
    for (uint32_t d = 0; d < depth_count; d++) {
        if (end - at < (int64_t)WIRE_DEPTH_BYTES) {
            return false;
        }
        const uint8_t this_depth = at[0];
        const uint32_t visual_count = get16(at + 2);
        at += WIRE_DEPTH_BYTES;
        if (end - at < (int64_t)visual_count * WIRE_VISUAL_BYTES) {
            return false;
        }
        for (uint32_t v = 0; v < visual_count; v++, at += WIRE_VISUAL_BYTES) {
            if (this_depth == depth && get32(at) == visual) {
                return at[4] == WIRE_VISUAL_TRUE_COLOR && get32(at + 8) == 0xFF0000u && get32(at + 12) == 0x00FF00u &&
                       get32(at + 16) == 0x0000FFu;
            }
        }
    }
    return false;
}

bool wire_setup_parse(const uint8_t *reply, uint64_t length, wire_setup_t *out) {
    memset(out, 0, sizeof(*out));
    if (length < WIRE_SETUP_FIXED_BYTES || reply[0] != 1u) {
        return false;
    }
    const uint32_t vendor_bytes = get16(reply + 24);
    const uint32_t screen_count = reply[28];
    const uint32_t format_count = reply[29];
    const uint64_t formats_at = WIRE_SETUP_FIXED_BYTES + (uint64_t)wire_pad4(vendor_bytes);
    const uint64_t screen_at = formats_at + (uint64_t)format_count * WIRE_FORMAT_BYTES;
    if (screen_count == 0u || length < screen_at + WIRE_SCREEN_BYTES) {
        return false;
    }
    const uint8_t *const screen = reply + screen_at;
    out->id_base = get32(reply + 12);
    out->id_mask = get32(reply + 16);
    out->root = get32(screen);
    out->visual = get32(screen + 32);
    out->screen_width = (uint16_t)get16(screen + 20);
    out->screen_height = (uint16_t)get16(screen + 22);
    out->depth = screen[38];
    bool bits_32 = false;
    for (uint32_t i = 0; i < format_count; i++) {
        const uint8_t *const format = reply + formats_at + (uint64_t)i * WIRE_FORMAT_BYTES;
        bits_32 = bits_32 || (format[0] == out->depth && format[1] == 32u);
    }
    const bool little_endian = reply[30] == 0u;
    out->usable = (uint8_t)(out->depth == 24u && bits_32 && little_endian &&
                            wire_visual_usable(screen, reply + length, out->visual, out->depth));
    return true;
}

uint64_t wire_packet_bytes(const uint8_t packet[WIRE_PACKET_BYTES]) {
    const uint8_t type = packet[0] & 0x7Fu;
    if (type == WIRE_PACKET_REPLY || type == WIRE_GENERIC_EVENT) {
        return WIRE_PACKET_BYTES + (uint64_t)get32(packet + 4) * 4u;
    }
    return WIRE_PACKET_BYTES;
}

/* A name counted in the request and padded after it. The two callers write the same shape. */
static uint32_t wire_named(uint8_t *out, uint8_t opcode, const char *name) {
    const uint32_t name_bytes = (uint32_t)strlen(name);
    const uint32_t total = 8u + wire_pad4(name_bytes);
    wire_head(out, opcode, 0u, total);
    put16(out + 4, name_bytes);
    memcpy(out + 8, name, name_bytes);
    return total;
}

uint32_t wire_query_extension(uint8_t *out, const char *name) {
    return wire_named(out, 98u, name);
}

uint32_t wire_intern_atom(uint8_t *out, const char *name) {
    return wire_named(out, 16u, name);
}

uint32_t wire_get_input_focus(uint8_t *out) {
    return wire_head(out, 43u, 0u, 4u);
}

uint32_t wire_create_window(uint8_t *out, uint32_t window, uint32_t parent, uint32_t visual, uint8_t depth,
                            uint16_t width, uint16_t height) {
    const uint32_t total = wire_head(out, 1u, depth, 40u);
    put32(out + 4, window);
    put32(out + 8, parent);
    put16(out + 16, width);
    put16(out + 18, height);
    put16(out + 22, 1u); /* InputOutput */
    put32(out + 24, visual);
    put32(out + 28, WIRE_CW_BACK_PIXEL | WIRE_CW_EVENT_MASK);
    put32(out + 32, 0u); /* black, until the first frame */
    put32(out + 36, WIRE_EVENT_KEY_PRESS | WIRE_EVENT_KEY_RELEASE | WIRE_EVENT_BUTTON_PRESS |
                        WIRE_EVENT_BUTTON_RELEASE | WIRE_EVENT_POINTER_MOTION | WIRE_EVENT_FOCUS_CHANGE);
    return total;
}

uint32_t wire_map_window(uint8_t *out, uint32_t window) {
    const uint32_t total = wire_head(out, 8u, 0u, 8u);
    put32(out + 4, window);
    return total;
}

uint32_t wire_create_gc(uint8_t *out, uint32_t gc, uint32_t drawable, uint32_t foreground) {
    const uint32_t total = wire_head(out, 55u, 0u, 20u);
    put32(out + 4, gc);
    put32(out + 8, drawable);
    put32(out + 12, WIRE_GC_FOREGROUND);
    put32(out + 16, foreground);
    return total;
}

uint32_t wire_change_property(uint8_t *out, uint32_t window, uint32_t property, uint32_t type, uint8_t format,
                              const void *data, uint32_t units) {
    const uint32_t data_bytes = units * format / 8u;
    const uint32_t total = 24u + wire_pad4(data_bytes);
    wire_head(out, 18u, 0u, total); /* Replace */
    put32(out + 4, window);
    put32(out + 8, property);
    put32(out + 12, type);
    out[16] = format;
    put32(out + 20, units);
    memcpy(out + 24, data, data_bytes);
    return total;
}

uint32_t wire_create_pixmap(uint8_t *out, uint32_t pixmap, uint32_t drawable, uint8_t depth, uint16_t width,
                            uint16_t height) {
    const uint32_t total = wire_head(out, 53u, depth, 16u);
    put32(out + 4, pixmap);
    put32(out + 8, drawable);
    put16(out + 12, width);
    put16(out + 14, height);
    return total;
}

uint32_t wire_fill_rectangle(uint8_t *out, uint32_t drawable, uint32_t gc, uint16_t width, uint16_t height) {
    const uint32_t total = wire_head(out, 70u, 0u, 20u);
    put32(out + 4, drawable);
    put32(out + 8, gc);
    put16(out + 16, width);
    put16(out + 18, height);
    return total;
}

uint32_t wire_create_cursor(uint8_t *out, uint32_t cursor, uint32_t source, uint32_t mask) {
    const uint32_t total = wire_head(out, 93u, 0u, 32u);
    put32(out + 4, cursor);
    put32(out + 8, source);
    put32(out + 12, mask);
    return total;
}

uint32_t wire_grab_pointer(uint8_t *out, uint32_t window, uint32_t cursor) {
    const uint32_t total = wire_head(out, 26u, 0u, 24u);
    put32(out + 4, window);
    put16(out + 8, WIRE_EVENT_BUTTON_PRESS | WIRE_EVENT_BUTTON_RELEASE | WIRE_EVENT_POINTER_MOTION);
    out[10] = 1u; /* the pointer and the keyboard both go on as normal */
    out[11] = 1u;
    put32(out + 12, window); /* held inside it */
    put32(out + 16, cursor);
    return total;
}

uint32_t wire_ungrab_pointer(uint8_t *out) {
    return wire_head(out, 27u, 0u, 8u);
}

uint32_t wire_warp_pointer(uint8_t *out, uint32_t window, int16_t x, int16_t y) {
    const uint32_t total = wire_head(out, 41u, 0u, 24u);
    put32(out + 8, window);
    put16(out + 20, (uint16_t)x);
    put16(out + 22, (uint16_t)y);
    return total;
}

uint32_t wire_shm_attach(uint8_t *out, uint8_t major, uint32_t segment, uint32_t shm_id) {
    const uint32_t total = wire_head(out, major, WIRE_SHM_MINOR_ATTACH, 16u);
    put32(out + 4, segment);
    put32(out + 8, shm_id);
    out[12] = 1u; /* the server only reads it */
    return total;
}

uint32_t wire_shm_detach(uint8_t *out, uint8_t major, uint32_t segment) {
    const uint32_t total = wire_head(out, major, WIRE_SHM_MINOR_DETACH, 8u);
    put32(out + 4, segment);
    return total;
}

uint32_t wire_shm_put_image(uint8_t *out, uint8_t major, uint32_t drawable, uint32_t gc, uint16_t width,
                            uint16_t height, uint8_t depth, uint32_t segment) {
    const uint32_t total = wire_head(out, major, WIRE_SHM_MINOR_PUT_IMAGE, 40u);
    put32(out + 4, drawable);
    put32(out + 8, gc);
    put16(out + 12, width);
    put16(out + 14, height);
    put16(out + 20, width);
    put16(out + 22, height);
    out[28] = depth;
    out[29] = WIRE_FORMAT_ZPIXMAP;
    out[30] = 1u; /* an event when done: until then the server may still read the pixels */
    put32(out + 32, segment);
    return total;
}

void wire_size_hints_fixed(uint32_t out[18], uint32_t width, uint32_t height) {
    memset(out, 0, 18u * sizeof(out[0]));
    out[0] = WIRE_HINT_MIN_SIZE | WIRE_HINT_MAX_SIZE;
    out[5] = width;
    out[6] = height;
    out[7] = width;
    out[8] = height;
}

bool wire_key_code(uint8_t keycode, uint32_t *out_code) {
    if (keycode < WIRE_KEYCODE_OFFSET) {
        return false;
    }
    *out_code = (uint32_t)keycode - WIRE_KEYCODE_OFFSET;
    return true;
}

wire_button_t wire_button(uint8_t detail) {
    switch (detail) {
    case 1u:
        return (wire_button_t){.code = BTN_LEFT, .kind = WIRE_BUTTON_KEY};
    case 2u:
        return (wire_button_t){.code = BTN_MIDDLE, .kind = WIRE_BUTTON_KEY};
    case 3u:
        return (wire_button_t){.code = BTN_RIGHT, .kind = WIRE_BUTTON_KEY};
    case 4u:
        return (wire_button_t){.value = 1, .kind = WIRE_BUTTON_WHEEL};
    case 5u:
        return (wire_button_t){.value = -1, .kind = WIRE_BUTTON_WHEEL};
    case 6u:
        return (wire_button_t){.value = -1, .kind = WIRE_BUTTON_HWHEEL};
    case 7u:
        return (wire_button_t){.value = 1, .kind = WIRE_BUTTON_HWHEEL};
    case 8u:
        return (wire_button_t){.code = BTN_SIDE, .kind = WIRE_BUTTON_KEY};
    case 9u:
        return (wire_button_t){.code = BTN_EXTRA, .kind = WIRE_BUTTON_KEY};
    default:
        return (wire_button_t){.kind = WIRE_BUTTON_NONE};
    }
}

bool wire_is_auto_repeat(const uint8_t release[WIRE_PACKET_BYTES], const uint8_t next[WIRE_PACKET_BYTES]) {
    return (next[0] & 0x7Fu) == WIRE_PACKET_KEY_PRESS && next[1] == release[1] && get32(next + 4) == get32(release + 4);
}

bool wire_focus_is_real(const uint8_t packet[WIRE_PACKET_BYTES]) {
    return packet[8] != WIRE_FOCUS_MODE_GRAB && packet[8] != WIRE_FOCUS_MODE_UNGRAB;
}

void wire_pointer_start(wire_pointer_t *pointer, uint32_t width, uint32_t height) {
    memset(pointer, 0, sizeof(*pointer));
    pointer->center_x = (int32_t)(width / 2u);
    pointer->center_y = (int32_t)(height / 2u);
    pointer->margin_x = (int32_t)(width / 4u);
    pointer->margin_y = (int32_t)(height / 4u);
    pointer->last_x = pointer->center_x;
    pointer->last_y = pointer->center_y;
    pointer->warp_pending = 1u; /* the first thing done with a held pointer is to put it in the middle */
}

bool wire_pointer_take(wire_pointer_t *pointer, int32_t x, int32_t y, int32_t *out_dx, int32_t *out_dy) {
    if (pointer->warp_pending != 0u && x == pointer->center_x && y == pointer->center_y) {
        pointer->warp_pending = 0u;
        pointer->last_x = x;
        pointer->last_y = y;
        return false;
    }
    *out_dx = x - pointer->last_x;
    *out_dy = y - pointer->last_y;
    pointer->last_x = x;
    pointer->last_y = y;
    return *out_dx != 0 || *out_dy != 0;
}

bool wire_pointer_wants_warp(wire_pointer_t *pointer) {
    if (pointer->warp_pending != 0u) {
        return false;
    }
    const int32_t dx = abs(pointer->last_x - pointer->center_x);
    const int32_t dy = abs(pointer->last_y - pointer->center_y);
    if (dx <= pointer->margin_x && dy <= pointer->margin_y) {
        return false;
    }
    pointer->warp_pending = 1u;
    return true;
}
