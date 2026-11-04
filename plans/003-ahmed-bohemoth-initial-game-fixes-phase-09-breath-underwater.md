# 003-ahmed-bohemoth-initial-game-fixes — phase 9: Breath bar and seeing underwater

**Status:** not started   (updated 2026-11-04)

## Goal

Under water you can see that you're under water, and you can see how much air is left, before it starts to hurt.

**Ahmed's words:** "please add breath while I am under-water that way I actually know". He'd broken through ice into what looked like a void, then agreed it was probably water.

## Why it happens

- **Drowning already works, invisibly.** You get 300 ticks of air (15 seconds), then 2 damage every 20 ticks (`run/src/player/survival/index.c:462`, `PLAYER_SURVIVAL_AIR_MAX`). The HUD draws no air: `run/src/ui/hud/README.md` says "Air, armor points and a bar for either are not in this stage". So you could be drowning with no warning. **That may explain the low hearts in Ahmed's screenshot, but it isn't confirmed.**
- **Nothing changes the picture when your eyes are under water.** A search of `cuda/` and `run/src/render/frame/` finds no check for the camera being in water. Water has a surface tint for rays that come in from above, which suits looking down from the air. From inside, **this is the likely reason it looked like a void**. It's unconfirmed, because the screenshot run was stopped, and stage 2 starts by looking.

The textures are already there, unused: `textures/ui/bubble`, `bar_breath`, `icon_breath`, `icon_breath_empty`.

## Todo

**Stage 1: the bar.** This is what Ahmed asked for.

- [ ] Send `air` from the sim to the HUD, as health and food are sent, in `run/src/game/`
- [ ] Draw a row of 10 bubbles above the hunger row in `run/src/ui/hud/index.c`: one for each 30 ticks of air, shown only while under water or still refilling
- [ ] Add tests in `tests/run/ui/hud/`: full, half, empty, hidden on land, and a golden picture
- [ ] Add a test in `tests/run/player/survival/` that the bar empties just as the damage starts

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

## Issues found

**Needs no new blocks or items,** so it doesn't touch saved worlds, and can be built any time.
