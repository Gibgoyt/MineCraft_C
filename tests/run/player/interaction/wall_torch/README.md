# tests/run/player/interaction/wall_torch

**Purpose:** ctest `run_player_interaction_wall_torch`. Tests a torch on a wall: `run/src/player/interaction/` and `run/src/world/wall_torch/`, and what the real renderer makes of it.
**Tier:** test.
**Owns:** the test arena, and what its world, game and card blocks take, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main, and the real workers for the picture.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/player/`, `run/src/game/` and everything under it, and a card for the picture.

Compute only. No display, console or input device is opened, and the program itself is not run.

What it checks:

- **Each face:** the four sides each give their own block, a top gives the floor torch, and an underside gives nothing. The hand goes on holding `torch`, and only a torch goes by the face.
- **What is behind it:** the side of what is not solid gives a floor torch if there is a floor and nothing if not. Tall grass is overwritten by whichever it comes to. Water and a block in the cell stop it.
- **What breaks it:** the block behind going drops every torch it held and no other, and a torch mined by itself leaves the wall. A blast is in `tests/run/mob/creeper/`, an axe in `tests/run/player/felling/`, and sand falling and leaves going in `tests/run/world/tick/`.
- **The real aim:** `player_camera_target` from an eye on each side finds the stone, then the torch there, then over its top the stone again.
- **A real picture:** at midnight, a stone in the air with all four placed through the real hands and drawn by the real scene, from four corners. Flame-coloured pixels must show. `tests/cuda/shapes/` holds what is drawn to an exact reference.

To look at the pictures, name a directory:

    mkdir -p /tmp/wall_torches
    MC_WALL_TORCH_SHOTS=/tmp/wall_torches tools/test-or-build.sh timeout 280 ctest --preset debug -R '^run_player_interaction_wall_torch$' --output-on-failure
