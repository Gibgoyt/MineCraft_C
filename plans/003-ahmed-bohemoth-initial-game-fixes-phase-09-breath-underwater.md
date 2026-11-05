# 003-ahmed-bohemoth-initial-game-fixes — phase 9: Breath bar and seeing underwater

**Status:** in progress: stage 1 done apart from Ahmed's check, stage 2 not started   (updated 2026-11-05)

## Goal

Under water you can see that you're under water, and you can see how much air is left, before it starts to hurt.

**Ahmed's words:** "please add breath while I am under-water that way I actually know". He'd broken through ice into what looked like a void, then agreed it was probably water.

## Why it happens

- **Drowning already works, invisibly.** You get 300 ticks of air (15 seconds), then 2 damage every 20 ticks (`run/src/player/survival/index.c:462`, `PLAYER_SURVIVAL_AIR_MAX`). The HUD draws no air: `run/src/ui/hud/README.md` says "Air, armor points and a bar for either are not in this stage". So you could be drowning with no warning. **That may explain the low hearts in Ahmed's screenshot, but it isn't confirmed.**
- **Nothing changes the picture when your eyes are under water.** A search of `cuda/` and `run/src/render/frame/` finds no check for the camera being in water. Water has a surface tint for rays that come in from above, which suits looking down from the air. From inside, **this is the likely reason it looked like a void**. It's unconfirmed, because the screenshot run was stopped, and stage 2 starts by looking.

The textures are already there, unused: `textures/ui/bubble`, `bar_breath`, `icon_breath`, `icon_breath_empty`.

## Todo

**Stage 1: the bar.** This is what Ahmed asked for.

- [x] Send `air` from the sim to the HUD, as health and food are sent, in `run/src/game/`
- [x] Draw a row of 10 bubbles above the hunger row in `run/src/ui/hud/index.c`: one for each 30 ticks of air, shown only while under water or still refilling
- [x] Add tests in `tests/run/ui/hud/`: full, half, empty, hidden on land, and a golden picture
- [x] Add a test in `tests/run/player/survival/` that the bar empties just as the damage starts

**Stage 2: seeing.** Ahmed didn't ask for this directly, but it's what caused the confusion.

- [ ] Take a headless screenshot under water in today's game, and look, to confirm what's wrong
- [ ] Send whether the eyes are under water to the card, using `movement->eyes_under_water`
- [ ] When they are, tint the picture blue and add short fog, in `cuda/src/post/`, with a setting beside the other effects
- [ ] Add a golden picture from under the sea, and record the cost in ms

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_ui_hud|run_player_survival|run_render_frame|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: jump in the sea and stay under. Bubbles should run out over 15 seconds, then hearts should start to go. Coming up should refill them, and the water should clearly look like water.

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

- `3530bd5` Draw a row of ten bubbles over the drumsticks while the body is under water or refilling its air, so that drowning no longer comes without warning.
- `efc0e1b` Say in the repository structure that the HUD draws bubbles of air.

## Issues found

**Stage 1, the plan's "empties just as the damage starts" is a second out (2026-11-05).** Expected: the last bubble goes on the tick the hearts start to go. Actual: the last bubble goes on tick 300 under water and the first 2 damage comes on tick 320, because `survival_breathe` gasps when `air` reaches -20, and `tests/run/player/survival/` already held that timing before this work. Left the sim alone: the row reads empty for one second, which warns rather than misleads, and no heart goes while a bubble is left. `the_bar_counts_down_with_the_air_and_is_empty_when_the_hearts_start_to_go` holds both ticks. If Ahmed wants them on one tick, it's a change to the sim's drowning, not to the bar.

**Stage 1, no new word crosses the threads (2026-11-05).** Expected from the todo: something new to send. Actual: `player_vitals_t.air` was already published each tick and nothing read it. `air < 300` is exactly "under water or still refilling", as a tick under water always leaves it below 300 and a tick out of it reaches 300 only when full. So `game_hud_state_read` works it out, with no separate under-water flag. A dead body's air stops changing, so the row is also hidden while dead, which the plan didn't say. Wiring outside the named files: `run/include/ui/index.h`, where `ui_hud_t` grew 32 bytes out of `ui_t`'s padding, so `sizeof(ui_t)` stays 1152.

**Stage 1, `ui/bubble` and `ui/icon_breath` are the same picture (2026-11-05).** Comparing the first 9 by 9 pixels of both `index.mctex` files shows the same values. Used `ui/icon_breath` with `ui/icon_breath_empty`, as hearts and drumsticks pair theirs. `ui/bubble` and `ui/bar_breath` stay unused.

**Stage 1, not yet seen over real water (2026-11-05).** Expected: the bubbles read clearly under water. Actual: only checked on the test gradient, in `tests/run/ui/hud/fixtures/air_640x480.png`, which I looked at. The full bubble is a hollow blue ring and the empty one a dark ring at low alpha, so either could be faint on blue. Left for Ahmed's check below, and stage 2's screenshot can show it too.

**Stage 1, the new tests fail when the code is wrong (2026-11-05).** Rounding bubbles down and showing the row at full air, both by hand and then reverted, failed `run_player_survival` (294 of 15149 checks) and `run_ui_hud` (`a_bubble_is_30_ticks...` and the real game test). The existing goldens came out unchanged under `MC_GOLDEN_WRITE=1`, by `git status`.

**Needs no new blocks or items,** so it doesn't touch saved worlds, and can be built any time.
