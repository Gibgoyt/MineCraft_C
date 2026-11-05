# 003-ahmed-bohemoth-initial-game-fixes — phase 9: Breath bar and seeing underwater

**Status:** in progress: stages 1 and 2 done and gate checked, with kelp and seagrass fixed, and only Ahmed's check is left: see Verification   (updated 2026-11-05)

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

- [x] Take a headless screenshot under water in today's game, and look, to confirm what's wrong
- [x] Send whether the eyes are under water to the card, using `movement->eyes_under_water`
- [x] When they are, tint the picture blue and add short fog, in `cuda/src/post/`, with a setting beside the other effects
- [x] Add a golden picture from under the sea, and record the cost in ms

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_ui_hud|run_player_survival|run_render_frame|render_feature_under_water|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: jump in the sea and stay under. Bubbles should run out over 15 seconds, then hearts should start to go. Coming up should refill them, and the water should clearly look like water.

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

- `3530bd5` Draw a row of ten bubbles over the drumsticks while the body is under water or refilling its air, so that drowning no longer comes without warning.
- `efc0e1b` Say in the repository structure that the HUD draws bubbles of air.
- `9f4dbe4` Tint the picture blue and add short fog when a body's eyes are under water, with a setting and a flag, so that the sea no longer looks like a black void.
- `9df6873` Say in the repository structure where the test of what eyes under water see lives.
- `937feed` Record stage 2 of the breath phase in its plan file: the void was black water, it is fixed and tested, with its commits and what was found.
- `b5f01ee` Count kelp and seagrass in the sea as water, so that an eye in a stalk neither breathes nor sees black water, and draw no dark box around a stalk seen from under water.

## Issues found

**Stage 1, the plan's "empties just as the damage starts" is a second out (2026-11-05).** Expected: the last bubble goes on the tick the hearts start to go. Actual: the last bubble goes on tick 300 under water and the first 2 damage comes on tick 320, because `survival_breathe` gasps when `air` reaches -20, and `tests/run/player/survival/` already held that timing before this work. Left the sim alone: the row reads empty for one second, which warns rather than misleads, and no heart goes while a bubble is left. `the_bar_counts_down_with_the_air_and_is_empty_when_the_hearts_start_to_go` holds both ticks. If Ahmed wants them on one tick, it's a change to the sim's drowning, not to the bar.

**Stage 1, no new word crosses the threads (2026-11-05).** Expected from the todo: something new to send. Actual: `player_vitals_t.air` was already published each tick and nothing read it. `air < 300` is exactly "under water or still refilling", as a tick under water always leaves it below 300 and a tick out of it reaches 300 only when full. So `game_hud_state_read` works it out, with no separate under-water flag. A dead body's air stops changing, so the row is also hidden while dead, which the plan didn't say. Wiring outside the named files: `run/include/ui/index.h`, where `ui_hud_t` grew 32 bytes out of `ui_t`'s padding, so `sizeof(ui_t)` stays 1152.

**Stage 1, `ui/bubble` and `ui/icon_breath` are the same picture (2026-11-05).** Comparing the first 9 by 9 pixels of both `index.mctex` files shows the same values. Used `ui/icon_breath` with `ui/icon_breath_empty`, as hearts and drumsticks pair theirs. `ui/bubble` and `ui/bar_breath` stay unused.

**Stage 1, not yet seen over real water (2026-11-05).** Expected: the bubbles read clearly under water. Actual: only checked on the test gradient, in `tests/run/ui/hud/fixtures/air_640x480.png`, which I looked at. The full bubble is a hollow blue ring and the empty one a dark ring at low alpha, so either could be faint on blue. Left for Ahmed's check below, and stage 2's screenshot can show it too.

**Stage 1, the new tests fail when the code is wrong (2026-11-05).** Rounding bubbles down and showing the row at full air, both by hand and then reverted, failed `run_player_survival` (294 of 15149 checks) and `run_ui_hud` (`a_bubble_is_30_ticks...` and the real game test). The existing goldens came out unchanged under `MC_GOLDEN_WRITE=1`, by `git status`.

**Stage 2, the void is confirmed, and it is black water (2026-11-05).** Expected from the plan: water from inside looked wrong, cause unconfirmed. Actual: flying down into the sea of the default seed at noon, x 77, y 575, was dark teal at z = -3, nearly black at -13 and all black at -23. Stored light runs out with depth. Under the sea with a real body, in `sea.png`'s place with the setting off, 34491 of 57600 pixels are under 12 on every channel, and none are with it on. Ahmed had also broken through ice, which stops the sky's light sooner. So a tint alone, as the plan had it, would multiply black: the fog is what fixes this, and its colour comes from the sky's light and not the world's, with 12 % of it even at the eye.

