# tests/run/game/command

**Purpose:** ctest `run_game_command`. Tests plan 006 phase 01's command box (`run/src/game/command/`, `run/src/ui/command/`),
the teleport in a real game on the card, `GAME_FILL_JUMP`, and the F3 tuning lines, against the numbers in
`plans/006-ahmed-bohemoth-biomes-follow-the-climate-phase-01-teleport-fly-and-tuning-readout.md`.
**Tier:** test.
**Owns:** nothing outside the test arena, and a directory of its own under `/tmp` for each test's saves, removed at its end.
Blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main and the real workers. The sessions run as `--menu --headless` does, ticks then draws, each draw settling.
**Public functions:** none; a test program.
**May be called by:** ctest. It times itself, so it runs alone (`test.cmake`).
**Depends on:** `tests/harness/`, `run/src/game/` and everything under it, `run/src/climate/`, a card.

Compute only: no display, console or input device is opened. Keys are typed as a headless script types them: one key a tick,
`~` with Shift held.

What it checks:

- **The parser, pure:** every command in master 2.6, with and without `/` and `~`, negatives and decimals: `/tp 10 20 30` is
  feet at 10.5, 20.5, 30, and `/tp ~2000 ~ ~` from 100.5, -40.5, 72 is 2100.5, -40.5, 72. `lat 0, 45, 60, -90` give y 0,
  19149, 25532, -38298 (to 1), and the preview reads "y 25532 · 60.0° N". Fifteen bad commands are refused, each with its
  reason; heights are held to the gameplay range and x and y to `PLAYER_COMMAND_PLACE_MOST`; 17 commands keep the last 16.
- **The box, through the menus:** `/tp 0 0` typed and Enter runs it and closes the box; `T` and Up bring it back; `/tq` stays,
  with a red line and the feet bit-equal; four more bad commands through `game_command_run` send nothing; Esc with the box up
  closes it and does not pause; Esc again pauses.
- **Three far jumps** (`/tp ~0 ~30000 ~`, then `/tp ~40000 ~ ~`, three times) in a world through the menus with its climate,
  at render distance 6: each `GAME_FILL_JUMP` has its nearest rings drawn in under 15 s in release (under 120 s at -O0, where
  only that it comes is checked), more columns come after every jump, each lands where it said from where the body stood,
  not waiting, and unharmed from the start of each arrival to its end (what the place does after is the place's: the third
  jump comes down in water on plan 007 phase 03's land, and drowns once before the next), and at three of the places the tuning words are `climate_normals_at` at the feet, rounded as printed.
  The lines are printed, as the "before" picture of biomes against the climate.
- **Saves and the clock:** a save taken mid-jump, with the body waiting for its ground 3 km away, reloads waiting there, then
  stands on the ground with room and its health; fly mode and `/speed 4` are kept by a save, and the HUD's line reads
  "Flying · 466 b/s · speed 4x"; `/time day 172` and `/time hour 6` move the clock forward to them; `/speed` alone says the factor.
