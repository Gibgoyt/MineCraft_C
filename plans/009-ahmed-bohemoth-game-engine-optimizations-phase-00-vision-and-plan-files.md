# 009-ahmed-bohemoth-game-engine-optimizations — phase 0: The vision, and the plan files

**Status:** done   (updated 2026-11-12)

**Read this file before any other file of plan 009,** then the master `plans/009-ahmed-bohemoth-game-engine-optimizations.md`. Every phase file assumes you know what is here.

- `cuda/src/traverse/README.md`, `cuda/src/lod/README.md`, `run/src/render/lod/README.md` and `cuda/src/shade/README.md` describe the renderer this plan changes.
- `plans/008-ahmed-bohemoth-smooth-frames-when-turning.md` describes the capped card and the instruments this plan borrows.
- `compile/src/machine_config/README.md` and `compile/src/game_tables/README.md` describe the two existing YAML-to-C tools that phase 02's tool follows.

## Goal

Two things are true when this phase is done:

1. **Every agent working on plan 009 can say in a few sentences:**
   - **What Ahmed asked:** no "Full detail" setting; blocks merged into larger shapes, near and far; everything seen within 16 blocks drawn properly, coarser further out; research from Unreal, Godot and voxel engines; every tuning number in YAML read at compile time, at `compile/config/<path>`.
   - **What this engine is:** a CUDA voxel ray caster with no triangles. So "fewer triangles" means **fewer steps per ray and fewer rays** here (master 2.1).
   - **Why 5.png is 29 ms:**
     - the card runs at about a third of its clock (plan 008);
     - primary rays graze the slope voxel by voxel;
     - far rays pass by full detail from the eye;
     - leaves send rays through holes;
     - the moon's shadow rays run uncapped over the terrain at 13° elevation.
   - **What plan 009 changes:**
     - measure first;
     - tuning YAML;
     - one pyramid of levels, chosen per ray by pixel error, with a guaranteed level 0 within 16 blocks, replacing Full detail;
     - merged boxes for empty space and water;
     - secondary rays by level;
     - temporal upscaling.
   - **What it must not change:** any pixel whose surface is within 16 blocks, which stays byte-identical to level 0 everywhere; and any data on disk.
2. **The plan files exist:** the master, and phase files 01 to 08, each with an unticked todo list and the status `not started`, as `plans/README.md` asks.

---

## Part 1: Where plan 009 sits

### In the renderer

| Pass | Kernel / file | What 009 does to it |
|---|---|---|
| Coarse | `traverse_coarse_kernel`, `cuda/src/traverse/index.cu:146-217` | Phase 05: far stretches test the pyramid's coarse levels |
| Primary | `traverse_primary_kernel`, `cuda/src/traverse/index.cu:219-239`; the loop in `cuda/include/traverse/index.hu:305-455` | Phase 04: one loop over the pyramid, banded by distance; phase 05: 2³ coalescing, brick distances |
| Far field | `lod_past_kernel`, `lod_frame_kernel`, `cuda/src/lod/index.cu:305-410`; `lod_trace` `:106-155` | Phase 04: the pass-by of full detail's columns goes; a level is only used past its band edge |
| Shade | `shade_sun_seen` `cuda/src/shade/index.cu:28-100`; `shade_kernel` `:737`; `shade_water_through` | Phase 06: shadow rays by cone and band, dim-light cut, alpha by distance; phase 05: uniform water bricks |
| Post | `post_taa_kernel`, `cuda/src/post/index.cu:123` | Phase 07: accumulation at output resolution from a smaller picture |
| Levels | `run/src/render/lod/` (terrain-built), `run/src/render/encode/`, `cuda/src/world_buffers/` | Phase 03: level 1 and up built on workers from the map's loaded sections |
| Settings | `run/src/ui/screen_settings/`, `run/src/settings/persist/`, `run/src/game/view/`, `run/src/main/` | Phase 04: Full detail removed; the block radius worked out |
| Build | `cmake/generated.cmake`, `compile/` | Phase 02: `compile/config/` and `compile/src/tuning/` |

### Beside the other plans

