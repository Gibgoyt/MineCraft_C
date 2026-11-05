# run/src/display/x11/wire

**Purpose:** the X11 protocol as bytes: `DISPLAY` and Xauthority parsing, the setup reply, each request we send, and the meaning of each event we read.
**Tier:** Borrow. Allocates nothing, and touches no socket or file: callers give it bytes and buffers.
**Owns:** nothing.
**Lifetime:** One call.
**Threads:** any; it has no state but what a caller passes in.
**Public functions:**

- `wire_display_number`, `wire_xauth_find`: which server, and the cookie it wants.
- `wire_setup_request`, `wire_setup_reply_bytes`, `wire_setup_parse`, `wire_packet_bytes`: the handshake and framing.
- `wire_query_extension` and the other request encoders: one each, writing into the caller's buffer.
- `wire_key_code`, `wire_button`, `wire_is_auto_repeat`: what an event means in evdev's terms, which is what the game reads.
- `wire_pointer_start`, `wire_pointer_take`, `wire_pointer_wants_warp`: mouse movement from a pointer held in the window.

**May be called by:** `display/x11`, and `audio` for `wire_display_number` alone, to tell a test's private display from a person's.
**Depends on:** nothing of ours.
