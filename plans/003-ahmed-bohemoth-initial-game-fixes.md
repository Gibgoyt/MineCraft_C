# 003-ahmed-bohemoth-initial-game-fixes

**Status:** collecting notes, nothing implemented   (updated 2026-11-04)

## Context

Ahmed is playing the game on his XFCE desktop and noting what to fix as he goes. This file collects those notes, with one phase file per topic. **It is plans only: no code changes until Ahmed says to start.** He adds notes over time, and each one becomes an entry below.

This is a living file, by Ahmed's direction, unlike plan 001, which `plans/README.md` rule 1 keeps verbatim.

**Direction:** "I also want the game to be sort of a hypixel + minecraft hybrid... as I research hypixel and play it myself i will create a proper game here". Hypixel features get planned as Ahmed brings them. The woodworking bench is the first.

**Playing so far:** commit `be4ae48`, on `--windowed` and full screen.

## Ahmed's notes

| # | Note | Phase file | Status |
|---|---|---|---|
| 1 | Torches can't be put on a wall | `...-phase-02-wall-torches.md` | Not started |
| 2 | Basic sound: crunches when mining different blocks | `...-phase-05-sound.md` | Not started |
| 3 | Can't make wooden stairs on a crafting table | `...-phase-03-wood-stairs.md` | Not started |
| 4 | A woodworking bench for all woodworking; benches are 2x1x1 on x, y, z | `...-phase-04-woodworking-bench.md` | Not started |
| 5 | Can't make copper armour | `...-phase-06-copper-armour.md` | Not started |

## Phases, in the order to build them

| Phase | What | Why here |
|---|---|---|
| 1 | Worlds survive new blocks. **Not requested: needed by the rest** | Adding any block today makes the game refuse every saved world |
| 2 | Wall torches | Smallest user-facing change, and proves phase 1 |
| 3 | Stairs from every wood | Adds blocks phase 4 then moves to the bench |
| 4 | Woodworking bench | Needs phase 3's recipes |
| 5 | Basic sound | Independent, the most new code, and starts with a test only Ahmed can hear |
| 6 | Copper armour | Only needs phase 1, and is small, so it can go any time after that |

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

## Decisions Ahmed has made

| Date | Decision |
|---|---|
| 2026-11-04 | Sound uses a second local socket, for the sound server |
| 2026-11-04 | After touching each plan file, commit it |

## Seen in the logs, not yet asked for

These came from Ahmed's run logs, not his notes. They stay out of the work until he says they should be in.

| # | What | Evidence |
|---|---|---|
| A | New worlds in one run share a seed | `test_1` and `test_2` both report seed `2198175988018515243`; `test_3`, after a restart, reports `12636146071196537873`. `blank_seed` is set once, at `run/src/main/index.c:918`, and passed to `ui_menu_saves_set`. |
| B | Frame rate in play is lower than frame times allow | 720p: 64 FPS against 8.9 ms frames. 1080p full screen: 59 FPS against 13.1 ms, with `shade` averaging 10.1 ms and peaking at 20.7 ms. The window loop held 119.9 FPS. Cause not found. |
| C | Ladders and stairs have one facing | Same cause as wall torches (Stage 09.1 issue 1). A real facing state would solve torches, ladders, stairs and turning benches together, but it's a bigger change than any one of them needs. |

## Verification of this file

    ls plans/003-ahmed-bohemoth-initial-game-fixes*
    git status --short    # only plans/ changes

No code, build or test changes while collecting notes.
