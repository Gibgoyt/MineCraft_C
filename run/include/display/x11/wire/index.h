/* See run/src/display/x11/wire/README.md. */
#ifndef MC_RUN_DISPLAY_X11_WIRE_INDEX_H
#define MC_RUN_DISPLAY_X11_WIRE_INDEX_H

#include <stdint.h>

enum : uint32_t {
    WIRE_COOKIE_BYTES = 16u, /* MIT-MAGIC-COOKIE-1 */
    WIRE_PACKET_BYTES = 32u, /* every event, error and short reply */
    WIRE_SETUP_REQUEST_BYTES = 12u + 20u + WIRE_COOKIE_BYTES,
    WIRE_REQUEST_MAX_BYTES = 256u, /* the longest any encoder below writes */
    WIRE_KEYCODE_OFFSET = 8u,      /* an X keycode is the evdev code plus this, on the evdev and libinput servers */
    WIRE_FOCUS_MODE_GRAB = 1u,     /* in byte 8 of a focus event: caused by a grab, not by the window manager */
    WIRE_FOCUS_MODE_UNGRAB = 2u,
};

/* The first byte of a packet from the server. An event sent by another client has its top bit set. */
typedef enum wire_packet_t : uint8_t {
    WIRE_PACKET_ERROR = 0,
    WIRE_PACKET_REPLY = 1,
    WIRE_PACKET_KEY_PRESS = 2,
    WIRE_PACKET_KEY_RELEASE = 3,
    WIRE_PACKET_BUTTON_PRESS = 4,
    WIRE_PACKET_BUTTON_RELEASE = 5,
    WIRE_PACKET_MOTION = 6,
    WIRE_PACKET_FOCUS_IN = 9,
    WIRE_PACKET_FOCUS_OUT = 10,
    WIRE_PACKET_CLIENT_MESSAGE = 33,
} wire_packet_t;

/* What the server's setup reply says, as far as drawing into a window goes. */
typedef struct wire_setup_t {
    uint32_t id_base;
    uint32_t id_mask;
    uint32_t root;
    uint32_t visual;
    uint16_t screen_width;
    uint16_t screen_height;
    uint8_t depth;
    uint8_t usable; /* depth 24 in 32-bit little-endian pixels, with red, green, blue where XRGB8888 has them */
    uint8_t _pad0[2];
} wire_setup_t;
static_assert(sizeof(wire_setup_t) == 24);

/* What a mouse button means. */
typedef enum wire_button_kind_t : uint8_t {
    WIRE_BUTTON_NONE = 0,
    WIRE_BUTTON_KEY = 1,    /* code is a BTN_* */
    WIRE_BUTTON_WHEEL = 2,  /* value is detents, away from the user positive */
    WIRE_BUTTON_HWHEEL = 3, /* value is detents, right positive */
} wire_button_kind_t;

typedef struct wire_button_t {
    uint32_t code;
    int32_t value;
    wire_button_kind_t kind;
    uint8_t _pad0[3];
} wire_button_t;
static_assert(sizeof(wire_button_t) == 12);

/* Turns pointer positions into movement while the pointer is held in the window by warping it back to the middle. */
typedef struct wire_pointer_t {
    int32_t last_x;
    int32_t last_y;
    int32_t center_x;
    int32_t center_y;
    int32_t margin_x; /* further than this from the middle, it is warped back */
    int32_t margin_y;
    uint8_t warp_pending; /* asked for, and its own motion event has not come yet */
    uint8_t _pad0[3];
} wire_pointer_t;
static_assert(sizeof(wire_pointer_t) == 28);

/* The display number in a DISPLAY value: ":0", ":0.0" or "unix:0". A remote one is refused, since there is no TCP here
 * and shared memory could not reach it anyway. */
[[nodiscard]] bool wire_display_number(const char *display, uint32_t *out_number);

/* Finds the MIT-MAGIC-COOKIE-1 for this display number in the bytes of an Xauthority file. A record with no number
 * serves any display. Stops at anything malformed. */
[[nodiscard]] bool wire_xauth_find(const uint8_t *bytes, uint64_t length, uint32_t display_number,
                                   uint8_t out_cookie[WIRE_COOKIE_BYTES]);

/* The first thing sent. cookie may be nullptr, for a server with no authentication. Returns the bytes written. */
uint32_t wire_setup_request(uint8_t *out, const uint8_t *cookie);

