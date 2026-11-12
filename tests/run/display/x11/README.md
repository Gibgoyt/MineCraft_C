# tests/run/display/x11

**Purpose:** holds `display/x11/wire`, the X11 protocol as bytes, to what the protocol says, including `wire_focus_is_real`: only a
focus event whose mode is not Grab or Ungrab counts in `display_x11_t.focus_losses`, which is what pauses the game (plan 005 phase 7).
`ctest run_display_x11`. What the session does with that count is `tests/run/game/focus/`.
**Tier:** test. Needs no server, no socket and no window, so it can never put anything on a desktop.

The connection itself is checked against a private `Xvfb` by hand: see `plans/002-ahmed-window-on-the-desktop.md`.

**Plan 008 phase 02, two segments:** the real `display_x11_free_buffer` and `display_x11_present` on a `display_x11_t` laid out as `display_x11_init` leaves it but with no connection (fd -1, which a look at the socket passes over). Puts are `display_x11_put_note`, what `display_x11_present` does before its send, and the server's answers `display_x11_completion_take`, what reading an `ShmCompletion` does, sent when the test chooses. Sockets are allowed only in `run/src/display/x11/`, so the bytes on the wire are the wire tests' part.

- **Two puts outstanding do not block:** two puts go, one from each segment; with no completion sent, the third call gives the spare at once (under 5 ms), `puts_skipped` is 1, and neither the spare nor a segment still out is ever put.
- **A completion frees its own segment:** completions in the other order free the one they name, and one naming no segment of ours frees nothing.
- **No completion for five seconds still fails:** both out, the older sent 4.9 s ago is still a skip; at 5.1 s it is a failure naming five seconds.
- `wire_shm_completion_segment` reads bytes 12 to 15.
