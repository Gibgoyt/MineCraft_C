# tests/run/player/command

**Purpose:** ctest `run_player_command`. Tests plan 006 phase 01's mailbox on `player_t` (`player_command_send`,
`player_command_take`), a teleported body's arriving in `run/src/player/survival/`, and fly mode and the speed factor in
`run/src/player/movement/`, on a body in a small world of stone, ticked as `game_sim_tick` ticks it.
**Tier:** test.
**Owns:** the test arena, and what its world takes, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/player/`, `run/src/world/`, `run/src/physics/`.

No card, no display and no input device.

What it checks, against the numbers in `plans/006-ahmed-bohemoth-biomes-follow-the-climate-phase-01-teleport-fly-and-tuning-readout.md`:

- **`/tp ~ ~ ~300`:** x and y bit-equal before and after, z 300 up; then 300 blocks down with no harm, standing, not arriving;
  and a real fall afterwards still hurts. A second command waits while the first is in the mailbox.
- **There and back:** far past what is loaded, the body waits where it was put, held, unharmed and with its keys counting for
  nothing; when the ground comes it stands on top of it with room; back home exact, bit-equal, with the same health.
- **Rock and ground:** feet inside stone climb to the first room above; on the ground is from the world's top down.
- **The edges:** heights held to the gameplay range, x and y to `PLAYER_COMMAND_PLACE_MOST`, a far step the same, and a dead
  body is not moved.
- **Fly:** no gravity (200 ticks hovering, not a hair lower), 116 blocks a second by default, 10 to 1000 on the wheel, a teleport
  keeps it flying, off a block over the stone takes no harm, and no food or saturation is spent.
- **Speed:** `/speed 5` walks at 5 x 4.317 blocks a second and flies at 5 x 116; 11 is refused; a teleport keeps it.