**Stage 2, an eye in the water had no distance (2026-11-05).** Expected: `post` could fog by `shade_pixel_t.t`. Actual: `shade_kernel` starts on the water around the eye at a `t` of about 0, so every pixel said it was at the eye. Far water came out `174c72` and not the water's colour `24658e`. Fixed in `cuda/src/shade/index.cu`: with `post.under_water`, that pixel's `t` is where the water stops, or a box in it. This is wiring outside `cuda/src/post/`. With the flag off the kernel's result is unchanged, and `render_feature_water`'s goldens and exact trace still pass. Taking the fix out by hand failed 4 checks of the new test.

**Stage 2, wiring the todo did not name (2026-11-05).** All the 16 bits of `game_config_t.features_off` were taken, and it was `uint16_t` in `main_options_t` too, so both are `uint32_t` now, with `god_rays` and `menu` moved so that neither struct changes size. `post_settings_t` grew from 80 to 112 bytes, out of `render_frame_t._pad1`, so `render_t` stays the same size. `player_vitals_t` has no byte left, so the flag crosses in its own relaxed word, `player_t.eyes_under_water`, out of `_pad0`, and is drawn by `render_frame_under_water_set` in `game_frame_draw`. The main help text's first string passed C's 4095-byte limit, so `--time-of-day` moved to the next one.

**Stage 2, the Settings page changed shape (2026-11-05).** Expected: one more box among the effects. Actual: it was two columns of eight with Back filling row 8. "Under water" now has the left of that row and Back the right, so no box moved. A person who clicks Back in the very middle of the screen now hits the gap: `tests/run/settings/index.c` clicks further right. I looked at `effects.png` at 640 by 480 and it fits. Not checked on the smaller sizes by eye, only by the existing test of every size.

**Stage 2, what it does not do (2026-11-05).** All left as they are, as the plan asks for no more:

- A flying eye, `--test-world` with no `--walk`, has no body and so no flag: it still sees the black water. Nobody plays that way.
- The colour goes by the time of day and not by depth or what is overhead, so a pool in a sealed cave is as blue as a sea at noon, and a deep sea no darker than a shallow one.
- The flag is as of the last tick and the eye is drawn between ticks, so crossing the surface can show the wrong picture for up to a tick. Seen only in arithmetic, not looked at in motion.
- The bed right beside the eye is still dark at depth, at 12 % water, as `sea.png` shows at its left. It reads as a dark hill, not a void. Ahmed's call if it should be lighter: it's `POST_WATER_FOG_NEAR`.

**Stage 2, how far it was seen (2026-11-05).** `tests/cuda/under_water/fixtures/dive.script` is a real body in the built program: walks and swims from the default seed's start to the sea, then down to z = -19, at noon. I looked at `land.png`, `sea.png` and `sea_no_air.png`, and two runs came out exact, 57600 of 57600 pixels. This also answers stage 1's open point: the bubbles read over real water, full and empty, in `sea.png`. It was not seen at night or in motion, which is for Ahmed. Taking the flag out of `game_frame_draw` by hand failed 3 checks.

**Stage 2, the cost is too small to measure (2026-11-05).** `render_feature_under_water`, debug build, 1920 by 1080, on the card's clock, 30 frames, under a hand-made sea with no entities: setting off, post 0.246 ms, shade 12.884 ms, frame 15.184 ms; on, post 0.251 ms, shade 12.829 ms, frame 15.173 ms. The frame's difference is noise.

**A product bug out of scope: `run_profiler` fails, before and after this work (2026-11-05).** Expected: `what_is_aimed_at_and_what_lies_there_have_kernels_when_they_are_there` passes. Actual: `tests/run/profiler/index.c:1095`, `overlay > 0u && overlay < frames`, with `161 frames: entity_rays in 116, entity_overlay in 161`. A clean build of `3c6d85f`, the main this branch merged, gives the same numbers and the same failure, so it isn't this stage's. It looks as though the body now always aims at a block, from the new dry-land start, so that the test's idea of a frame with no outline is out of date, but I didn't confirm that. Left alone, as it's outside this stage. It's the only one of the 104 tests failing: full `ctest --preset debug` said 103 passed.

