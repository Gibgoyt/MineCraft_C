# tests/cuda/bridge

**Purpose:** ctest `render_feature_bridge`. The scripted headless proof of placing ahead, for bridging (plan 005 phase 13): a body walks out over water placing 20 planks ahead of it, and the card draws the blue preview of each before it goes down. The rules themselves are tested in `tests/run/player/interaction/forward/`.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of the test, well under 1 GiB. With `MC_BRIDGE_SHOTS` set, PNG files in that directory.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/cuda/sky/fixtures/rig.h` for a real run with a world holding only what a test puts there, `run/src/player/`, `run/src/game/` and everything under it, and a card. No card fails this test, on purpose: a skip would read as a pass.

Compute only. It opens no display and no input device, and does not run the built program: the built program has no way to put 20 planks in a hand or a lake under a body, and this needs both.

The world is a lake 5 deep on a stone floor, with a stone platform one block wide out into it from the west, its top level with the water. A real `player_t` stands on the platform's east end with 24 oak planks and, tick by tick as the game runs it (`player_interact_tick`, then `player_tick`), walks east looking half a radian down with the right button held. The aim goes past the bridge's end into the water and finds nothing in reach, so each click places the cell ahead. It checks:

- all 20 go down at the platform's height in a line from x = 1 to 20 with no gap, the body never drops below the bridge's top, and each was shown `OK` the tick before, on its own cell;
- the preview, sent to the card as `game_preview_publish` sends it, turns more than 200 pixels blue at the end, each one the picture without it under the blue on one of the six faces.

To look at the pictures:

    mkdir -p /tmp/bridge_shots
    MC_BRIDGE_SHOTS=/tmp/bridge_shots tools/test-or-build.sh timeout 280 ctest --preset debug -R '^render_feature_bridge$'

`bridge_00.png` at the start, `bridge_05.png` and `bridge_10.png` along the way, and `bridge_20.png` at the end are what the player sees, with the cell ahead blue; each has a `_bare.png` beside it without the preview. `bridge_side.png` is the whole bridge from above and to the south, with the next cell blue past its end.
