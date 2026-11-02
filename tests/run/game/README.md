# tests/run/game

**Purpose:** ctest `run_game`. Tests `run/src/game/`'s real loop as `main_run_test_world` runs it, on the real card and threads, with fake kernels for the display and input: `game_loop_source_begin`, `game_loop_source`, `game_loop_discard`, and `display_frames_run` around them. What a frame draws is `tests/run/render/frame/`.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main, the real workers, the real `sim` and `gpu` threads, and the real guardian process.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/display/mode/fixtures/`, `tests/run/input/fixtures/`, `run/src/game/`, and everything under it, and a card.

Compute only. No real display, console or input device is opened: both kernels are fakes. The guardian's fork is real.

What it checks:

- **The source:** a first frame starts the threads, the mouse and keys fly the camera by the clock, Esc ends the run, and what waited on another VT, and the time spent there, count for nothing.
- **The display loop, with the guardian forked:** the pictures are pinned before the fork, and the threads start after it. A picture made after the fork matches one made before it, byte for byte, which is what says a pin survives a fork on this kernel and driver. A flight then streams and draws with the guardian alive, and ends on what one thread draws from there.
- **A measurement, on request:** `MC_FRAME_MEASURE_THREADS=1` on the release preset runs the real threads at the default render distance from nothing, and prints how long it took. Otherwise that test does nothing.
