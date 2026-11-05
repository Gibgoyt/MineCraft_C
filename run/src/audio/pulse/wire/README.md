# run/src/audio/pulse/wire

**Purpose:** the PulseAudio native protocol as bytes: where the socket and the cookie are, each command we send, and each reply and notice we read.
**Tier:** Borrow. Allocates nothing, and touches no socket or file: callers give it bytes and buffers.
**Owns:** nothing.
**Lifetime:** One call.
**Threads:** any; it has no state but what a caller passes in.
**Public functions:**

- `pulse_wire_socket_path`, `pulse_wire_cookie_path`: which server, and the cookie it may want. A `PULSE_SERVER` naming another machine is refused, as there is no TCP here.
- `pulse_wire_descriptor`, `pulse_wire_descriptor_parse`: the 20 bytes before every packet.
- `pulse_wire_auth`, `pulse_wire_client_name`, `pulse_wire_create_stream`, `pulse_wire_delete_stream`: one each, writing into the caller's buffer.
- `pulse_wire_head_parse`, `pulse_wire_u32_parse`, `pulse_wire_stream_parse`, `pulse_wire_request_parse`: what the server says. Each refuses anything malformed or cut short.

**May be called by:** `audio/pulse`, and `tests/run/audio/pulse/`, whose stand-in for a server speaks the same bytes.
**Depends on:** nothing of ours.

There is no official protocol document. The layout was read from PulseAudio's source as reference, and proved against the real `pipewire-pulse` 1.6.9 with silence: see "Stage 1 spike" in `plans/003-ahmed-bohemoth-initial-game-fixes-phase-05-sound.md`.
