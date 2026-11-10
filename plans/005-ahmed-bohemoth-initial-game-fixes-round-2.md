# 005-ahmed-bohemoth-initial-game-fixes-round-2

**Status:** being built by waves of agents, from 2026-11-10. Done so far: phases 3, 5 and 9; phase 8's labels; phase 14's measuring and live test (the starvation fixes are in progress). **Until phase 14 is built, play at render distance 32 with full detail 32,** which draws correctly   (updated 2026-11-09)

## Context

Ahmed's second round of notes from playing, after plan 003 was built. They're from `../prompt.md` and from chat. **Plans only: nothing is implemented until Ahmed says to start.** This is a living file, with one phase file per topic, following `plans/README.md`.

Plan 004 is Ahmed's climate system, so these notes are here in 005, as he asked.

**Playing:** `main` at `c09513e` or later.

## Ahmed's notes

| # | Note | Phase file | Cause found | Status |
|---|---|---|---|---|
| 1 | The tree feller must always be on, not need an axe | `...-phase-01-tree-feller-always-on.md` | Plan 003's "axes only" default | Not started |
| 2 | Doors can't be placed | `...-phase-02-doors.md` | Door blocks were never added | Not started |
| 3 | Turn off zombies, skeletons and creepers, for Ahmed's own mobs later | `...-phase-03-turn-off-hostile-mobs.md` | Data change: `weight: 0` | **Done** (`121e880f`): weight 0, and taken out of saved worlds as they open |
| 4 | Animals with males and females: cattle, sheep, pigs, goats, mountain goats, chickens. Females give milk or eggs, males are food. A bed from wool | `...-phase-04-animals.md` | No animals, wool, hides or meat exist; mobs spawn only in the dark; a bed works but needs wheat | Not started |
| 5 | No armour bar | `...-phase-05-armour-bar.md` | It was never drawn | **Done** (`80435865`): ten icons above the hearts, bubbles a row higher |
| 6 | Woodworking bench can't make planks or sticks, overlaps the table; a stoneworking bench; the table only makes benches | `...-phase-06-crafting-stations.md` | Planks and sticks were kept on the table to reach the bench | **Blocked: Ahmed's Hypixel research** |
| 7 | Very laggy after a long time in another window | `...-phase-07-lag-after-alt-tab.md` | Likely the world filling to its 70-mob cap while it never paused. Not proven | Not started |
| 8 | Can't see very far; what's a chunk? | `...-phase-08-render-distance.md` | Working as set: 32 chunks is 512 blocks, and nothing is drawn past full detail at 32 | **Labels done** (`e0cf3d33`): sliders show blocks, with a tooltip each. Raising the default waits for phase 14 |
| 9 | Use Minecraft's default textures, not Pixel Perfection: download it, keep Pixel Perfection | `...-phase-09-default-textures.md` | The repo is public, and the texture folders hold the pixels too, so the default pack goes in a git-ignored copy | **Done** (`af93d651`): `tools/fetch_minecraft_default.sh`, then `tools/import_minecraft_default.sh`, builds from `textures_local/`. Nothing from Mojang is committed |
| 10 | A translucent blue preview of a block before placing it, like Hypixel | `...-phase-10-placement-preview.md` | Only an outline of the aimed-at block exists | Not started |
| 11 | Stations: stoneworking bench, a 2×2×1 smith table for iron, copper and gold, a leather tanning rack and a leather workbench. **No** iron-working bench. No armour without the leather workbench. Realistic furnaces | `...-phase-06-crafting-stations.md` | New stations | Part of phase 6 |
| 12 | More ores and alloys: chromium ore with its spawn rate in YAML, and stainless steel from 9 iron and 1 chromium | `...-phase-11-ores-and-alloys.md` | Ores are a table in code, not YAML; no alloys exist | Not started |
| 13 | Stairs and the woodworking bench only face north: face the player, and turn before placing | `...-phase-12-block-facing.md` | A voxel holds only an id, so no block can face a way | Not started |
| 14 | A blue preview for every block, including the next block of a 1×1 bridge | Added to `...-phase-10-placement-preview.md` | — | Part of phase 10 |
| 15 | Place a block ahead while looking forward, like Bedrock, without turning to the edge | `...-phase-13-forward-placement.md` | Placing needs the aim to hit a block face, and looking ahead off a bridge it hits nothing | Not started |
| 16 | The tree feller wears the axe for every log that falls, not just the ones chopped | Added to `...-phase-01-tree-feller-always-on.md` | Plan 003's default "each log wears the axe" | Part of phase 1 |
| 17 | Loading takes about 5 minutes, and changing render distance or full detail lags. A change must show in under 1 second, tested by agents in a live game | `...-phase-14-loading-and-render-distance-speed.md` | The far field starving the workers (note 18). **Not sound:** the build Ahmed tested predates sound, and a render distance 32 run with sound draws correctly | Not started |
| 18 | Rendering issues on a new world: flat untextured slabs around you, a mined log still drawn (`~/Pictures/10.png`, `11.png`) | `...-phase-14-loading-and-render-distance-speed.md` | **Identified:** at render distance 64, far-field builds take all 16 workers for minutes, and the job queue has no priorities, so near chunks and mined-block redraws wait. Reproduced; render distance 32 draws correctly | Not started |
| 19 | A mined tree vanishes about 5 minutes later; changing render distance or full detail takes about 5 minutes, with rendering issues | `...-phase-14-loading-and-render-distance-speed.md` | **Measured live:** render distance 32 → 64 shows nothing new for over 16 s and settles in 3 to 4 minutes. Full detail 32 → 16 shows nothing for 45 s, then everything turns darker. The far field keeps the old picture until rebuilt, and draws no unsaved edits | Not started |

