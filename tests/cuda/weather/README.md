# tests/cuda/weather

**Purpose:** ctest `render_feature_weather`. Tests plan 004 phase 08's weather in the renderer: the drops, the bolt and the weather fog in `run/include/gpu_bridge/post/index.h` and `cuda/src/post/`, the sky, light and fog in `run/src/render/frame/`, the slots from `run/src/game/weather/`, and `--weather` and `--no-weather` in `run/src/main/`.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/png`, `tests/cuda/sky/fixtures/rig.h` for a real run with a world holding only what a test puts there, `run/src/game/`, and everything under it, and a card. No card fails this test, on purpose.

Compute only. It opens no display and no input device, and runs the built program only with `--headless --no-sound`.

What it checks:

- **By hand, on the host, the same lines as the card:** a clear sky leaves 10,000 random colours and render_day_fill's sky and light to the bit; a drop found at its cell's middle covers `POST_RAIN_ALPHA` and 0.1 blocks across nothing; with wind 6 the streak through a place a block up is 0.67 blocks upwind; snow is round flakes at `POST_SNOW_ALPHA`; no ray up from under a roof meets a drop, and in the open many do; nothing is drawn behind a wall, in a gale too; a bolt 10 blocks east is covered and a block off is not, and its flicker and flash by distance (1, 0.5, 0.1); visibility 124, 117 and 496 at render distance 32, never under 32, and the fog colour.
- **Golden pictures** of each weather (clear, rain, snow, thunder with a strike, fog, dust, hurricane), from inside a stone house looking out of its doorway at noon, in `fixtures/`, held byte for byte, each looked at before it was kept. `MC_GOLDEN_WRITE=1` writes them again. The same slot twice is the same picture, every weather differs from the clear one, and with the setting off any weather is `render_frame_draw`'s own picture.
- **The roof in the real renderer:** looking up from inside, the top third with rain is the same as with no drops; outside, hundreds of pixels have drops.
- **A flash:** the ground of a thunderstorm brightens by more than 20% on the tick of a strike 300 blocks away.
- **The built program:** `--test-world --weather rain` is not the clear picture and the same twice; an unknown kind, and `--weather` or `--no-weather` with no world, are refused.
