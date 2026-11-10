# tests/run/ui/hud

**Purpose:** ctest `run_ui_hud`. Tests `run/src/ui/hud/`, with what `run/src/game/` does with it.
**Tier:** test.
**Owns:** the test arena, and a `ui_t`'s and a `texture_t`'s regions, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main; the real game's and the built program's threads run their own.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/ui/`, `run/src/game/`, `run/src/display/headless/png/`, `compile/src/png/` to read the golden pictures, the built program, and a card for the real game and that program.

What it holds to:

- Where things go, by hand, at three sizes; half units by hand; every item has an icon.
- Random states against a separate model, a pixel at a time on `simd_reference_blend_over`, with every byte of the guard band round the canvas unchanged, at sizes from a pixel to 1080p.
- A half shows the side nearest the full ones. Slots, counts and wear each change only their own part.
- Bubbles: 30 ticks each by hand; full, half and empty over the drumsticks, changing nothing else; not there on land; and through the real game, up and gone as air refills.
- Armour (plan 005 phase 05): 0 draws nothing, 1, 10 and 20 points change only the row of ten over the hearts, 10 to 20 only its last five; beside the bubbles, the bubbles are the same row a row higher and the armour stays; goldens `armor_1_640x480.png`, `armor_10_air_640x480.png` and `armor_20_air_320x240.png`, and with 0 the picture is `air_640x480.png` unchanged. Through the real game, a diamond helmet shows 3 by the next tick, a chestplate more beside the bubbles, and the row goes when they come off.
- The overlay's numbers change only their own line, and it stays on its panel, with the latitude and sun line of plan 004 phase 02 (its longest, `lat 90.00 S  sun -90.0`, fits). On the real game flying, that line is the eye's latitude and the sun of the clock there.
- No heap and no table memory in 200 frames.
- The top right corner of plan 004 phase 10 has its own program, ctest `run_ui_hud_corner` in `corner/`, as this file is past 1500 lines; its goldens are in `fixtures/` with these (`corner_*.png`), and it holds `date_640x480.png` and `coordinates_640x480.png` to the corner with its other lines off.
- The date line, plan 004 phase 01: a small panel at the top right just as wide as its text, nothing left of it touched, beside the coordinates and not over them, below a notice's band, nothing outside a tiny canvas, and the golden `fixtures/date_640x480.png`. Its real-game test is in `tests/run/game/census/`, as that test opens a world for the census already.
- Coordinates: a small panel just as wide as its text, each number changing it, below a notice's band, and on the overlay's panel with room for the longest. On the real game, whole blocks from the sim and rounded down past 0, drawn only when the config says, and not twice with F3 up.
- **The real game, one thread:** with a `ui_t`, the target is the model drawn over the picture the card made, and that picture is not touched. Without one, the target is the picture, byte for byte. F3 turns the overlay on and off, flying too.
- **The real threads:** what is put on the hotbar reaches the screen across threads, the overlay's numbers are real ones, and a frame takes nothing from the heap.
- **Real keys, on a fake device:** F3 through `game_loop_source`: one press however long it is held, and a tap over inside one poll still counts.
- **The built program:** `--headless --test-world --walk` has the HUD's solid pixels where they should be, and with F3 in its script differs only inside the panel. It ends with no leak.

**Golden pictures:** `fixtures/*.png`, exact to the byte: the same code on the same textures has no reason to differ, as the blend kernel is integer arithmetic, and nothing here depends on the card. To make them again, after looking at them:

    MC_GOLDEN_WRITE=1 tools/test-or-build.sh ctest --preset debug -R '^run_ui_hud$'

Any change to a texture or the font changes them, and they are made again and looked at. `debug_640x480.png` was remade on 2026-11-06 for plan 004 phase 02, after looking at it: the overlay's panel is one line taller, and that line reads `lat 1.33 N  sun 30.0`.