- **Plan 008** owns pacing, the readback stream, the AO cache, the resolution ladder, the far-field turn cache and the instruments (per-kernel shade clocks, the frame log, the card's MHz, `--at X Y Z --seed S`). Plan 009 uses them and builds none of them again.
  - 009 phase 01 starts after 008 phase 01 has landed. If it has not, 009 phase 01 adds only its own counters and the reference mode, and writes in Issues found what it lacked.
  - 009 phase 07 builds on 008 phase 05. If 008 phase 05 is not built, 009 phase 07 waits.
- **Plan 006** phase 01 edits `run/src/game/index.c` and F3. 009 phase 04 rebases onto it.
- **Plan 007** touches terrain only. It does not overlap.
- No file of plans 006, 007 or 008 is edited by this plan.

### What was read for this plan, and what was not measured

Read:

- the renderer's code and READMEs, file by file (master section 0);
- plans 001 (section 2), 005 phases 8 and 14, and 008;
- `research/001.md`, `002.md` and `003-speed-research.md`.

The web research is cited in master section 13.

**Nothing was run for this plan.** No build, no test and no card work, as Ahmed asked for research and plans only. Every cost in the master is either F3's numbers from 5.png, plan 005's and 008's recorded measurements, or marked as judgment.

---

## Part 2: Rules every phase follows

- **Heavy commands go through the mutex**, one command per call, with `timeout` inside: `tools/test-or-build.sh timeout 900 ctest --preset debug ...` (`.claude/rules/test-or-build-mutex.md`).
- **Never touch Ahmed's game.** Before any card work, `pgrep -af 'build/release/bin/minecraft_c'` must print nothing; if it prints something, ask him and wait. Nothing is left running: `pgrep -af "$PWD/build"` prints nothing at the end of every phase.
- **Never start the game for him.** A phase ends by telling him the exact command to run.
- **The near guarantee is a test, not a hope.** From phase 04 on, every phase runs the near-guarantee test (master section 8) and it must pass.
- **Pictures move only on purpose.** Phases 01, 02 and 03 change no picture: every golden picture and every settled `bench_world` hash stays the same bytes. Phases 04 to 07 change pictures; each new hash is recorded only after looking at the shot.
- **Every new number is a tuning YAML key from phase 02 on**, with `why`, unit and range. No new magic constant in code.
- **One commit per plan file touched**, as Ahmed, `git commit -F -`, one sentence, no AI trailer:
  - check with `git log -1 --format=%B | grep -iE 'co-authored|anthropic|claude|opus|sonnet'` printing nothing;
  - never push.
- **Todos are ticked as work lands**, and issues go in the phase file's Issues found with evidence (`plans/README.md` rule 4).

---

## Part 3: The phases, and what each leaves behind

| Phase | Leaves behind | Pictures |
|---|---|---|
| 01 Measure where the steps go | His view as a bench row; step histograms by band; the reference mode; the experiment table | Unchanged |
| 02 Compile-time tuning YAML | `compile/config/`, `compile/src/tuning/`, `MC_TUNE_*`, `MC_TUNE_HASH` | Unchanged, to the byte |
| 03 One pyramid of levels | Levels 1 and up for the near region, built on workers from the map's sections | Unchanged |
| 04 Continuous LOD replaces Full detail | Bands by pixel error; no pass-by; Full detail gone | Changed past 16 blocks only |
| 05 Merged boxes | Coalescing, brick distances, uniform water bricks, the coarse pass on the pyramid, each kept or dropped | Unchanged (exact skipping) |
| 06 Secondary rays by level | Shadow cone, dim-light cut, alpha by distance | Changed in shadows and far leaves |
| 07 Temporal upscaling | Internal 75 % with output-resolution accumulation | Converges to 100 % when still |
| 08 The final check | Every target measured, Ahmed's verdict | |

## Ahmed's words, 2026-11-12

- ""Full detail" should never ever be a thing! if there are is a 2x2x1 group of blocks, they should be rendered only as triangle lines of the larger rectangle that it has created as a result of being one shape!!"
- "this should be for all blocks, even blocks that are close to us!! resolution drops from really close to us!! at all times 16 blocks in all directions **VIEWABLE** from the user must be rendered properly! and further optimization as we move away!!"
- "config settings should be read at compile time from a yaml file so that we can very very easily change things up! and fine tune what we need to!!! I think maybe at ./compile/config/<FILEPATH> yaml files!"

## Todo

- [x] Read the renderer, the far field, the world buffers, the settings screen and the compile-time YAML tools, with file and line for each claim
- [x] Read plans 001, 004 (style), 005 phases 8 and 14, and 008, and `research/001.md` to `003`
- [x] Research Unreal, Godot, Minecraft LOD mods and voxel ray casting, with sources
- [x] Write the master `plans/009-ahmed-bohemoth-game-engine-optimizations.md`
- [x] Write phase files 01 to 08, every box unticked, status `not started`
- [x] One commit per file, as Ahmed, no AI trailer, nothing pushed

## Verification

    ls plans/009-*
    grep -L "## Issues found" plans/009-*phase-*.md                   # prints nothing
    git log --format=%B -n 10 | grep -iE 'co-authored|anthropic|claude|opus|sonnet'   # prints nothing
    git status --short                                                # only other agents' work

Nothing heavy runs in this phase, so nothing goes through the mutex.

## Commits

Appended as they land:

- `7073b466` Add the plan 009 master for engine optimizations.

## Issues found

None.
