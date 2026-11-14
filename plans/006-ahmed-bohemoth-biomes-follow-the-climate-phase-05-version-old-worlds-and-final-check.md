# 006-ahmed-bohemoth-biomes-follow-the-climate — phase 5: Version, old worlds and the final check

**Status:** in progress: version 2 in, joint with plan 007 phase 06; the old worlds wait on Ahmed   (updated 2026-11-14)

Read first: `plans/006-ahmed-bohemoth-biomes-follow-the-climate-phase-00-vision-and-plan-files.md`, then the master `plans/006-ahmed-bohemoth-biomes-follow-the-climate.md` (sections 0 "Ahmed's decisions", 3, 4, 5, 7 and 9). Every number here comes from the master. Where this file adds a number the master does not give, it says so and why.

## Goal

`TERRAIN_GENERATOR_VERSION` is 2 (`run/include/terrain/index.h:32`), and every terrain pin has a version-2 row with its reason: `FEA_DIGEST_OF_GENERATOR` (`tests/run/terrain/feature/index.c:49-61`) gains its second entry, and its `static_assert` on the row count holds. A save made by generator 1 is refused with the existing message (`run/src/save/world/index.c:505-555`) and still shows on the world list as made by an older generator. `saves/test_1` to `saves/test_8` are listed with sizes in this file and then deleted, unless Ahmed names one to keep first. The full debug and release suites pass, a release build is made, and the new pins are the same bytes on 1 and 16 workers. The mismatch survey's "after" numbers are written next to plan 004 phase 11's "before" numbers in this file. Ahmed's gate notes are recorded.

## Where this fits

**The vision.** Phases 02 to 04 changed the land every seed makes. A save holds only what the player changed over generated land (`save/world/README.md:54`), so a world saved on the old land, opened on the new, would stitch edits onto a different landscape. The repo's rule is to raise the generator version and refuse such a save rather than migrate it. Ahmed decided, 2026-11-12: "Delete them. The generator version goes to 2."

**What reads it:**

| Reader | What it reads |
|---|---|
| `run/src/save/world/` | `TERRAIN_GENERATOR_VERSION` in each manifest header (`header.generator`, `:505`), compared on open (`:550`) |
| `tests/run/terrain/feature/` | `FEA_DIGEST_OF_GENERATOR[TERRAIN_GENERATOR_VERSION]` against the land |
| `tests/run/save/world/`, `tests/fuzz/save/` | The version in fixtures and fuzz seeds |
| The world list in the menu | "made by an older generator" for version-1 saves |
| Later plans (geology) | The next version bump, which follows the same pattern |

**What this phase must NOT build:**

- No migration of old saves (master section 7).
- No change to plan 004's files: the "after" survey numbers go in **this** file (master section 9).
- No new land change. If a pin moves here that phases 02 to 04 did not record, it is a bug: stop and write it in Issues found.
- No push.

## Ahmed's words

- Decision, 2026-11-12: old worlds (`saves/test_1` to `test_8`): "**Delete them.** The generator version goes to 2".
- Master section 7: "Before deleting, the agent lists them with sizes in the phase file. **Ahmed may keep any he names before then.**"

---

## Background, explained

### Why a version and not a migration

A world save is the deltas over generated land, plus a manifest with the generator number. The comment at `run/include/terrain/index.h:30-31`: "Raised by any change that gives any seed other blocks: a save holds only what changed, so a world saved on other land is refused, and never stitched to a different landscape." **Example:** a player dug a 3-block hole at z 440 on a snowy slope; on the new land the slope there is grass at z 452. Replaying the delta would leave three floating air blocks under 12 blocks of new ground: worse than refusing.

### The digest table and its assert

`FEA_DIGEST_OF_GENERATOR[]` has one row per version, row 0 for "no generator", and `static_assert(sizeof(...)/sizeof(...[0]) == TERRAIN_GENERATOR_VERSION + 1u)`. So bumping the version without a row fails the build, and a row without a bump fails the assert too. Each row carries a comment naming the plan and phase that set it and why. Row 2's comment names plan 006 phases 02 to 04.

