# 006-ahmed-bohemoth-biomes-follow-the-climate — phase 1: Teleport, fly and the tuning readout

**Status:** done: built, tested, the debug suite and the release build pass (updated 2026-11-12)

Read first: `plans/006-ahmed-bohemoth-biomes-follow-the-climate-phase-00-vision-and-plan-files.md`, then the master `plans/006-ahmed-bohemoth-biomes-follow-the-climate.md` (sections 0, 2.6, 5 and 9). Every number here comes from the master. Where this file adds a number the master does not give, it says so and why.

## Goal

In an open survival world, Ahmed can press `T` or `/`, type `/tp X Y Z`, `/tp ~DX ~DY ~DZ`, `/tp X Y`, `/tp lat DEG [X]`, `/tp top`, `/fly`, `/time day N`, `/time hour H` or `/help`, and the game does it while it keeps running. A teleport never kills the player, never leaves him inside rock, and never leaves the world black: three far jumps (30 km north and 40 km east, three times) each redraw the world round the player within 15 s, measured by a new `GAME_FILL_JUMP` fill. Fly mode has no gravity, no fall damage and no hunger drain, with speed from 10 to 1000 blocks a second on the wheel, and it survives a save and reload. F3 shows three tuning lines (biome, family, class; yearly, warm and cold temperatures at the feet's height; rain, driest month and snowline). Until phase 03 those lines show today's biome next to the climate, which is the "before" picture. The biome rules do not change in this phase: every terrain digest holds.

## Where this fits

**The vision.** Plan 006 is the vegetation layer of the stack (phase 00): seed, terrain, climate, then biomes that follow the climate. Ahmed wants to *tune* that by eye: "a /tp command somehow so I can teleport to higher latitudes/longitudes and when i see a mountain to higher altitudes really really fast". So the tools come first, and every later phase's gate is walked with them.

**What later phases read from it:**

| Reader | What it uses |
|---|---|
| Phase 02 | The F3 lines, to show the climate sample's values match plan 004's worked examples at three places (its gate) |
| Phase 03 | `/tp lat 0, 15, 30, 45, 60, 75`, the F3 family line (its gate) |
| Phase 04 | `/tp` to an equatorial mountain, `/fly` up, `/tp lat 50`; `/time` to look at winter against summer (its gate) |
| Phase 05 | All of it, for Ahmed's final gate notes |

**What this phase must NOT build:**

- No change to biomes, surface, features, the climate sample or deterministic maths. Those are phases 02 to 04. Every `run_terrain_*` pin holds.
- No new blocks, no new biomes.
- No multiplayer-style permission system, no command for items, mobs or weather. Only the commands in master 2.6.
- No longitude: x has no climate meaning in an infinite east-west world (master 2.6), so nothing pretends x is longitude.
- No change to how the climate tiles or the map stream, *except* the fix the black-world investigation needs, which is recorded in Issues found with its evidence.

## Ahmed's words

- "we also need a /tp command somehow so I can teleport to higher latitudes/longitudes and when i see a mountain to higher altitudes really really fast so that I can fine tune exactly how terrain/biome generation ties in with our climate system"
- "EXTREMELY THOROUGHLY EXAMINE EVERYTHING NEEDED FOR THIS PLAN WHILE WE ARE STILL WRITING PLAN FILES! SO THAT WE CAN HAVE A SOLID IMPLEMENTATION!!"
- Decision, 2026-11-12: "`/tp` with absolute, `~` relative and `lat`; a `/fly` toggle; and an F3 tuning readout".

"Higher latitudes" is answered by `/tp lat DEG`. "Longitudes" is answered honestly: `lat` takes an optional x, and the box says x is not longitude. "Higher altitudes really really fast" is `/tp ~ ~ ~300`, `/tp top` and `/fly` at up to 1000 blocks a second.

---

## Background, explained

### A latch between two threads

Main reads the keyboard; the sim moves the body. They never share a variable without a stated order. A **latch** is a word main writes once and the sim takes once: main stores the target, then sets `teleport_word` with **release**; the sim loads `teleport_word` with **acquire**, and only if it is set reads the target and clears the word. Release then acquire means everything main wrote before the release is visible after the acquire. This is how `player_t` already hands looks and vitals between threads (`player_look_send`, `vitals_word`, `run/include/player/index.h:80-90`).

**Example.** Main writes `teleport_target = {0, 25532, 90}` then `teleport_word = 1` (release). The sim's next step loads 1 (acquire), reads the target, starts the move, and stores 0. If Ahmed presses Enter twice in one tick, the second overwrites the first before the sim sees either: the latest wins, which is what he meant.

### `player_t` and its size assert

`player_t` is asserted at exactly 2304 bytes (`static_assert(sizeof(player_t) == 2304)`, `run/include/player/index.h:94`), with one spare byte (`_pad0[1]` at 2099). A latch needs a word (4 bytes at least) and a target (three `float64_t`, 24 bytes, plus a kind). So the layout is rearranged, the size assert updated (to the next multiple of the 64-byte cache line, 2368, if it doesn't fit otherwise) and every `offsetof` assert after the change moved with it. Anything that hard-codes `player_t`'s size (the game's arena budget, `app_t`'s asserts) moves too.

### Relative coordinates

`~` means "where I am"; `~300` means "where I am plus 300". So `/tp ~ ~ ~300` keeps x and y and goes 300 blocks up. **Example:** feet at (100.5, -40.5, 72.0): `/tp ~2000 ~ ~` goes to (2100.5, -40.5, 72.0). Whole numbers given without `~` are block coordinates and land on the block's middle: `/tp 10 20 30` puts the feet at (10.5, 20.5, 30.0).

### Latitude to y

Plan 004 phase 02 puts the equator at y = 0 and the tropic (23.5°) 10 km north, so a pole is $Y_{\text{pole}} = 10000 \cdot 90 / 23.5 = 38\,298$ blocks away. Master 2.6: $y = \text{DEG}/90 \cdot Y_{\text{pole}}$. **Examples:** `lat 0` gives y 0; `lat 45` gives 19 149; `lat 60` gives 25 532 (the master's own example, "y 25532 · 60.0° N"); `lat -90` gives -38 298. The preview shows `climate_latitude_deg(y)` of the result, so the round trip is visible. (Latitude is a triangle wave that repeats every 153 191 blocks; `lat` only ever picks the first period, -90 to 90.)

### A fill, and why the jump gets its own kind

`run/src/game/fill/` times how fast the world comes in after a world opens (`GAME_FILL_OPEN`) or the render distance changes (`GAME_FILL_CHANGE`), ring by ring (`GAME_FILL_RINGS = 64`, `ring_near_ns` for rings 0 to `GAME_FILL_NEAR_RING = 4`), `run/include/game/fill/index.h:20-35`. A teleport is a third reason the world must come in from nothing, so it gets `GAME_FILL_JUMP = 2`. "Drawn within 15 s" in this file means `ring_near_ns` of that fill is under 15 000 000 000.

### Why a big jump may leave the world black

Plan 004 phase 02, Issues found 14 (2026-11-08): after a fast headless flight to x 44768, y 31150 the game had generated **53 448 columns after 2612 ticks and the same 53 448 after 16 032 ticks**: streaming stopped for good, with nothing in the log. Two suspects (master section 0):

1. **The early `return` in `game_columns_request`** (`run/src/game/index.c:1081-1086`). When the nearest column's climate tiles aren't ready, it cancels that request, counts a `climate_waits`, and returns: *no other column is asked for in that step*. If that one column's tile never becomes ready (for example because it is outside what `climate_schedule` is building, or its slot is stuck), every step returns at the same place forever.
2. **A climate tile slot stuck `CLAIMED` or `EVICTING`**, so `climate_area_ready` never turns true for it.

Either gives exactly what was seen: no new columns, no warning. A teleport makes exactly that jump, so this is fixed first.

---

## Design

### Directories and files

New parts, each with exactly one `index.*` and a `README.md` (purpose, tier, owns, lifetime, threads, public functions, callers, dependencies; `plans/REPO_STRUCTURE.md` section 3):

| Path | What | Tier |
|---|---|---|
| `run/src/ui/command/`, `run/include/ui/command/` | Draws the one-line box, the preview line and the red error line; feeds keys through `ui_widget_text_field` and `ui_widget_input_feed` (`run/include/ui/widget/index.h:102,133`), which already handle `- . / ~`, digits and Enter. Holds no game state | Borrower |
| `run/src/game/command/`, `run/include/game/command/` | The overlay in the Atlas pattern (`game_atlas_input`, `game_atlas_compose`, `run/include/game/atlas/index.h:23,27`), the 16-command history, the parser, the clamping, the latitude preview, and sending to the latch | Part of `game` |

Changes inside existing parts:

- `run/include/player/index.h`: `teleport_word`, `teleport_target` (and its kind: exact feet, stand on ground, top), the layout rearranged, the 2304 assert updated.
- `run/src/player/` (sim step): take the latch first; `player_movement_start` (`run/include/player/movement/index.h:70`), which sets the position, zeroes velocity and fall distance and stops drawing from smearing.
- `run/src/player/survival/`: the arrival wait. Today a body that has come back waits through `survival->placing` (byte 143) and `survival_place` / `survival_stand_at_spawn` (`run/src/player/survival/index.c:255-293`). The master names the flag `arriving`. **Default:** a new `arriving` byte next to `placing`, with a `survival_arrive` that reuses `survival_place`'s wait (read the cell under the target; return until it loads) and a `survival_stand_at` that takes a point instead of `survival->spawn`. `arriving` is saved with survival.
- `run/src/player/movement/`: `PLAYER_MOVEMENT_MODE_FLY` after `PLAYER_MOVEMENT_MODE_CLIMB = 4`; saved with movement.
- `run/src/game/fill/`: `GAME_FILL_JUMP = 2`; started by the sim when it takes a latch.
- `run/src/game/corner/` (or wherever the F3 lines are composed): the three tuning lines.
- `run/src/game/input/`: `game_screen_up` (the Esc guard) learns the command box, so Esc closes it before it opens the pause menu.

### The commands (master 2.6)

| Command | Target | Notes |
|---|---|---|
| `/tp X Y Z` | Feet at (X+0.5, Y+0.5, Z) | If inside rock: climb to the first space with headroom |
| `/tp ~DX ~DY ~DZ` | Relative; any mix | `/tp ~ ~ ~300` keeps x, y |
| `/tp X Y` | Stand on the top solid block with headroom | Waits for the column |
| `/tp lat DEG` | y from DEG; x kept; stand on ground | Preview "y 25532 · 60.0° N" |
| `/tp lat DEG X` | The same, x given | |
| `/tp top` | Straight up to the highest ground of this column | |
| `/fly` | Toggle fly | HUD "Flying · 120 b/s" |
| `/time day N`, `/time hour H` | Day of year 1 to 365, clock 0 to 24 | Through the calendar the census already uses |
| `/help` | Lists these in the box | |

**Clamping:** z to the world's height range (`MC_WORLD_Z_MIN` to the storage top); |x| and |y| to the save's limits. **Bad input** shows one red line and changes nothing.

### How a teleport runs

1. Enter: main parses, clamps, writes `teleport_target`, sets `teleport_word` (release).
2. The sim, at the start of its step, takes the latch (acquire), calls `player_movement_start(target)`, sets `arriving = 1` (no fall damage, held still), starts `GAME_FILL_JUMP`.
3. Each step while `arriving`: if the column under the target is not loaded, return. When loaded: `X Y Z` climbs out of rock; `X Y`, `lat` and `top` stand on the top solid block with headroom. Clear `arriving`.
4. Map, climate tiles and the far field recentre on their own: they follow the eye.

### Fly

`PLAYER_MOVEMENT_MODE_FLY`: no gravity, no fall damage, no hunger exhaustion. Space up, Shift down, wheel speed 10 to 1000 b/s as the test-world camera does, Ctrl doubles. An unloaded column stops the body at its edge as walking does. Leaving fly in the air falls normally from there, but the fall distance starts at 0 when fly ends, so `/fly` off over ground takes no damage from the flight itself.

### The F3 tuning lines (master 2.6)

```
biome savanna  family Savanna  class Savanna (climate)
yearly 24.6 °C  warm 27.1  cold 22.0 at z 88   today 25.3
rain 640 mm/yr  dry month 4 mm   snowline none
```

Worked out by the sim every 20 ticks, as the corner climate line is, from `climate_normals_at` (`run/include/climate/index.h:274`) at the feet. In this phase "family" is the family the climate class *would* give (master 2.4 table), and "biome" is today's noise biome: the mismatch is visible on screen. `(fallback)` at a test world, where phase 02's fallback lands; until then a test world shows `(no climate)`.

## Owned paths

This phase may edit these, plus this file:

- `run/{src,include}/ui/command/` and `run/{src,include}/game/command/` (new)
- `tests/run/game/command/` (new), `tests/run/player/` new tests only
- `run/{src,include}/game/fill/`: `GAME_FILL_JUMP` only

**Wiring outside them, each as small as possible:**

- `run/include/player/index.h`, `run/src/player/index.c`: the latch and its asserts
- `run/{src,include}/player/movement/`: fly mode
- `run/{src,include}/player/survival/`: `arriving`, `survival_arrive`, `survival_stand_at`
- `run/src/save/` (player and survival records): `arriving` and fly mode, with the format's own version rule
- `run/src/game/index.c`: the box in the input order; whatever the black-world fix needs in `game_columns_request` (`:1078-1090`)
- `run/src/climate/index.c`, `run/src/climate/cache/`: only if the investigation shows a stuck slot
- `run/src/game/input/index.c`: `game_screen_up`
- `run/src/game/corner/`: the F3 lines
- `plans/REPO_STRUCTURE.md`: rows for the new parts and tests

## Steps

1. **Reproduce the black world, headless, before writing anything else.** Release, `--test-world` and a menu world, flying to 30 km north and 40 km east as Issue 14 did; log `climate_waits`, columns generated, and each climate slot's state once a second. *Why: the fix must follow evidence, and the teleport would otherwise ship a known way to break the game.*
2. **Find the cause among the suspects and fix it.** If the early `return` is it, the likely fix is `continue` to the next offered column (the map offers nearest first, so order still holds), with a bounded count of skips a step. If it is a stuck slot, fix the cache's state machine. Record the evidence in Issues found. *Why: master 2.6, "investigated from the suspects above, and fixed and recorded wherever it is".*
3. **`GAME_FILL_JUMP`** in `run/src/game/fill/`, and the three-jump test (Tests). *Why: the 15 s number needs a clock that starts at the jump.*
4. **The latch on `player_t`**, the layout and asserts, and the sim handler with `player_movement_start`. *Why: a teleport is a move by the sim, never by main, or the body and the camera disagree for a frame.*
5. **`arriving` in survival**, its wait, standing on the ground, saved. *Why: no fall damage and no falling through an unloaded column.*
6. **The parser** in `run/src/game/command/`, as a pure function from text and the player's position to a target or an error, tested alone. *Why: most of the bugs will be here, and a pure function is cheap to test.*
7. **The overlay** (`run/src/ui/command/`, `run/src/game/command/`): `T` and `/`, history (16), Esc through `game_screen_up`, the preview, the red line. *Why: Atlas pattern, so the game keeps running.*
8. **Fly mode** and `/fly`, the wheel, the HUD line. *Why: "higher altitudes really really fast".*
9. **`/time day` and `/time hour`.** *Why: phase 04's gate compares winter snow and the summer snowline.*
10. **The F3 tuning lines.** *Why: the "before" readout, and every later gate reads it.*
11. **The headless key script test**, READMEs, `plans/REPO_STRUCTURE.md`. Tick the todo as each lands.

## Todo

From the master (section 9, phase 01):

- [x] Find and fix the black world after a big jump (plan 004 phase 02 Issues found 14), with the three-jump test
- [x] `GAME_FILL_JUMP` in `run/src/game/fill/`
- [x] The command box overlay (`run/src/ui/command/`, `run/src/game/command/`): T and `/`, history, Esc, a red error line
- [x] The parser: every command in 2.6, a latitude preview, and the clamping
- [x] The teleport latch on `player_t`, its sim handler, `arriving` in survival (saved), and standing on the ground
- [x] `PLAYER_MOVEMENT_MODE_FLY` and `/fly`, with speed on the wheel and the HUD line
- [x] `/time day` and `/time hour`
- [x] The F3 tuning lines. Until phase 03 they show climate values and today's biome, which is useful as the "before"
- [x] Tests in 5 (teleport, black world, fly), including a headless script that types `/tp` commands as keys
- [x] READMEs, and `plans/REPO_STRUCTURE.md` for the new directories

Ahmed's addition, 2026-11-12:

- [x] `/speed N`, N from 1 to 10, multiplies walking and flying speed by N; `/speed` alone says the factor; saved with the player;
  on the HUD's flying line ("Flying · 580 b/s · speed 5x"), combined with `/fly`'s wheel speed

Expanded:

- [x] Black world reproduced headless with a once-a-second log of `climate_waits`, columns generated and slot states; evidence in Issues found
- [x] Cause found (the early `return` at `run/src/game/index.c:1081-1086`, a stuck `CLAIMED`/`EVICTING` slot, or something else) and fixed
- [x] `player_t` latch added; the 2304-byte assert and every `offsetof` after it updated; build clean
- [x] `survival_arrive` / `survival_stand_at` reuse `survival_place`'s wait and `survival_stand_at_spawn`'s climb
- [x] `game_screen_up` closes the box on Esc before the pause menu
- [x] Every `run_terrain_*` pin unchanged
- [x] `standards_lint` and `clang_format` pass
- [x] The whole debug suite passes
- [x] `pgrep -af "$PWD/build"` prints nothing after the tests

## Tests

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `three_far_jumps_each_redraw_within_fifteen_seconds` (`tests/run/game/command/`, release, headless) | `/tp ~0 ~30000 ~`, then `/tp ~40000 ~ ~`, three times; `GAME_FILL_JUMP`'s `ring_near_ns` each time | Each under 15 s; columns generated rises after every jump | Streaming stops (Issue 14), or a jump's fill never finishes |
| `the_parser_reads_every_command` | Every row of the commands table, with and without `~`, negative numbers, decimals | `/tp 10 20 30` → (10.5, 20.5, 30.0); `/tp ~2000 ~ ~` from (100.5, -40.5, 72) → (2100.5, -40.5, 72) | A sign or `~` is misread |
| `latitude_gives_the_right_y` | `lat 0`, `45`, `60`, `-90` | y 0, 19 149, 25 532, -38 298 (±1); preview reads "60.0° N" | The pole constant or sign is wrong |
| `a_bad_command_changes_nothing` | `/tp`, `/tp a b`, `/tp lat 91`, `/tq`, `/time day 400` | Player bytes before = after; one red line | A partial parse moves the player |
| `coordinates_are_clamped` | `/tp 0 0 99999`, `/tp 0 0 -99999`, huge x | z within the world's range; x within the save's limit | The body leaves the world |
| `teleport_there_and_back_lands_safely` | Jump far and back on a survival world | Same health; no fall damage; column loaded; standing room (`physics_box_is_clear`) | Fall damage, stuck in rock, or falling through an unloaded column |
| `tp_up_keeps_x_and_y` | `/tp ~ ~ ~300` | x, y bit-equal before and after; z +300 | Relative maths uses the camera instead of the feet |
| `a_save_mid_jump_reloads_standing` | Autosave while `arriving`; reload | Reloads with `arriving` set, then stands; no damage | `arriving` not saved |
| `fly_has_no_gravity_and_no_fall_damage` | `/fly`, rise 200, hover 200 ticks, `/fly` off over ground at 1 block | z unchanged while hovering; health unchanged after landing; food unchanged | Gravity or hunger runs in fly |
| `fly_is_saved_and_reloaded` | Save in fly, reload | Mode is `PLAYER_MOVEMENT_MODE_FLY` | Mode not saved |
| `the_box_takes_keys_and_keeps_history` (headless key script) | Types `/tp 0 0`, Enter, `T`, Up | The box shows `/tp 0 0` again; 17 commands keep the last 16 | History off by one |
| `esc_closes_the_box_before_the_pause_menu` | `T`, Esc | Box closed, game not paused | `game_screen_up` not told |
| `the_tuning_lines_match_climate_normals_at` | F3 at three places | Each number equals `climate_normals_at` at the feet, rounded as printed | Wrong z or wrong day |

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_(game|player)_.*$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 900 ctest --preset release -R '^run_game_command$' --output-on-failure
    pgrep -af "$PWD/build"    # must print nothing

Never start the game and leave it running; Ahmed starts it himself.

## Gate

Ahmed, in his own game: types `/tp lat 60`, `/tp ~ ~ ~400`, `/fly`, and back to `/tp 0 0`, and reads F3 at each stop. The world must be drawn after each jump.

## Risks and open questions

| Risk or question | Default |
|---|---|
| The black world has a deeper cause than the two suspects | Phase 01 doesn't close until three far jumps redraw (master section 6) |
| Rearranging `player_t` moves offsets something else hard-codes | Every `offsetof` assert is updated in the same commit; the full debug suite runs |
| `survival_stand_at_spawn` climbs from a given z; `/tp X Y` needs the top solid block, a search from the top down | `survival_stand_at` takes a mode: climb from z, or top-down from the column's highest solid block |
| A teleport while dead, asleep, or with a screen open | Refused with a red line; only an alive, awake player with no other screen teleports |
| Q2: keys for the box | T and `/` (master Q2) |
| Q3: fly speed range | 10 to 1000 b/s on the wheel (master Q3) |
| Q4: `/tp` in test worlds (no climate)? | Yes, using the fallback climate once phase 02 lands (master Q4) |

## Commits

Appended as they land:

- ed6c5a5a Add the command box with /tp, /fly, /speed, /time and /help, a teleport mailbox on the player, fly mode, GAME_FILL_JUMP and the F3 tuning lines, and find the black world after a big jump to be the test world's eye inside rock.
- (this commit) Draw the F3 tuning panel only where there is a climate, let go of the widget's focus when the box closes, and record the verification.

## Issues found

**1. The black world after a big jump (plan 004 phase 02, Issues found 14) is the test world's flying eye inside rock, not
streaming stopping.**

- **Reproduced:** release, `--test-world --headless --no-sound --day-of-year 171 --time-of-day 0.0 --render-distance 8`,
  Ctrl at wheel 13 for 600 ticks, the eye at x 44918, y 31252, z 38: black, sky and all, `coarse 0.01 ms`, and 53627 columns
  generated, then no more (plan 004 saw 53448). A log every 20 ticks (once a second) of the map, the stream and the card:
  `ready 237 pending 0 active 237 leaving 0 cursor 197/197 quiet 1 jobs 0`, the render window centred on the eye's chunk,
  7761 sections on the card, the pool not full, `climate_waits` 0 (a test world has no climate). Everything the eye's place
  wants was loaded and on the card; nothing more was asked for because the eye stood still.
- **Bisected:** due east to x 40353 (y 440): black; due north to y 40913: drawn (a jungle at 83.85 N, the mismatch this plan
  is for). `--no-taa`, `--no-weather`, `--no-sky`, `--no-fog` and `--no-lod`: all black. One tick of Space, to z 580: the sky.
- **Cause:** a test world's eye keeps the height it started at (z 38 here) and passes through blocks. East of about 30 km the
  land rises over it, and the eye ends inside stone, where every ray starts in rock. No streaming fault, and neither suspect:
  with a climate, the three-jump test below never waits long at the column gate, and no slot is seen stuck.
- **Done:** nothing changed in streaming or the climate cache (master section 0's suspects were examined in the code and by the
  test, and both are left as they are). What a teleport must not do is what Issue 14 looked like: `/tp` never leaves a body in
  rock (`PLAYER_SURVIVAL_ARRIVE_EXACT` climbs to the first room), on the ground is from the world's top down, and fly mode
  collides. A test world's eye is as it was: it flies through rock by design.
- **The three-jump test** (`run_game_command`, release, render distance 6, a world through the menus with its climate, seed 7):
  each `GAME_FILL_JUMP`'s nearest rings at 1.98, 0.10, 0.09, 0.11, 0.16 and 0.09 s (all rings by 2.52 s at worst), more columns
  after every jump, the whole run 21 s; in the final release run 0.09, 0.08, 0.12, 0.16, 0.12 and 0.10 s, the run 10 s. The first jump builds the climate's mask pages and tiles round a new place; the rest
  land mostly on sea, which generates fast.

**2. `player_t` grew to 2368 bytes, and main's frame was made smaller to fit.** The latch did not fit in the 2304: main's line
(2048 to 2111) is full, and the only spare bytes were padding inside sub-structs the save writes whole. So a line of its own
at 2304 holds the target, the word, the fly wheel and the flight word (sim to main). `app_t` went from 16128 to 16192, and the
release link said main's frame was 16448 bytes against 16384; the options moved to a frame of their own
(`main_with_options`, `[[gnu::noinline]]`), as `main_with_app` did for plan 004.

**3. A mailbox of one, not "the latest wins".** Main writing a second target while the sim reads the first would be a data
race. So main writes only while the word is 0 and the sim reads the target before putting 0 back; a second command in the same
tick is refused with "Still on the way from the last one: try again". Typing takes far longer than a tick, so it is never seen
by hand.

**4. The save's version went from 6 to 7.** Fly mode (mode 5), the fly step and the factor are in `player_movement_t`'s two
padding bytes, and `arriving` and its kind in `player_survival_t`'s padding: an old file has zeros there and reads as it was.
An older build would call a flying body damage, so by the format's own rule the version goes up. `tests/run/save/state`'s
file-by-hand pins the version, and moved from 6 to 7 with it.

**5. Sizes that moved:** `game_fill_t` 1792 to 1856 (the jump's ask and its centre; `started_ns` is atomic now, as a jump's
fill starts with the sim running, which reads it); `game_corner_t` 192 to 256 (the tuning words); `game_world_parts_t` 61120 to
63168 (the command box, `game_command_t`, 1864 bytes). `run/src/game/index.c` is 1660 lines.

**6. Choices the master left open, with what was built:**

- The flying line is on the top right corner's line 0, which plan 004 left for a settlement's name. The speed it shows is what
  the body flies at, the factor in: "Flying · 466 b/s · speed 4x" is 116 x 4 rounded. Walking with a factor shows "Speed 4x".
- Fly speed is 10 x 1.25^(step - 1), steps 1 to 22 held to 10 and 1000 blocks a second, 116 by default; Ctrl doubles it.
- `/time day N` and `/time hour H` only go forward, to the next day N or the next H o'clock, so the weekly census runs for each
  week passed, as sleeping and `--climate-jump` do; going back would leave the census owing weeks it had already run.
- The history (16) lasts as long as the world is open: its memory is the World arena's.
- In a test world, which has no body, `/tp` moves the flying eye (on the ground means 3 blocks over `terrain_height_estimate`),
  and `/fly` and `/speed` say the eye always flies. F3's tuning panel is drawn only where there is a climate: `run_ui_hud` holds that F3 changes nothing outside its own panel in a
  world with none, and the panel broke two of its checks (1885 pixels each) in the first full run. The words still say
  `(no climate)`. Phase 02, whose fallback gives a test world numbers to show, must widen that test's region to the tuning panel
  (`ui_command_lines_draw`'s rect under `ui_hud_debug_panel`) when it draws it there.
- `/speed`'s factor is the player's, not the body's: `player_movement_place` keeps it, the fly speed and fly mode through a
  teleport; a respawn keeps the factor and lands on its feet, not flying. A new world starts at 1.

**7. The "before" picture, as the tuning lines printed it on seed 7:** at 80008, 60008 a `frozen_river` biome in the Steppe
class at a yearly 18.7 °C; at 40008, 30008 a `frozen_ocean` where the yearly mean is -14.3 °C (agreeing); in the test world a
jungle at 83.85 N. Phase 03's gate reads these lines.

**9. Flaky under load:** in the first full debug run (164 tests, 2543 s) `run_physics_raycast`'s
`on_real_threads_the_outline_is_in_the_picture_on_the_camera_it_was_drawn_from` failed `edges_found * 2u > edges_asked`; alone
it passes (2.3 s). It times real threads against each other and nothing here touches the outline; recorded, not changed.

**8. How the jumps are timed.** The test runs the session as `--menu --headless` does, ticks then draws, and each draw settles,
so a jump's fill is timed in wall time from the tick the sim takes the teleport to the nearest rings on the card. At -O0 the
bound is 120 s, where only that each jump's world comes is checked; the 15 s is checked in release, as the Verification runs it.
