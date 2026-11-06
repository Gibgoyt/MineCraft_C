# tests/run/player/survival

**Purpose:** ctest `run_player_survival`. Tests `run/src/player/survival/`, with what `run/src/player/` and `run/src/game/` do with it.
**Tier:** test.
**Owns:** the test arena, and what its world, game and card blocks take, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main; and for the test of the real loop, the real workers and the real `sim` and `gpu` threads.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/player/`, `run/src/game/` and everything under it, `config/items/`, and a card for the tests of the game.

Compute only. No real display, console or input device is opened: pictures go into plain memory, and the program always runs `--headless`.

What it checks, with every number worked out by hand from the table in `run/src/player/survival/README.md`:

- **The sums:** armor, a fall and night for any numbers, and what counts within ten ticks of a hit. Night is by the sun's height (plan 004 phase 02); on the equator at the equinox it is the old two fractions at every time of day but the last tick of dawn, where the old ones were not symmetric about noon.
- **What the world does:** a real body really falling from each height, on to water, a ladder and a bed; lava every ten ticks to death, what it lights, and water putting that out; a cactus from the side and on top, and not from a block away; air running out, gasps and coming back; a block at the eyes, and glass and leaves not smothering.
- **The bar:** through `game_hud_state_read`, not up on land, a bubble gone each 30 ticks with no heart lost while there is one, empty on tick 300, the first gasp on tick 320, up and empty while drowning, back at 4 a tick, gone when full, and gone on death.
- **Food:** what walking, sprinting, jumping, swimming and mining take; saturation before food; healing at both rates; starving down to 1; sprinting stopping at 6; eating in 32 ticks, and what starts it over; and every food's row.
- **Death and coming back:** falling while dead with nothing moving it and no hands; a held button, and a press too soon or with no one at the controls, not counting; everything full again; going up past what was built at the place; and holding still with the ground not loaded, and nothing hurting it meanwhile.
- **The bed:** by day it only sets the place; a night is 100 ticks, then morning beside it; a press, a hit and the bed going end it early with no morning; sneaking places against it; one too far off says so; and coming back to it, walled in, or with it gone.
- **The same bytes,** from the same world and buttons, with nothing from the heap.
- **The real game:** on the card, through the headless scene's ticks: eats, keeps a bowl, places a bed that can be seen, sleeps with the morning reaching `game_t`'s day, dies with all it carried spilled and books that add up, comes back by the bed and picks it up.
- **The real threads:** eats, dies in real lava, and is brought back to a real bed by a press between two ticks, with main seeing it all through `player_vitals_read`.
- **The program:** a script sprints and jumps for 15 s, the report says saturation went down and nothing hurt, and a second run says the same.

What it cannot do from a script is get a bed or any food: a script cannot give items and no recipe makes a bed. So the game tests above fill the inventory through the API. See the stage file's issues.
