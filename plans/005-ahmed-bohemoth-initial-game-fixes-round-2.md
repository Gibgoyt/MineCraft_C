# 005-ahmed-bohemoth-initial-game-fixes-round-2

**Status:** collecting notes, nothing implemented   (updated 2026-11-05)

## Context

Ahmed's second round of notes from playing, after plan 003 was built. They're from `../prompt.md` and from chat. **Plans only: nothing is implemented until Ahmed says to start.** This is a living file, with one phase file per topic, following `plans/README.md`.

Plan 004 is Ahmed's climate system, so these notes are here in 005, as he asked.

**Playing:** `main` at `c09513e` or later.

## Ahmed's notes

| # | Note | Phase file | Cause found | Status |
|---|---|---|---|---|
| 1 | The tree feller must always be on, not need an axe | `...-phase-01-tree-feller-always-on.md` | Plan 003's "axes only" default | Not started |
| 2 | Doors can't be placed | `...-phase-02-doors.md` | Door blocks were never added | Not started |
| 3 | Turn off zombies, skeletons and creepers, for Ahmed's own mobs later | `...-phase-03-turn-off-hostile-mobs.md` | Data change: `weight: 0` | Not started |
| 4 | Sheep, and a bed, to sleep through the night | `...-phase-04-sheep-wool-bed.md` | No sheep or wool exist; the bed's recipe uses wheat | Not started |
| 5 | No armour bar | `...-phase-05-armour-bar.md` | It was never drawn | Not started |
| 6 | Woodworking bench can't make planks or sticks, overlaps the table; a stoneworking bench; the table only makes benches | `...-phase-06-crafting-stations.md` | Planks and sticks were kept on the table to reach the bench | **Blocked: Ahmed's Hypixel research** |
| 7 | Very laggy after a long time in another window | `...-phase-07-lag-after-alt-tab.md` | Likely the world filling to its 70-mob cap while it never paused. Not proven | Not started |
| 8 | Can't see very far; what's a chunk? | `...-phase-08-render-distance.md` | Working as set: 32 chunks is 512 blocks, and nothing is drawn past full detail at 32 | Not started |
| 9 | Use Minecraft's default textures, not Pixel Perfection: download it, keep Pixel Perfection | `...-phase-09-default-textures.md` | The repo is public, and the texture folders hold the pixels too, so the default pack goes in a git-ignored copy | Not started |
| 10 | A translucent blue preview of a block before placing it, like Hypixel | `...-phase-10-placement-preview.md` | Only an outline of the aimed-at block exists | Not started |
| 11 | Stoneworking and iron-working benches; wooden, stone and iron tools; maybe a 2×2×1 smith table for metal ores, metal tools and all armour | Added to `...-phase-06-crafting-stations.md` | New stations | Part of phase 6 |

## Order to build

| Order | Phase | Why here |
|---|---|---|
| 1 | 3 Turn off hostile mobs | Smallest, and may remove the lag as a side effect |
| 2 | 7 Lag | Measure first, then pause on focus loss |
| 3 | 1 Tree feller | Small, and plays every session |
| 4 | 5 Armour bar | Small |
| 5 | 8 Render distance | Small, and needs a measurement |
| 6 | 2 Doors | Bigger, and if Ahmed picks a real block state, that comes first |
| 7 | 4 Sheep and bed | Needs phase 3's spawn changes |
| 8 | 9 Default textures | Independent of the rest; ready to go |
| 9 | 10 Placement preview | Best after doors and stations exist, so it knows every shape |
| 10 | 6 Crafting stations | When Ahmed's research is done |

## Questions for Ahmed

