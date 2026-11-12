/* ctest run_display_x11. See README.md. The protocol as bytes, and plan 008 phase 02's two segments with the server's answers played by
 * the test: no X server, no socket and no window. */
#include "harness/index.h"

#include "display/x11/index.h"
#include "display/x11/wire/index.h"
#include "time/index.h"

#include <linux/input-event-codes.h>
#include <string.h>

static uint32_t read16(const uint8_t *in) {
    return (uint32_t)in[0] | (uint32_t)in[1] << 8;
}

static uint32_t read32(const uint8_t *in) {
    return read16(in) | read16(in + 2) << 16;
}

static void write16(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void write32(uint8_t *out, uint32_t value) {
    write16(out, value);
    write16(out + 2, value >> 16);
}

TEST(a_local_display_gives_its_number) {
    uint32_t number = 99u;
    CHECK(wire_display_number(":0", &number));
    CHECK_EQ_I64(number, 0);
    CHECK(wire_display_number(":0.0", &number));
    CHECK_EQ_I64(number, 0);
    CHECK(wire_display_number(":12.1", &number));
    CHECK_EQ_I64(number, 12);
    CHECK(wire_display_number("unix:3", &number));
    CHECK_EQ_I64(number, 3);
}

TEST(a_display_on_another_machine_or_with_no_number_is_refused) {
    uint32_t number = 0;
    CHECK(!wire_display_number("otherhost:0", &number));
    CHECK(!wire_display_number("10.0.0.1:0", &number));
    CHECK(!wire_display_number("localhost:10.0", &number));
    CHECK(!wire_display_number(":", &number));
    CHECK(!wire_display_number(":x", &number));
    CHECK(!wire_display_number(":0x", &number));
    CHECK(!wire_display_number("", &number));
    CHECK(!wire_display_number(nullptr, &number));
}

/* One Xauthority record, whose lengths are big-endian. Returns the bytes written. */
static uint64_t auth_record(uint8_t *out, const char *number, const char *name, uint8_t fill, uint32_t cookie_bytes) {
    uint64_t at = 0;
    const char *const address = "admin";
    const uint32_t fields[3] = {(uint32_t)strlen(address), (uint32_t)strlen(number), (uint32_t)strlen(name)};
    const char *const texts[3] = {address, number, name};
    out[at++] = 1u;
    out[at++] = 0u;
    for (uint32_t i = 0; i < 3u; i++) {
        out[at++] = (uint8_t)(fields[i] >> 8);
        out[at++] = (uint8_t)fields[i];
        memcpy(out + at, texts[i], fields[i]);
        at += fields[i];
    }
    out[at++] = (uint8_t)(cookie_bytes >> 8);
    out[at++] = (uint8_t)cookie_bytes;
    memset(out + at, fill, cookie_bytes);
    return at + cookie_bytes;
}

TEST(the_cookie_for_this_display_is_found_past_others) {
    uint8_t file[512];
    uint64_t length = auth_record(file, "1", "MIT-MAGIC-COOKIE-1", 0x11u, 16u);
    length += auth_record(file + length, "0", "XDM-AUTHORIZATION-1", 0x22u, 16u);
    length += auth_record(file + length, "0", "MIT-MAGIC-COOKIE-1", 0x33u, 16u);
    uint8_t cookie[WIRE_COOKIE_BYTES];
    REQUIRE(wire_xauth_find(file, length, 0u, cookie));
    CHECK_EQ_I64(cookie[0], 0x33);
    CHECK_EQ_I64(cookie[15], 0x33);
    REQUIRE(wire_xauth_find(file, length, 1u, cookie));
    CHECK_EQ_I64(cookie[0], 0x11);
    CHECK(!wire_xauth_find(file, length, 2u, cookie));
}

TEST(a_record_with_no_number_serves_any_display) {
    uint8_t file[256];
    const uint64_t length = auth_record(file, "", "MIT-MAGIC-COOKIE-1", 0x44u, 16u);
    uint8_t cookie[WIRE_COOKIE_BYTES];
    REQUIRE(wire_xauth_find(file, length, 7u, cookie));
    CHECK_EQ_I64(cookie[0], 0x44);
}

TEST(a_cut_off_or_odd_auth_file_finds_nothing_and_reads_nothing_past_its_end) {
    uint8_t file[256];
    const uint64_t length = auth_record(file, "0", "MIT-MAGIC-COOKIE-1", 0x55u, 16u);
    uint8_t cookie[WIRE_COOKIE_BYTES];
    for (uint64_t cut = 0; cut < length; cut++) {
        CHECK(!wire_xauth_find(file, cut, 0u, cookie));
    }
    const uint64_t short_cookie = auth_record(file, "0", "MIT-MAGIC-COOKIE-1", 0x55u, 15u);
    CHECK(!wire_xauth_find(file, short_cookie, 0u, cookie));
}

TEST(the_setup_request_is_little_endian_with_the_cookie_padded_in_place) {
    uint8_t cookie[WIRE_COOKIE_BYTES];
    memset(cookie, 0xABu, sizeof(cookie));
    uint8_t out[WIRE_SETUP_REQUEST_BYTES];
    const uint32_t bytes = wire_setup_request(out, cookie);
    CHECK_EQ_I64(bytes, 12 + 20 + 16); /* an 18-byte name pads to 20 */
    CHECK_EQ_I64(out[0], 'l');
    CHECK_EQ_I64(read16(out + 2), 11);
    CHECK_EQ_I64(read16(out + 6), 18);
    CHECK_EQ_I64(read16(out + 8), 16);
    CHECK(memcmp(out + 12, "MIT-MAGIC-COOKIE-1", 18u) == 0);
    CHECK_EQ_I64(out[32], 0xAB);
    CHECK_EQ_I64(wire_setup_request(out, nullptr), 12);
    CHECK_EQ_I64(read16(out + 6), 0);
}

/* A setup reply with one screen, one format and one visual. */
static uint64_t setup_reply(uint8_t *out, uint8_t depth, uint8_t bits, uint32_t red) {
    memset(out, 0, 256u);
    out[0] = 1u;
    write32(out + 12, 0x04000000u);
    write32(out + 16, 0x001FFFFFu);
    write16(out + 24, 5u); /* vendor pads to 8 */
    out[28] = 1u;
    out[29] = 1u;
    uint8_t *const format = out + 40 + 8;
    format[0] = depth;
    format[1] = bits;
    uint8_t *const screen = format + 8;
    write32(screen, 0x123u);
    write16(screen + 20, 1920u);
    write16(screen + 22, 1080u);
    write32(screen + 32, 0x21u);
    screen[38] = depth;
    screen[39] = 1u;
    uint8_t *const allowed = screen + 40;
    allowed[0] = depth;
    write16(allowed + 2, 1u);
    uint8_t *const visual = allowed + 8;
    write32(visual, 0x21u);
    visual[4] = 4u;
    write32(visual + 8, red);
    write32(visual + 12, 0x00FF00u);
    write32(visual + 16, 0x0000FFu);
    const uint64_t total = (uint64_t)(visual + 24 - out);
    write16(out + 6, (uint32_t)((total - 8u) / 4u));
    return total;
}

TEST(a_setup_reply_gives_the_screen_and_says_if_its_pixels_are_ours) {
    uint8_t reply[256];
    const uint64_t length = setup_reply(reply, 24u, 32u, 0xFF0000u);
    CHECK_EQ_I64((int64_t)wire_setup_reply_bytes(reply), (int64_t)length);
    wire_setup_t setup;
    REQUIRE(wire_setup_parse(reply, length, &setup));
    CHECK_EQ_I64(setup.id_base, 0x04000000);
    CHECK_EQ_I64(setup.root, 0x123);
    CHECK_EQ_I64(setup.visual, 0x21);
    CHECK_EQ_I64(setup.screen_width, 1920);
    CHECK_EQ_I64(setup.screen_height, 1080);
    CHECK_EQ_I64(setup.depth, 24);
    CHECK_EQ_I64(setup.usable, 1);
}

TEST(a_screen_with_other_pixels_is_not_usable_and_a_short_reply_is_refused) {
    uint8_t reply[256];
    wire_setup_t setup;
    uint64_t length = setup_reply(reply, 24u, 24u, 0xFF0000u);
    REQUIRE(wire_setup_parse(reply, length, &setup));
    CHECK_EQ_I64(setup.usable, 0); /* three bytes a pixel */
    length = setup_reply(reply, 24u, 32u, 0x0000FFu);
    REQUIRE(wire_setup_parse(reply, length, &setup));
    CHECK_EQ_I64(setup.usable, 0); /* red and blue the other way round */
    length = setup_reply(reply, 16u, 32u, 0xFF0000u);
    REQUIRE(wire_setup_parse(reply, length, &setup));
    CHECK_EQ_I64(setup.usable, 0);
    length = setup_reply(reply, 24u, 32u, 0xFF0000u);
    for (uint64_t cut = 0; cut < 40u; cut++) {
        CHECK(!wire_setup_parse(reply, cut, &setup));
    }
    for (uint64_t cut = 40u; cut < length; cut++) {
        if (wire_setup_parse(reply, cut, &setup)) {
            CHECK_EQ_I64(setup.usable, 0); /* cut inside the visuals: never read past, and never said to be ours */
        }
    }
}

TEST(packets_are_32_bytes_unless_a_reply_says_more) {
    uint8_t packet[WIRE_PACKET_BYTES];
    memset(packet, 0, sizeof(packet));
    packet[0] = WIRE_PACKET_KEY_PRESS;
    CHECK_EQ_I64((int64_t)wire_packet_bytes(packet), 32);
    packet[0] = WIRE_PACKET_REPLY;
    write32(packet + 4, 3u);
    CHECK_EQ_I64((int64_t)wire_packet_bytes(packet), 44);
    packet[0] = 35u;
    CHECK_EQ_I64((int64_t)wire_packet_bytes(packet), 44);
    packet[0] = WIRE_PACKET_ERROR;
    CHECK_EQ_I64((int64_t)wire_packet_bytes(packet), 32);
}

/* Each request's length field must agree with the bytes returned, and be whole 4-byte units, or the server loses its place. */
static void check_request(test_context_t *t, const uint8_t *out, uint32_t bytes, uint8_t opcode) {
    CHECK_EQ_I64(out[0], opcode);
    CHECK_EQ_I64(bytes % 4u, 0);
    CHECK_EQ_I64(read16(out + 2) * 4u, bytes);
    CHECK(bytes <= WIRE_REQUEST_MAX_BYTES);
}

TEST(every_request_is_as_long_as_it_says) {
    uint8_t out[WIRE_REQUEST_MAX_BYTES];
    uint32_t hints[18];
    wire_size_hints_fixed(hints, 640u, 480u);
    check_request(t, out, wire_query_extension(out, "MIT-SHM"), 98u);
    check_request(t, out, wire_intern_atom(out, "_NET_WM_STATE_FULLSCREEN"), 16u);
    check_request(t, out, wire_get_input_focus(out), 43u);
    check_request(t, out, wire_create_window(out, 1u, 2u, 3u, 24u, 640u, 480u), 1u);
    check_request(t, out, wire_map_window(out, 1u), 8u);
    check_request(t, out, wire_create_gc(out, 1u, 2u, 0u), 55u);
    check_request(t, out, wire_change_property(out, 1u, 39u, 31u, 8u, "MineCraft_C", 11u), 18u);
    check_request(t, out, wire_change_property(out, 1u, 40u, 41u, 32u, hints, 18u), 18u);
    check_request(t, out, wire_create_pixmap(out, 1u, 2u, 1u, 1u, 1u), 53u);
    check_request(t, out, wire_fill_rectangle(out, 1u, 2u, 1u, 1u), 70u);
    check_request(t, out, wire_create_cursor(out, 1u, 2u, 2u), 93u);
    check_request(t, out, wire_grab_pointer(out, 1u, 2u), 26u);
    check_request(t, out, wire_ungrab_pointer(out), 27u);
    check_request(t, out, wire_warp_pointer(out, 1u, 320, 240), 41u);
    check_request(t, out, wire_shm_attach(out, 130u, 1u, 2u), 130u);
    check_request(t, out, wire_shm_detach(out, 130u, 1u), 130u);
    check_request(t, out, wire_shm_put_image(out, 130u, 1u, 2u, 640u, 480u, 24u, 3u), 130u);
}

TEST(a_put_asks_for_a_whole_frame_and_for_word_when_it_is_done) {
    uint8_t out[WIRE_REQUEST_MAX_BYTES];
    CHECK_EQ_I64(wire_shm_put_image(out, 130u, 0x11u, 0x22u, 640u, 480u, 24u, 0x33u), 40);
    CHECK_EQ_I64(out[1], 3);
    CHECK_EQ_I64(read32(out + 4), 0x11);
    CHECK_EQ_I64(read32(out + 8), 0x22);
    CHECK_EQ_I64(read16(out + 12), 640); /* whole image */
    CHECK_EQ_I64(read16(out + 14), 480);
    CHECK_EQ_I64(read16(out + 20), 640); /* all of it shown */
    CHECK_EQ_I64(read16(out + 22), 480);
    CHECK_EQ_I64(out[28], 24);
    CHECK_EQ_I64(out[29], 2); /* ZPixmap */
    CHECK_EQ_I64(out[30], 1); /* the event that says the buffer may be drawn on again */
    CHECK_EQ_I64(read32(out + 32), 0x33);
}

TEST(a_completion_names_its_segment_in_bytes_12_to_15) {
    uint8_t packet[WIRE_PACKET_BYTES] = {0};
    packet[0] = 65u;
    write32(packet + 4, 0x11u); /* the drawable */
    write32(packet + 12, 0x2Au);
    CHECK_EQ_I64(wire_shm_completion_segment(packet), 0x2A);
}

/* ---- Two segments, plan 008 phase 02 ---------------------------------------------------------------------------- */

enum : uint32_t {
    X11T_SIDE = 4u,
};

/* A display_x11_t as display_x11_init leaves it, but with no connection (fd -1, which a look at the socket passes over): every buffer
 * test memory, the segments 0x103 and 0x107. A put is display_x11_put_note, as display_x11_present does before its send, and the server's
 * answer display_x11_completion_take, as reading its ShmCompletion does. Nothing to deinit. */
static void x11t_open(test_context_t *t, display_x11_t *x11) {
    memset(x11, 0, sizeof(*x11));
    x11->fd = -1;
    x11->borrowed_inbox = test_alloc(t, 1u << 18);
    x11->borrowed_events = test_alloc(t, 4096u * WIRE_PACKET_BYTES);
    for (uint32_t i = 0u; i < DISPLAY_X11_BUFFERS; i++) {
        const uint64_t bytes = (uint64_t)X11T_SIDE * X11T_SIDE * 4u;
        x11->buffers[i] = (display_buffer_t){.owned_map = test_alloc(t, bytes),
                                             .map_bytes = bytes,
                                             .pitch = X11T_SIDE * 4u,
                                             .width = X11T_SIDE,
                                             .height = X11T_SIDE};
    }
    x11->segment[0] = 0x103u;
    x11->segment[1] = 0x107u;
    x11->shm_completion = 65u;
    x11->running = 1u;
}

/* A frame: the free buffer, and a put from it unless it is the spare. */
static bool x11t_frame(display_x11_t *x11, uint32_t *out_which) {
    if (!display_x11_free_buffer(x11, out_which)) {
        return false;
    }
    if (*out_which != DISPLAY_X11_SPARE) {
        display_x11_put_note(x11, *out_which, time_now_ns());
    }
    return true;
}

TEST(two_puts_outstanding_do_not_block) {
    display_x11_t x11;
    x11t_open(t, &x11);
    uint32_t which = 99u;
    REQUIRE(x11t_frame(&x11, &which));
    CHECK(which < DISPLAY_X11_SEGMENTS);
    const uint32_t first = which;
    REQUIRE(x11t_frame(&x11, &which));
    CHECK(which < DISPLAY_X11_SEGMENTS && which != first);
    CHECK_EQ_U64(x11.frames, 2u);
    /* Both with the server, with no completion: the third finds none free, at once, and is the spare. */
    const int64_t started_ns = time_now_ns();
    REQUIRE(x11t_frame(&x11, &which));
    const int64_t took_ns = time_now_ns() - started_ns;
    CHECK_EQ_U64(which, DISPLAY_X11_SPARE);
    CHECK_EQ_U64(x11.puts_skipped, 1u);
    CHECK(took_ns < 5000000);
    CHECK(x11.failed_call == nullptr);
    CHECK_EQ_U64(x11.frames, 2u);
    /* Neither the spare nor a segment still with the server is ever put: present refuses before it sends. */
    CHECK(!display_x11_present(&x11, DISPLAY_X11_SPARE));
    CHECK(!display_x11_present(&x11, first));
    CHECK_EQ_U64(x11.frames, 2u);
}

TEST(a_completion_frees_its_own_segment) {
    display_x11_t x11;
    x11t_open(t, &x11);
    uint32_t which = 99u;
    REQUIRE(x11t_frame(&x11, &which));
    const uint32_t first = which;
    REQUIRE(x11t_frame(&x11, &which));
    const uint32_t second = which;
    /* The second's completion comes first: the second is free, and the first still out. */
    display_x11_completion_take(&x11, x11.segment[second], time_now_ns());
    CHECK_EQ_U64(x11.put_pending[first], 1u);
    CHECK_EQ_U64(x11.put_pending[second], 0u);
    CHECK(x11.put_done_ns != 0); /* it was the last put */
    REQUIRE(x11t_frame(&x11, &which));
    CHECK_EQ_U64(which, second);
    /* Now the first's: it is free, and the second out again; the first's done is not the last put's. */
    display_x11_completion_take(&x11, x11.segment[first], time_now_ns());
    CHECK_EQ_I64(x11.put_done_ns, 0);
    REQUIRE(x11t_frame(&x11, &which));
    CHECK_EQ_U64(which, first);
    CHECK_EQ_U64(x11.put_pending[second], 1u);
    /* A completion naming no segment of ours frees nothing. */
    display_x11_completion_take(&x11, 0x999u, time_now_ns());
    REQUIRE(x11t_frame(&x11, &which));
    CHECK_EQ_U64(which, DISPLAY_X11_SPARE);
    CHECK_EQ_U64(x11.puts_skipped, 1u);
    /* Both back: the one not put last goes first. */
    display_x11_completion_take(&x11, x11.segment[0], time_now_ns());
    display_x11_completion_take(&x11, x11.segment[1], time_now_ns());
    REQUIRE(x11t_frame(&x11, &which));
    CHECK_EQ_U64(which, second);
}

TEST(no_completion_for_five_seconds_still_fails) {
    display_x11_t x11;
    x11t_open(t, &x11);
    uint32_t which = 99u;
    REQUIRE(x11t_frame(&x11, &which));
    REQUIRE(x11t_frame(&x11, &which));
    /* Both out; the older one was sent 4.9 s ago: still a skip. */
    const int64_t now_ns = time_now_ns();
    x11.segment_sent_ns[x11.last_put ^ 1u] = now_ns - 4900000000ll;
    REQUIRE(x11t_frame(&x11, &which));
    CHECK_EQ_U64(which, DISPLAY_X11_SPARE);
    CHECK(x11.failed_call == nullptr);
    /* 5.1 s: the X server is stuck, and that is a failure, as it was with one segment. */
    x11.segment_sent_ns[x11.last_put ^ 1u] = now_ns - 5100000000ll;
    CHECK(!display_x11_free_buffer(&x11, &which));
    REQUIRE(x11.failed_call != nullptr);
    CHECK_CONTAINS(x11.failed_call, "five seconds");
}

TEST(an_x_keycode_is_the_evdev_code_plus_eight) {
    uint32_t code = 0;
    REQUIRE(wire_key_code(9u, &code));
    CHECK_EQ_I64(code, KEY_ESC);
    REQUIRE(wire_key_code(25u, &code));
    CHECK_EQ_I64(code, KEY_W);
    REQUIRE(wire_key_code(65u, &code));
    CHECK_EQ_I64(code, KEY_SPACE);
    CHECK(!wire_key_code(7u, &code));
}

TEST(buttons_mean_what_the_game_reads) {
    CHECK_EQ_I64(wire_button(1u).code, BTN_LEFT);
    CHECK_EQ_I64(wire_button(1u).kind, WIRE_BUTTON_KEY);
    CHECK_EQ_I64(wire_button(2u).code, BTN_MIDDLE);
    CHECK_EQ_I64(wire_button(3u).code, BTN_RIGHT);
    CHECK_EQ_I64(wire_button(4u).kind, WIRE_BUTTON_WHEEL);
    CHECK_EQ_I64(wire_button(4u).value, 1);
    CHECK_EQ_I64(wire_button(5u).value, -1);
    CHECK_EQ_I64(wire_button(7u).kind, WIRE_BUTTON_HWHEEL);
    CHECK_EQ_I64(wire_button(200u).kind, WIRE_BUTTON_NONE);
}

TEST(a_release_and_press_at_one_time_is_a_repeat_and_nothing_else_is) {
    uint8_t release[WIRE_PACKET_BYTES];
    uint8_t press[WIRE_PACKET_BYTES];
    memset(release, 0, sizeof(release));
    memset(press, 0, sizeof(press));
    release[0] = WIRE_PACKET_KEY_RELEASE;
    release[1] = 25u;
    write32(release + 4, 1000u);
    press[0] = WIRE_PACKET_KEY_PRESS;
    press[1] = 25u;
    write32(press + 4, 1000u);
    CHECK(wire_is_auto_repeat(release, press));
    write32(press + 4, 1001u);
    CHECK(!wire_is_auto_repeat(release, press)); /* let go and pressed again */
    write32(press + 4, 1000u);
    press[1] = 26u;
    CHECK(!wire_is_auto_repeat(release, press)); /* another key */
    press[1] = 25u;
    press[0] = WIRE_PACKET_MOTION;
    CHECK(!wire_is_auto_repeat(release, press));
}

TEST(the_pointer_gives_movement_and_its_own_warp_gives_none) {
    wire_pointer_t pointer;
    wire_pointer_start(&pointer, 640u, 480u);
    CHECK(!wire_pointer_wants_warp(&pointer)); /* the first warp is already asked for */
    int32_t dx = 0;
    int32_t dy = 0;
    /* Moves that were on their way before the first warp is seen count from where it was. */
    CHECK(wire_pointer_take(&pointer, 330, 240, &dx, &dy));
    CHECK_EQ_I64(dx, 10);
    CHECK_EQ_I64(dy, 0);
    CHECK(!wire_pointer_take(&pointer, 320, 240, &dx, &dy)); /* the warp itself */
    CHECK(wire_pointer_take(&pointer, 325, 237, &dx, &dy));
    CHECK_EQ_I64(dx, 5);
    CHECK_EQ_I64(dy, -3);
    CHECK(!wire_pointer_wants_warp(&pointer));
    CHECK(wire_pointer_take(&pointer, 500, 237, &dx, &dy));
    CHECK_EQ_I64(dx, 175);
    CHECK(wire_pointer_wants_warp(&pointer));
    CHECK(!wire_pointer_wants_warp(&pointer)); /* once per warp */
    CHECK(wire_pointer_take(&pointer, 510, 237, &dx, &dy));
    CHECK_EQ_I64(dx, 10);
    CHECK(!wire_pointer_take(&pointer, 320, 240, &dx, &dy));
    CHECK(wire_pointer_take(&pointer, 321, 240, &dx, &dy));
    CHECK_EQ_I64(dx, 1);
}

/* Plan 005 phase 7: only the window manager moving the focus counts, as that is what pauses the game. */
TEST(a_focus_event_is_real_unless_its_mode_says_grab_or_ungrab_whatever_its_detail) {
    uint8_t packet[WIRE_PACKET_BYTES];
    memset(packet, 0, sizeof(packet));
    packet[0] = WIRE_PACKET_FOCUS_OUT;
    write32(packet + 4, 77u);
    for (uint32_t detail = 0u; detail < 8u; detail++) {
        packet[1] = (uint8_t)detail; /* Ancestor, Virtual, Inferior, Nonlinear, ...: never a mode */
        packet[8] = 0u;              /* Normal: Alt+Tab, or a click on another window */
        CHECK(wire_focus_is_real(packet));
        packet[8] = 1u; /* Grab: XFCE's keyboard grab while Alt+Tab is held */
        CHECK(!wire_focus_is_real(packet));
        packet[8] = 2u; /* Ungrab */
        CHECK(!wire_focus_is_real(packet));
        packet[8] = 3u; /* WhileGrabbed: the focus really moved, with a grab held */
        CHECK(wire_focus_is_real(packet));
    }
}

TEST_MAIN()