### 1 against 16 workers

Every pin is computed twice, with `job_init_count(1)` and with 16 workers, and must be `memcmp`-equal. This catches any order dependence the new climate sample could have brought in (a sample filled from a tile that a different worker built in a different order).

---

## Design

### The pins

| Pin | Where | Version-2 change |
|---|---|---|
| `FEA_DIGEST_OF_GENERATOR` | `tests/run/terrain/feature/index.c:49-61` | New row 2 with its reason |
| Biome, surface, feature digests | `tests/run/terrain/{biome,surface,feature}/` | Already re-pinned in phases 03, 04; their comments now say "version 2" |
| Height, density, hydrology, cave, ore digests | `tests/run/terrain/{density,hydrology,cave,ore}/` | **Unchanged** (master section 3); a change is an Issue |
| Climate digests | `tests/run/climate/*` | Re-pinned in phase 02 for `det_*`; unchanged here |
| Save fixtures | `tests/run/save/world/`, `tests/fuzz/save/` | Whatever names the current version follows it |

### The old worlds

`saves/` is ignored by git (`.gitignore:8`, `/saves/`), so deleting them changes no tracked file. Listed on 2026-11-12 at plan setup (to be listed again, fresh, before deleting):

| World | Bytes (`du -sb`) | Size (`du -sh`) | Last changed |
|---|---|---|---|
| `saves/test_1` | 504 969 | 516K | 2026-11-04 |
| `saves/test_2` | 1 137 265 | 1.2M | 2026-11-04 |
| `saves/test_3` | 231 460 | 244K | 2026-11-05 |
| `saves/test_4` | 233 444 | 252K | 2026-11-06 |
| `saves/test_5` | 247 024 | 264K | 2026-11-06 |
| `saves/test_6` | 1 216 651 | 1.2M | 2026-11-07 |
| `saves/test_7` | 9 593 774 | 9.3M | 2026-11-12 |
| `saves/test_8` | 1 446 580 | 1.5M | 2026-11-09 |