| Phase | Question | Default if not answered |
|---|---|---|
| 1 | Keep sneak to chop one log? | No: always on, no exceptions |
| 2 | Doors as 16 blocks each, or build a real block state first? | **Recommended: a block state first**; until then, 16 blocks each |
| 4 | Should the bed be two blocks long like Minecraft's? | No, it stays one |
| 6 | The order of stations, and where mixed materials such as torches and iron tools go | **No default: blocks phase 6** |
| 7 | Pause when the window loses focus? | Yes, with a setting |
| 6 | Is the smith table the iron-working bench, or are both wanted? | One station: the smith table |
| 6 | Does "any metal" include diamond tools, and "armour only here" include leather? | Yes to both |
| 10 | When placing would fail, show nothing or red? | Nothing |

## Decisions Ahmed has made

| Date | Decision |
|---|---|
| 2026-11-05 | Default textures: downloaded from Mojang, kept out of git, with Pixel Perfection kept |
| 2026-11-05 | Stations: woodworking, stoneworking, iron-working, and maybe a 2×2×1 smith table that alone makes armour |

## How to test the game now

This is the build at `main`. It has everything from plan 003, and none of this plan.

**Build**, from `~/Projects/Games/MineCraft_C`:

    tools/test-or-build.sh cmake --preset release
    tools/test-or-build.sh cmake --build --preset release

**Run**, in a window on the desktop (Alt+Tab lets go of the mouse):

    ./build/release/bin/minecraft_c --windowed

**Make a new world.** Worlds from before the height change are refused with a message on the list. Then:

| To check | What to do | What you should see today |
|---|---|---|
| Start | Make a new world | On land, with x y z top left |
| World | Look around, and climb | Long mountain ranges, seas that end, peaks up to about 1150 |
| How far you see | Settings → render distance 128, full detail 32 | Land drawn coarser out past 512 blocks (phase 8 explains this) |
| Caves | Find an opening and walk in, with torches | Passages underground. Dark without torches |
| Under water | Swim down and stay | The view goes blue, 10 bubbles empty, then hearts drop |
| Tree feller | Chop a log **with an axe**, then with bare hands | The axe brings the tree down. Hands take one log (phase 1 changes this) |
| Wall torches | Place a torch on each side of a block, then mine the block | Torches lean out from walls, and drop when it goes |
| Copper armour | Smelt copper, make the four pieces on the table, wear them | They're worn, but **no armour bar** (phase 5) |
| Woodworking bench | Table: 3 planks over 2 sticks. Place it, make stairs on it | Wood stairs and tools come off the bench. **Planks and sticks don't** (phase 6) |
| Doors | Craft a door and try to place it | **Nothing happens** (phase 2) |
| Bed | 3 wheat over 3 planks on the table, place it, sleep at night | Sleeps through the night. **The recipe uses wheat**, as there's no wool yet (phase 4) |
| Height | Build up | A block goes at z = 1250, and not at 1251 |
| Sound | Mine stone, wood, dirt, sand and leaves; change volume in Settings | Crunches by material. **No agent has heard these** |
| Lag | Play, then go to another window for 10+ minutes, and come back | Likely laggy (phase 7). Press F3 and note the numbers |

**To see the world from above:**

    tools/test-or-build.sh timeout 600 build/release/bin/bench_world_map /tmp/mc_c_maps 2198175988018515243

Then open the pictures in `/tmp/mc_c_maps`.

**When reporting a problem,** paste what the terminal printed when you left the world. The report at the end lists frames, mobs, ticks and saves.

## Seen in the logs, not yet asked for

Carried over from plan 003, still open. Each stays out of the work until Ahmed says to add it.

| # | What |
|---|---|
| A | New worlds made in one run share a seed |
| B | Frame rate in play is lower than frame times allow. Ahmed's last session: 69.6 FPS with 8.06 ms mean frames |
| D | A new world drops a few items by itself, likely leaf decay |
| E | A floor torch shows no flame |
| F | The world list can show a seed as a negative number |
| H | The `run_audio` test failed once under load in the release suite, and passed alone 4 times (plan 003 phase 5) |

## Verification of this file

    ls plans/005-ahmed-bohemoth-initial-game-fixes-round-2*
    git status --short    # only plans/ changes

No code, build or test changes while collecting notes.
