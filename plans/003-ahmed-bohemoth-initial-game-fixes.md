# 003-ahmed-bohemoth-initial-game-fixes

**Status:** all ten notes built, and played end to end in the built program by the final check, round 1. What is left is Ahmed's: playing on his desktop, and listening, as no agent has heard a sound. See "Final check" below   (updated 2026-11-05)

## Context

Ahmed is playing the game on his XFCE desktop and noting what to fix as he goes. This file collects those notes, with one phase file per topic. It began as plans only, and Ahmed then said to build it. He adds notes over time, and each one becomes an entry below.

This is a living file, by Ahmed's direction, unlike plan 001, which `plans/README.md` rule 1 keeps verbatim.

**Direction:** "I also want the game to be sort of a hypixel + minecraft hybrid... as I research hypixel and play it myself i will create a proper game here". Hypixel features get planned as Ahmed brings them. The woodworking bench is the first.

**Playing so far:** commit `be4ae48`, on `--windowed` and full screen.

## Ahmed's notes

| # | Note | Phase file | Status |
|---|---|---|---|
| 1 | Torches can't be put on a wall | `...-phase-02-wall-torches.md` | Done and played: a torch on each of four walls and on the floor. Ahmed to check |
| 2 | Basic sound: crunches when mining different blocks | `...-phase-05-sound.md` | Built, and tested on bytes in memory and with silence. Nobody has heard it: Ahmed to listen |
| 3 | Can't make wooden stairs on a crafting table | `...-phase-03-wood-stairs.md` | Done: stairs of every wood, made on the bench (note 4). Played with spruce. Ahmed to check |
| 4 | A woodworking bench for all woodworking; benches are 2x1x1 on x, y, z | `...-phase-04-woodworking-bench.md` | Done and played: made, placed, spruce stairs on it and not on the table. Ahmed to check |
| 5 | Can't make copper armour | `...-phase-06-copper-armour.md` | Done and played: all four made on a table and worn. Ahmed to check |
| 6 | Tree feller: no more floating wood, and Hypixel has one | `...-phase-07-tree-feller.md` | Done and played: one cut felled 47 logs. Ahmed to say how it feels |
| 7 | One mountain and a huge ocean: proper world generation, mountains, caves, a large continuous world | `...-phase-08-world-generation.md` | Done and played: starts on land, ranges to 1151, a cave walked into. Ahmed to judge the maps and the feel |
| 8 | Breath while under water | `...-phase-09-breath-underwater.md` | Done and played: blue water, bubbles run out, then hearts. Ahmed to check |
| 9 | Coordinates, as an option in settings | `...-phase-10-coordinates.md` | Done: on by default, in every picture. Ahmed to check |
| 10 | World from -250 to +1250 (it was first asked for and built as +500) | `...-phase-11-world-height.md` | Done and played: a block at z = 1250, refused at 1251. Ahmed to check |

## Phases, in the order to build them

File numbers are the order they were written. This is the order to build them, with the world first, because it's the biggest thing wrong and everything else is played in it.

| Order | Phase | What | Why here |
|---|---|---|---|
| 1 | 10 | Coordinates | Small, and helps judge everything after |
| 2 | 11 | Taller world, to +1250 | Before terrain is tuned, so it's only tuned once |
| 3 | 8 | World generation | The biggest problem. Starts with a map tool |
| 4 | 9 | Breath and seeing under water | Small, and prevents invisible deaths |
| 5 | 1 | Worlds survive new blocks. **Not requested: needed by the rest** | Adding any block today makes the game refuse every saved world |
| 6 | 2 | Wall torches | Smallest change that adds blocks, and proves phase 1 |
| 7 | 7 | Tree feller | Needs nothing else, so it can move earlier |
| 8 | 6 | Copper armour | Needs phase 1 |
| 9 | 3 | Stairs from every wood | Adds blocks that phase 4 then moves to the bench |
| 10 | 4 | Woodworking bench | Needs phase 3's recipes |
| 11 | 5 | Basic sound | Independent, the most new code, and starts with a test only Ahmed can hear |

## Questions for Ahmed

Each is also in its phase file. None blocks collecting more notes.

| Phase | Question | Default if not answered |
|---|---|---|
| 1 | Fine to lose today's test worlds, once? | Yes, they're test worlds |
| 3 | Did you use oak planks for the stairs? That tells us if there's a second cause | Test settles it |
| 3 | Should slabs get every wood too? | No |
| 4 | Do wooden tools move to the bench? | Yes, as "all wood working" |
| 4 | Bench recipe: 3 planks over 2 sticks? | Yes |
| 5 | Use the spare CPU thread for audio? | Yes |
| 6 | Are the proposed copper armour stats OK? | Yes |
| 6 | Copper tools too? | No |
| 7 | Should bare hands fell trees too? | No, axes only |
| 7 | Should each log wear the axe? | Yes, one use per log |
| 7 | Sneak to chop just one log? | Yes |
| 8 | Are the world targets right: about 25–30% sea, and mountain ranges? | Decide by looking at the map |
| 10 | Show coordinates by default? | Yes, while testing |

