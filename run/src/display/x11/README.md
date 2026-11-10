# run/src/display/x11

**Purpose:** a window on the local X server, so the game runs on a normal desktop such as XFCE. No libX11 or xcb: our own protocol bytes over the server's Unix socket, with frames in MIT-SHM shared memory.
**Tier:** Arena. Takes what it needs from `display`'s arena.
**Owns:** the shared memory segment, the one connection, and the window on the server, which goes when the connection closes.
**Lifetime:** Process.
**Threads:** main.
**Public functions:**

- `display_x11_init`, `display_x11_deinit`: a pair. `_init` logs why if it fails, and leaves nothing behind.
- `display_x11_arena_bytes`: what `_init` takes from the arena.
- `display_x11_service`: reads what has come without waiting.
- `display_x11_poll`: hands input to a `display_x11_sink_t`, which `main` joins to `input`.
- `display_x11_wait_ready`, `display_x11_present`: frame pacing against the server's completion event.

**May be called by:** `display`.
**Depends on:** `arena`, `log`, `time`, `display/flip` (`display_buffer_t`), `display/x11/wire`.
**Parts:** `wire`, the protocol as bytes.

This is the one place sockets are allowed: a Unix socket to a server on this machine. A `DISPLAY` naming another machine is refused, so there is still no TCP anywhere. Plain `shmget` memory is allowed here for the same reason the dumb buffer is in `display/device`: it is shared with something outside the process, not heap.

## Using the pointer

While the window has focus the pointer is held in it and hidden, and is warped back to the middle before it can reach an edge. The game reads the difference, so it gets movement the same way it does from evdev.

Key codes are X keycodes minus 8, which is the evdev code. Alt+Tab takes the focus away, which lets the pointer go and lets go of every key; clicking the window takes it back. The click itself does not reach the game.

Each time the window manager takes the focus away, `focus_losses` goes up by one. The game's session reads it each frame and opens the pause screen (plan 005 phase 7). A focus event caused by a grab, such as the keyboard grab XFCE takes while Alt+Tab is held, does not count: its mode, in byte 8 of the event, says Grab or Ungrab (`wire_focus_is_real`). Until plan 005 phase 7 this read byte 1, the detail, as the mode.

## Limits

- Movement goes through the desktop's pointer acceleration, which KMS with evdev does not. Turn it down in XFCE's mouse settings if aiming feels uneven.
- No vsync: the frame rate is paced by a timer, and the desktop's compositor decides about tearing.
- One fixed window size, chosen at start.
