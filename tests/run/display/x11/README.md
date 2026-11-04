# tests/run/display/x11

**Purpose:** holds `display/x11/wire`, the X11 protocol as bytes, to what the protocol says. `ctest run_display_x11`.
**Tier:** test. Needs no server, no socket and no window, so it can never put anything on a desktop.

The connection itself is checked against a private `Xvfb` by hand: see `plans/002-ahmed-window-on-the-desktop.md`.