**The order:** list again (sizes and dates) into this file; ask the main thread to put the list in front of Ahmed; wait for his word on any to keep (master Q5, default: delete all); check no game process has a save open (`pgrep -af minecraft_c`; **never kill Ahmed's running game**: if it is running, wait and say so); then `rm -r` exactly the listed directories that are not kept, and record what was deleted.

## Owned paths

This phase may edit these, plus this file:

- `run/include/terrain/index.h`: `TERRAIN_GENERATOR_VERSION` only
- `tests/run/terrain/feature/index.c`: `FEA_DIGEST_OF_GENERATOR` row 2 only
- `tests/run/terrain/*/`: comments on version-2 pins only
- `tests/run/save/world/index.c`, `tests/fuzz/save/index.c`: only where they name the current version
- `saves/test_1` to `saves/test_8` (untracked): delete, as above

**Not touched:** plan 004's files; anything in `run/src/terrain/` beyond the version constant.

## Steps

1. **Run every terrain pin and list what moved since phase 01 began**, against the reasons phases 02 to 04 recorded. *Why: a version bump must not hide an unrecorded change.*
2. **`TERRAIN_GENERATOR_VERSION` 2** and `FEA_DIGEST_OF_GENERATOR` row 2 with its comment. Build: the assert holds. *Why: master section 7.*
3. **Save tests**: a version-1 save is refused with the message at `run/src/save/world/index.c:550-555`; a version-2 save round-trips; the world list marks version 1 as older. *Why: the user-visible side of the bump.*
4. **1 against 16 workers** on every new pin. *Why: master section 4, determinism.*
5. **The survey "after":** `bench_biome_vs_class` over plan 004 phase 11's 8 seeds; write the table below. *Why: master section 9, "in plan 006's phase file (plan 004's files are left as they are)".*
6. **The old worlds**, in the order above.
7. **The full debug and release suites; a release build.**
8. **Ahmed's gate notes**, in his words, in this file. Tick as each lands.

### The survey, before and after

Before is plan 004 phase 11 (its Results, recorded by 2026-11-08, 8 seeds, its narrower desert-only measure, % of land whose biome disagrees). After is filled here, with both measures.

| Class | Before (plan 004 phase 11) | After, plan 004's measure | After, family measure (target < 5%) |
|---|---|---|---|
| Ice | 17.6 | | |
| Tundra | 16.4 | | |
| Desert | 81.0 | | |
| Steppe | 80.9 | | |
| Rainforest | 16.3 | | |
| Monsoon | 16.9 | | |
| Savanna | 16.8 | | |
| Mediterranean | 21.5 | | |
| Temperate | 17.9 | | |
| Continental | 0.0 (no land) | | |
| Boreal | 19.9 | | |

## Todo

From the master (section 9, phase 05):

- [x] `TERRAIN_GENERATOR_VERSION` 2; version-2 rows for every terrain pin, each with its reason
- [ ] List `saves/test_1` to `test_8` with sizes in the phase file, then delete them (unless Ahmed names one to keep)
- [ ] The full debug and release suites; a release build; the 1 against 16 worker check on the new pins
- [ ] Update plan 004's phase 11 table "after" column from the new survey, in plan 006's phase file (plan 004's files are left as they are)
- [ ] Ahmed's gate notes

Expanded:

- [x] Every pin that moved since phase 01 matched to a recorded reason (step 1)
- [x] `FEA_DIGEST_OF_GENERATOR` row 2 at `tests/run/terrain/feature/index.c`; its `static_assert` holds
- [x] A version-1 save refused with the existing message; version 2 round-trips
- [ ] The saves list refreshed on the day of deletion; Ahmed's answer on keeping any recorded; no game running while deleting
- [ ] The survey table above filled
- [ ] `pgrep -af "$PWD/build"` prints nothing

## Tests

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `the_land_is_the_version_two_land` (`run_terrain_feature`) | Digest against `FEA_DIGEST_OF_GENERATOR[2]` | Equal | An unrecorded land change |
| The row-count assert | Build | `rows == TERRAIN_GENERATOR_VERSION + 1` = 3 | Version bumped without a row, or the reverse |
| `a_save_from_generator_one_is_refused` (`run_save_world`) | A fixture manifest with `generator = 1` | Open fails with the "different landscape" message | The comparison at `:550` changed |
| `a_version_two_save_round_trips` | Save, reopen | Same deltas | Header written wrong |
| `every_pin_is_the_same_on_one_and_sixteen_workers` | All terrain pins | `memcmp` equal | Order dependence from the sample |
| `height_density_hydrology_caves_and_ores_did_not_move` | Their pins | Equal to before plan 006 | Master section 3 broken |
| The survey | 8 seeds | Family measure < 5% every class | Phase 03 regression |

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^run_(terrain|save)_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 3600 ctest --preset release --output-on-failure
    tools/test-or-build.sh timeout 7200 build/release/bin/bench_biome_vs_class --seeds 8
    du -sb saves/test_*; ls -ld saves/test_*    # the list, before deleting
    pgrep -af minecraft_c                        # nothing of ours; never kill Ahmed's game
    pgrep -af "$PWD/build"                       # must print nothing

## Gate

Ahmed's own notes, written here in his words: he plays a fresh world, uses `/tp`, `/fly` and F3 from phase 01 to look at the equator, a 45° mountain and the pole, and says whether the biomes and snow follow the climate. He also says which, if any, of `saves/test_1` to `test_8` to keep, before they are deleted.

## Risks and open questions

| Risk or question | Default |
|---|---|
| A pin moves that phases 02 to 04 did not record | Stop; Issue found; find the cause before bumping |
| Ahmed's game is running with one of the test worlds open (`saves/test_7` changed on 2026-11-12) | Do not delete while it runs; never kill it; wait and tell the main thread |
| Ahmed wants to keep a world | It stays; it will be refused on open by generator 2, and shown as older |
| Release `-O3 -flto` moves a bit the debug pins don't | The release suite runs; a difference is an Issue, as plan 001 checked |
| Q5: keep any of `saves/test_1` to `test_8`? | Delete all, as said (master Q5) |

## Commits

Appended as they land:

## Issues found

**Done jointly with plan 007 phase 06's version and final-check part** (2026-11-14, one agent, on main at f5a3f85c), as both masters' default (plan 007 Q2). Open questions took their defaults; plan 007's lapse stays 0.012 (its Q6). The one exception to "take the default" is the old worlds, which wait on Ahmed's word (issue 3).

1. **The pins that moved since phase 01 began (2026-11-12, `01bb5f76`), each to its recorded reason.** No terrain pin moved without one, and the version bump itself moved none: row 2 holds the land's last digest, and every `run_terrain_*` and `run_save_*` test passed on it unchanged (22 of 22, debug). The commits that changed a pinned value in `tests/run/terrain/` or `tests/run/climate/`:

   | Commit | Plan, phase | Pins | Reason recorded in |
   |---|---|---|---|
   | `2bd6e96b`, `7c85777a` | 006-03 | biome, biome columns, feature row 1, forest | phase 03's Issues: biomes from the climate |
   | `ae33abef`, `cdee825d` | 007-03 | cell, systems, hydrology (3), landform; biome, feature (biome height) | plan 007 phase 03's Issues 9 to 11 |
   | `7d301a20`, `ef0821e4` | 006-04 | climate_rules, biome, feature row 1, forest | phase 04's Issues: snow, ice, sand and trees from the climate |
   | `b3baa77b`, `2024cecb`, `677c45e8` | 007-04 | density, cave, ore, hydrology (3), cell, systems; biome, feature after rebasing on 006-04 | plan 007 phase 04's Issue 11 |
   | `40e8e057` | 007-05 | biome columns, systems (basins on their maps) | plan 007 phase 05's Issues |
   | `430cdcc6` | 007-06 | all of the above once more (tuning) | plan 007 phase 06's Issue 8 |

   Plan 006's own phases moved only the biome, biome-columns, feature, forest and climate_rules pins, as master section 3 asks; the density, hydrology, cave and ore pins moved by plan 007's landforms (phases 03 to 06), each with its line. Phase 02 moved none (`fda5f4e2`, `256d313e`: "no terrain digest moved", "no climate digest moved"). The Atlas goldens and the render pictures moved with plan 007 phases 02 to 06, recorded there.

2. **Version 2** (`run/include/terrain/index.h`). `FEA_DIGEST_OF_GENERATOR` row 2 is `0xBF67826538D85EA0`, the land plan 007 phase 06 left, with a comment naming plan 006 phases 02 to 04 and plan 007 phases 03 to 05 with 06's tuning, closed 2026-11-14; the `static_assert` holds at 3 rows (the build passes). Row 1 goes back to `0xCFD3A129B38458E6`, version 1's land as it stood before plan 006 phase 03 moved it, which is what worlds saved by generator 1 were cut from; its comment keeps every value it held, so the history stays. `run_save_world` now names generator 1 (`what_cannot_be_trusted_is_refused_and_left_as_it_was`: generators 0, **1** and 3 each refused with `SAVE_WORLD_ERROR_GENERATOR`, the manifest left as it was, and the list's words checked to be "its land is from another game version"), and holds a new world's manifest to generator **2** (`a_new_world_is_one_manifest_and_its_bytes_are_known`), which `a_world_reloads_byte_for_byte_over_many_saves` reopens four times byte for byte. Both are pinned to the number by a `static_assert(TERRAIN_GENERATOR_VERSION == 2u)`, so the next bump has to look at them. The phase's two named tests were folded into those two, as the file sits at the coding standard's 2000-line limit (exactly 2000). `tests/fuzz/save/` follows the constant. `run/src/terrain/README.md` says what version 2 is.
