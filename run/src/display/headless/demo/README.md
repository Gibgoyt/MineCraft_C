# run/src/display/headless/demo

**Purpose:** the scene `--headless` shows until `game` exists: the test pattern, a cursor the script moves, a lamp per key, and the tick number in lamps. So a screenshot shows, with no game behind it, that input reached the frame and which tick it was.
**Tier:** Borrow. Allocates nothing.
**Owns:** nothing.
**Lifetime:** One run.
**Threads:** the one running the backend.
**Public functions:**

- `display_headless_demo_scene`: a `display_headless_scene_t` drawing this.
- `display_headless_demo_key_lamp`, `display_headless_demo_tick_lamp`: where a lamp is, for a test to look there.

**May be called by:** `main`, tests.
**Depends on:** `display/headless`, `display/headless/script`, `display/flip` (only the pattern's pixels).

## What a frame shows

- **Key lamps:** 8 rows of 32, from the top left, at `DISPLAY_DEMO_MARGIN`. A lamp is yellow while the key whose evdev code is `row * 32 + column` is down.
- **Tick lamps:** one row below those, cyan for each set bit of the tick number, lowest bit on the left.
- **Cursor:** a square, starting in the middle of the frame and moved by `move`. White when idle; red, green and blue add up for left, right and middle held. Its side is 32 pixels plus 4 per net `wheel` step, between 8 and 128.