## Decisions Ahmed has made

| Date | Decision |
|---|---|
| 2026-11-04 | Sound uses a second local socket, for the sound server |
| 2026-11-04 | After touching each plan file, commit it |
| 2026-11-04 | Open-source generators may be cloned to `/tmp/` to learn from |
| 2026-11-04 | The world runs from -250 to +1250, replacing the original +500 |

## Seen in the logs, not yet asked for

These came from Ahmed's run logs, not his notes. They stay out of the work until he says they should be in.

| # | What | Evidence |
|---|---|---|
| A | New worlds in one run share a seed | `test_1` and `test_2` both report seed `2198175988018515243`; `test_3`, after a restart, reports `12636146071196537873`. `blank_seed` is set once, at `run/src/main/index.c:918`, and passed to `ui_menu_saves_set`. |
| B | Frame rate in play is lower than frame times allow | 720p: 64 FPS against 8.9 ms frames. 1080p full screen: 59 FPS against 13.1 ms, with `shade` averaging 10.1 ms and peaking at 20.7 ms. The window loop held 119.9 FPS. Cause not found. |
| C | Ladders and stairs have one facing | Same cause as wall torches (Stage 09.1 issue 1). A real facing state would solve torches, ladders, stairs and turning benches together, but it's a bigger change than any one of them needs. |
| D | A new world drops items by itself | Phases 2 and 6: on seed 36, 3 items on the ground after 53 ticks and 119 after 5251, with nothing mined. Likely leaf decay on mangroves. Not traced |
| E | A floor torch shows no flame | Phase 2 issue 7: its box stops at 20 of 32, under the pack's flame. Seen again in the final check's `floor.png` |
| F | The world list shows a seed as a signed number | `run/src/ui/menu/index.c:219` prints `PRId64`, so a seed the log gives as unsigned can show as negative: the final check saw `-6692979156013209846` on the list |
| G | Ahmed's three test worlds are still in the main checkout's `saves/` | The final check copied them, with his `settings.yaml`, and opened them: each is refused on the list with "its land is from another game version", and the game goes on. No agent may touch the main checkout, so they are his to delete |

## Final check, round 1, 2026-11-05

On a clean build of main at `c56e17e`, in the built program, with real keys and mouse and the coordinates on screen. Where a script needed items in hand, a saved world was given them by an edit of its state file outside the repo, as phases 2 and 6 did; everything after that was real input. Pictures read one by one.

| Ahmed's words | What was played | Seen |
|---|---|---|
| Start on land | New worlds on 6 seeds, his `2198175988018515243` among them | All on dry land: snow, sand, mud |
| A large continuous world, mountain ranges | `bench_world_map` on his seed | 29.1% sea, 96.5% of the land in one piece, 14 high pieces over 1 km^2, ground to 1151 |
| Caves | `bench_cave_walk`'s walks, played from the menu on the default seed and his | Into a dark passage 24 under the ground on the default seed. On his, 2614 ticks long, mobs killed the body on the way |
| Breath under water | Into the sea on the default seed and down | The view goes blue, ten bubbles empty, then hearts go, then death |
| Tree feller | `fell.script`, fixed: phase 7 issue 14 | One cut: 47 logs felled, none left in the air |
| Wall torches | Four dirt blocks round the body, a torch on each near side, one on the floor, one wall mined | A torch leaning from each of the four, the floor torch, and the mined wall's torch gone |
| Copper armour | All four from ingots on a table, worn by shift-click | 121, 176, 165 and 143 uses; all four worn, by the state file |
| Woodworking bench, stairs | A bench from 3 planks over 2 sticks on a table, placed, spruce stairs on it | The stairs' pattern makes nothing on the table, and spruce stairs on the bench, one placed |
| Height to 1250 | `run_save_state_game`'s built program, pictures kept: phase 11 issue 14 | Placed at z = 1250, refused above, stood at 1251 |
| A window | `./build/release/bin/minecraft_c --windowed` with his settings, on a private Xvfb with xfwm4, closed by its button | Menu, a new world on land, mined, inventory, walked; 119.5 FPS; 0 bytes live at exit |

Suites, all through `tools/test-or-build.sh`: debug, 119 of 119 in 1934 s, with the lint and clang-format. Sanitize, 122 of 123: `run_audio_pulse`, a bug in that test, fixed (phase 5's log), and 4 of 4 audio tests after. Release built clean.

Found and fixed: `fell.script` stale since wooden tools moved to the bench, now run by `run_playthrough` (phase 7 issue 14); the sanitize failure above. Out of scope, in the table above: D to G.

Left for Ahmed: the boxes marked for him in the phase files, which are playing it on his desktop and listening, as no agent may play a sound aloud.

## Verification of this file

    ls plans/003-ahmed-bohemoth-initial-game-fixes*