**Gate check, an eye in kelp breathed and saw black (2026-11-05).** Expected: every eye in the sea uses up air and sees water. Actual: kelp and seagrass take a water cell's place in the world, and `movement_touches` asked `eyes == BLOCK_ID_WATER`. So an eye in a stalk refilled air at 4 a tick and lost the flag, and a body wholly in a stalk stopped swimming. Giving the flag by hand was not enough: `shade_kernel` only moved `t` for `layer == 0u && !has_face`, and from a stalk's cell the first water has a face and comes after a clear texel. With the old code, the new tests give `an eye in kelp: 0 pixels of 57600 are far water`, and `run_player_survival` fails 9 of 13547 checks in its new test. Fixed: `movement_cell_is_water` counts a stalk by what is over it, so kelp placed on land is still a plant, and `shade_kernel` moves `t` while only water has been drawn on the ray. Now 40796 of 57600 pixels are far water, and the rest are the plant. New tests: `kelp_and_seagrass_in_the_sea_give_no_air_and_kelp_on_land_takes_none` and the kelp part of `flag_off_or_setting_off_is_the_same_picture_and_on_is_water`.

**Gate check, stage 2's "dark hill" was mostly a dark box around kelp (2026-11-05).** Expected from stage 2's notes: the dark mass at the left of `sea.png` is the bed. Actual: part of it was a stalk near the eye. A clear texel of its cell stopped `t` there, which drew the whole cell as near, dark water. With the fix above the stalk reads as kelp, with the bed behind it and seagrass on the bed: seen at 960 by 540 in the real game. `sea.png` and `sea_no_air.png` came out with 6571 and 6587 pixels changed, so I looked at them and wrote them again with `MC_GOLDEN_WRITE=1`. `land.png` did not change. The bed right beside the eye is still dark, as stage 2 says.

**Gate check, what real play showed (2026-11-05).** All headless, in the built program, and I read the pictures:

- **Drowning, through the real menu:** seed typed into a new world, walked to the sea at x 79, y 579, down to z = -16. One bubble goes about every 30 ticks, no heart goes while the row has a bubble, and the empty row comes before the hearts start, which then go 1 each 20 ticks. A settings file with no `effect_under_water` line keeps the effect on, by `README` and by the code. The report said `hurt 8 times`, with no zombie or skeleton blows.
- **Dying:** `--test-world --walk`, staying down. One heart left at tick 2500 and `You died` by 2700, still seeing water, with the row hidden.
- **Coming up:** swimming up from z = -19, at noon and at midnight. The bubbles refill, then go. The water at midnight is dark teal, not black, and no frame of the 21 flashes wrong. This was in screenshots 5 to 20 ticks apart, not in motion.
- **Ahmed's ice, seed 8:** walked out onto a frozen sea, mined the ice underfoot, went in. From above, the hole shows blue water, not a void. Under the ice it is blue all round, with bubbles and hearts clear, and looking up shows the shape of the hole with the sun through it. The underside of the ice is hard to tell from far water, though the hole shows. Left alone: that's for Ahmed to judge, and he didn't ask for it.

**Gate check, left alone (2026-11-05).**

- Mined ice leaves an air cell, not water: the hole's water is one block below the ice. Not this phase's.
- With the flag on and the eye just over the surface between ticks, a ray now counts to where the water stops, where before it counted to the surface. That is still at most a tick, and no screenshot caught it.
- `bench_biome_map --help` takes `--help` as its directory and writes a map there. A product bug out of scope: I moved the stray output out of the tree.

**Gate check, the runs (2026-11-05).** Clean debug build, then the phase's Verification, all 6 passed, before and after the fix. Whole debug suite with `-E soak`, as this touched `shade` and `movement`: 104 of 105 in 1781 s. The one failure was `run_profiler`, as stage 2 says. Main fixed that in `9a8c337`, and after merging it `run_profiler`, `run_player_felling` and `run_item_equipment` pass, and the merge touched only tests and notes. Release build: `render_feature_under_water`, `run_player_survival`, `run_player_movement` and `run_ui_hud` pass. The stage 2 cost was not measured again: the only new work is one `bool` on a ray.

Nothing in this phase's code is a stub or a TODO, and every ticked box has code and a test behind it. Ahmed's check is the one thing left.

**Needs no new blocks or items,** so it doesn't touch saved worlds, and can be built any time.
