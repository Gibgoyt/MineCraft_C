# tests/run/display/x11

**Purpose:** holds `display/x11/wire`, the X11 protocol as bytes, to what the protocol says, including `wire_focus_is_real`: only a
focus event whose mode is not Grab or Ungrab counts in `display_x11_t.focus_losses`, which is what pauses the game (plan 005 phase 7).
`ctest run_display_x11`. What the session does with that count is `tests/run/game/focus/`.
**Tier:** test. Needs no server, no socket and no window, so it can never put anything on a desktop.

The connection itself is checked against a private `Xvfb` by hand: see `plans/002-ahmed-window-on-the-desktop.md`.