/* How many bytes the whole setup reply takes, given its first 8. */
[[nodiscard]] uint64_t wire_setup_reply_bytes(const uint8_t header[8]);

/* Reads a whole successful setup reply. False if it is too short to be one. */
[[nodiscard]] bool wire_setup_parse(const uint8_t *reply, uint64_t length, wire_setup_t *out);

/* How long a packet is, given its first 32 bytes: 32, or more for a reply or generic event with data after it. */
[[nodiscard]] uint64_t wire_packet_bytes(const uint8_t packet[WIRE_PACKET_BYTES]);

/* The requests. Each writes into out, which holds WIRE_REQUEST_MAX_BYTES, and returns the bytes written. */
uint32_t wire_query_extension(uint8_t *out, const char *name);
uint32_t wire_intern_atom(uint8_t *out, const char *name);
uint32_t wire_get_input_focus(uint8_t *out);
uint32_t wire_create_window(uint8_t *out, uint32_t window, uint32_t parent, uint32_t visual, uint8_t depth,
                            uint16_t width, uint16_t height);
uint32_t wire_map_window(uint8_t *out, uint32_t window);
uint32_t wire_create_gc(uint8_t *out, uint32_t gc, uint32_t drawable, uint32_t foreground);
uint32_t wire_change_property(uint8_t *out, uint32_t window, uint32_t property, uint32_t type, uint8_t format,
                              const void *data, uint32_t units);
uint32_t wire_create_pixmap(uint8_t *out, uint32_t pixmap, uint32_t drawable, uint8_t depth, uint16_t width,
                            uint16_t height);
uint32_t wire_fill_rectangle(uint8_t *out, uint32_t drawable, uint32_t gc, uint16_t width, uint16_t height);
uint32_t wire_create_cursor(uint8_t *out, uint32_t cursor, uint32_t source, uint32_t mask);
uint32_t wire_grab_pointer(uint8_t *out, uint32_t window, uint32_t cursor);
uint32_t wire_ungrab_pointer(uint8_t *out);
uint32_t wire_warp_pointer(uint8_t *out, uint32_t window, int16_t x, int16_t y);
uint32_t wire_shm_attach(uint8_t *out, uint8_t major, uint32_t segment, uint32_t shm_id);
uint32_t wire_shm_detach(uint8_t *out, uint8_t major, uint32_t segment);
uint32_t wire_shm_put_image(uint8_t *out, uint8_t major, uint32_t drawable, uint32_t gc, uint16_t width,
                            uint16_t height, uint8_t depth, uint32_t segment);

/* Size hints that hold a window at one size, as WM_NORMAL_HINTS holds them: 18 values of 32 bits. */
void wire_size_hints_fixed(uint32_t out[18], uint32_t width, uint32_t height);

/* The evdev code of a keycode. False for one below the offset, which no key has. */
[[nodiscard]] bool wire_key_code(uint8_t keycode, uint32_t *out_code);

/* What a button number means. Wheel "buttons" only count when pressed. */
wire_button_t wire_button(uint8_t detail);

/* A held key makes X send a release and a press with one time stamp, where evdev would send a repeat. */
[[nodiscard]] bool wire_is_auto_repeat(const uint8_t release[WIRE_PACKET_BYTES], const uint8_t next[WIRE_PACKET_BYTES]);

/* Whether a FocusIn or FocusOut is the window manager moving the focus, and not someone's grab or its end: the mode, byte 8, is
 * not Grab (1) or Ungrab (2). Byte 1 is the detail, which says nothing of that. */
[[nodiscard]] bool wire_focus_is_real(const uint8_t packet[WIRE_PACKET_BYTES]);

void wire_pointer_start(wire_pointer_t *pointer, uint32_t width, uint32_t height);

/* Takes one motion event's position in the window. Returns whether it is movement, in out_dx and out_dy: the event of a
 * warp is not. */
bool wire_pointer_take(wire_pointer_t *pointer, int32_t x, int32_t y, int32_t *out_dx, int32_t *out_dy);

/* Whether to warp back to the middle now. Asking says yes at most once per warp, and expects the warp to be sent. */
[[nodiscard]] bool wire_pointer_wants_warp(wire_pointer_t *pointer);

#endif