## Order to build

| Order | Phase | Why here |
|---|---|---|
| 1b | 3 Turn off hostile mobs | Smallest, and may remove the lag as a side effect |
| 2 | 7 Lag | Measure first, then pause on focus loss |
| 1 | 14 Loading and render distance speed | **First now:** the cause is known, and the game is hard to play until it is fixed |
| 3 | 1 Tree feller | Small, and plays every session |
| 4 | 5 Armour bar | Small |
| 5 | 8 Render distance | Small, and needs a measurement |
| 6 | 12 Blocks that face a way | The biggest change, needed by doors, the smith table and turning |
| 7 | 10 Placement preview | With facing and turning from phase 12 |
| 8 | 13 Placing ahead | Shares phase 10's placement function |
| 9 | 2 Doors | On phase 12's block state |
| 10 | 4 Animals | Needs phase 3's spawn changes, and gives the hides phase 6 needs |
| 11 | 9 Default textures | Independent of the rest; ready to go |
| 12 | 11 Ores and alloys | Moving ores to YAML can go any time. Alloys need phase 6's smith table |
| 13 | 6 Crafting stations | When Ahmed's research is done. Needs phases 4, 11 and 12 |

## Questions for Ahmed

| Phase | Question | Default if not answered |
|---|---|---|
| 1 | Keep sneak to chop one log? | No: always on, no exceptions |
| 14 | Are the targets right: a change shows in under 1 s, nearby ground in 2 s, everything at 32 in 15 s? | Yes |
| 12 | A block state for facing, rather than more blocks per facing? | A block state, now phase 12 |
| 13 | How far down must you look to place ahead? | Anything from level down |
| 4 | Should the bed be two blocks long like Minecraft's? | No, it stays one |
| 4 | Breeding, now that animals are male and female? | Not in this plan |
| 4 | Do bulls, rams and billy goats fight back? | No, all flee |
| 6 | The order of stations, and where torches, beds and bows go | **No default: blocks phase 6** |
| 6 | A blast furnace for iron, chromium and alloys, with the furnace for copper, gold, food and glass? | Yes |
| 6 | How much metal turns a leather armour piece into a metal one? | Minecraft's ingot count for that piece |
| 7 | Pause when the window loses focus? | Yes, with a setting |
| 10 | Which key rotates the preview? | R |
| 11 | Do 9 iron + 1 chromium make 10 stainless steel? | Yes, 10 |
| 11 | Which more ores: tin, zinc, nickel, silver? | None until Ahmed picks |

## Decisions Ahmed has made

| Date | Decision |
|---|---|
| 2026-11-05 | Default textures: downloaded from Mojang, kept out of git, with Pixel Perfection kept |
| 2026-11-05 | Stations: woodworking, stoneworking, and a 2×2×1 smith table |
| 2026-11-06 | **No iron-working bench.** The smith table works iron, copper and gold, and alloys |
| 2026-11-06 | Diamond can't be worked by anything for now, and that's fine |
| 2026-11-06 | Leather tanning rack and leather workbench, like Hytale. No armour without the leather workbench |
| 2026-11-06 | A failed placement shows red, and the preview can be rotated |
| 2026-11-06 | Animals are male or female. Only females give milk or eggs. Goats give hides, not wool. Mountain goats live in mountains |
| 2026-11-06 | Stainless steel is 9 iron + 1 chromium. Chromium ore, with its spawn rate in its YAML |
| 2026-11-06 | It should feel like a real game, not a Minecraft sandbox |
| 2026-11-06 | Blocks face the player when placed, and can be turned before placing, starting with stairs and the woodworking bench |
| 2026-11-06 | Every block gets the blue preview, including bridging, and a block can be placed ahead while looking forward |
| 2026-11-09 | Felling wears the axe only for logs chopped by hand |
| 2026-11-09 | **The tree feller works whether or not you hold an axe** ("must be on regardless if I mine a tree with an axe!!") |
| 2026-11-09 | Agents must test render distance changes in a live game, and see them in under a second |

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
| How far you see | Keep render distance 32, full detail 32 | **Don't raise render distance yet:** at 64 or more the far field starves the near world (phase 14) |
| Caves | Find an opening and walk in, with torches | Passages underground. Dark without torches |
| Under water | Swim down and stay | The view goes blue, 10 bubbles empty, then hearts drop |
| Tree feller | Chop a log **with an axe**, then with bare hands | The axe brings the tree down. Hands take one log (phase 1 changes this) |
| Wall torches | Place a torch on each side of a block, then mine the block | Torches lean out from walls, and drop when it goes |
| Copper armour | Smelt copper, make the four pieces on the table, wear them | They're worn, and the armour bar fills above the hearts |
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
| B | Frame rate in play is lower than frame times allow. Ahmed's last session: 69.6 FPS with 8.06 ms mean frames. Phase 14 measures alongside it |
| D | A new world drops a few items by itself, likely leaf decay |
| E | A floor torch shows no flame |
| F | The world list can show a seed as a negative number |
| H | The `run_audio` test failed once under load in the release suite, and passed alone 4 times (plan 003 phase 5) |

## Verification of this file

    ls plans/005-ahmed-bohemoth-initial-game-fixes-round-2*
    git status --short    # only plans/ changes

No code, build or test changes while collecting notes.
