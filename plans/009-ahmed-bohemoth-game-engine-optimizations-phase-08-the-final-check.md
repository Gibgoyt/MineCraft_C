# 009-ahmed-bohemoth-game-engine-optimizations — phase 8: The final check

**Status:** not started   (updated 2026-11-12)

Read first: the phase 00 file, then the master: sections 7 (targets), 8 (tests), 9 (risks) and 12 (open questions). Then every earlier phase file's Results, Gate answers and Issues found, and `bench/world/README.md`, "Reading these numbers".

## Goal

Every target in the master's section 7 is measured like with like against phase 01's "Today", on Ahmed's card as it is, with its clock beside every number. Every suite passes. Ahmed has played and given his verdict. When this phase is done:

- **The targets table below is filled:** each row has today's value, the final value, the card's MHz, and met or missed, said plainly.
- **The full debug, sanitize and release suites pass,** with every heavy command through the mutex.
- **`bench/world/history.tsv`** has a final run at render distance 32 and at 64, labelled with the commit and the `MC_TUNE_HASH`.
- **The tuning YAML holds the values Ahmed chose** (E from phase 04, the internal scale from phase 07), committed.
- **Every README and plan document the plan touched says what the code now does:**
  - REPO_STRUCTURE: `compile/config/`, `compile/src/tuning/`, `run/src/render/lod/octant/`;
  - CODING_STANDARD: the tuning rule;
  - the renderer's READMEs: no "full detail";
  - the master's open questions: each answered or still open, with its default.
- **Ahmed's verdict** at his spot is recorded.

## Where this fits

**The vision.** Ahmed's words, all of them, checked:

| He said | Checked by |
|---|---|
| "Full detail" should never ever be a thing | `grep -rni "full.detail" run cuda compile bench` finds nothing; the Settings screen has five sliders |
| a 2x2x1 group … rendered … as the larger rectangle … one shape | The pyramid's reduction tests (phase 03); merged boxes' step counts (phase 05) |
| resolution drops from really close … further optimization as we move away | The bands at the chosen E; step bands at his spot |
| at all times 16 blocks in all directions VIEWABLE … rendered properly | The near-guarantee test, run once more here on every place, at every E he tried |
| research … unreal engine and godot | Master section 3 and its sources |
| config settings … read at compile time from a yaml file … ./compile/config/ | `compile/config/`, one edit plus one build (phase 02's gate, repeated) |

**What this phase must NOT build:** nothing new. A fix found here is a small commit named in Issues found. A larger one goes back to its phase, reopened.

## Ahmed's words

- "this way in the future we can very easily change numbers to get the sweet spot of exactly what we need!!"

---

## Targets (filled in by this phase)

| Measure | Today (phase 01) | Final | MHz | Target | Met? |
|---|---|---|---|---|---|
| Card time at his spot, capped, p99 over 240 settled frames | | | | ≤ 16.7 ms | |
| The same, mean | | | | ≤ 14 ms | |
| Stretch: cool card, if measured | | | | ≤ 8.33 ms | recorded |
| Near guarantee | n/a | | | byte-identical | |
| Error share past 16 blocks at the chosen E | n/a | | | ≤ 1 % | |
| Mean primary steps a pixel at his spot | | | | −40 % | |
| Shadow-ray steps at his spot | | | | −50 % | |
| Water walk on `deep_ocean/orbit` at 64 | | | | −50 % | |
| `bench_world` rows at 32 | `history.tsv` | | | none > 3 % slower | |
| Settings screen | 6 sliders | | | 5, no "Full detail" | |
| VRAM at 64 | | | | ≤ today | |
| Fill-in at 64 | 7.66 s | | | no worse | |
| Edit to every level | up to 1 s | | | ≤ 100 ms | |
| Tuning edit to new numbers | n/a | | | only dependents rebuild | |

## Todo

- [ ] Ahmed's game is closed before any card work (`pgrep -af 'build/release/bin/minecraft_c'` prints nothing), or he is asked and waited for
- [ ] Debug suite, sanitize suite, release build: all pass
- [ ] `bench_world` full run at 32 and at `--render-distance 64`, recorded with the commit and `MC_TUNE_HASH`
- [ ] `ahmed_slope/night-64` and `day-64` with `--steps`; the targets table filled, each row met or missed said plainly
- [ ] Near-guarantee test on every place at the chosen E, FOV 70 and 30, render scale 100 and 75
- [ ] `grep -rni "full.detail" run cuda compile bench` finds nothing
- [ ] REPO_STRUCTURE, CODING_STANDARD and every touched README read against the code
- [ ] The master's open questions: each answered, or left with its default, listed in this file
- [ ] Chosen E and internal scale committed in `compile/config/`
- [ ] Ahmed plays his slope at night and by day, at render distance 64, and gives his verdict
- [ ] Nothing left running: `pgrep -af "$PWD/build"` prints nothing

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                                    # must print nothing
    tools/test-or-build.sh cmake --preset debug
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1800 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --preset sanitize
    tools/test-or-build.sh cmake --build --preset sanitize
    tools/test-or-build.sh timeout 3600 ctest --preset sanitize --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --record bench/world/history.tsv --label "009 final: rows at 32"
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --render-distance 64 --record bench/world/history.tsv --label "009 final: rows at 64"
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --only =ahmed_slope/night-64 --steps /tmp/mc_c_009/final/night.tsv --shots /tmp/mc_c_009/final
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --only =ahmed_slope/day-64 --steps /tmp/mc_c_009/final/day.tsv --shots /tmp/mc_c_009/final
    tools/test-or-build.sh cmake --build --preset release
    pgrep -af "$PWD/build"                                                       # must print nothing

## Gate

Ahmed runs, himself:

    ./build/release/bin/minecraft_c --menu

He stands where 5.png was taken, at night, render distance 64, F3 up. He looks up the slope, turns, walks, mines a block 20 blocks away and one far off, and looks again by day. He says:

1. What F3's FPS and gpu total read, against 31.9 and 29.44 ms.
2. Whether anything within 16 blocks looks different from before. It must not.
3. Whether the far slope, the ridge and the shadows look right.
4. Whether changing a number in `compile/config/` and rebuilding was as easy as he wanted.
5. His verdict on plan 009.

His answers are recorded below.

## Risks and open questions

| Risk or question | Default |
|---|---|
| The card's clock differs between phase 01 and now | MHz beside every number; rows repeated; like with like only |
| A target is missed | Said plainly; the next lever named (E, internal scale, a phase 05 experiment revisited, or plan 008's work) |
| Plans 006 and 008 landed changes that move numbers | Phase 01's rows re-run on today's `main` with 009's tuning set to "off" where possible, and the comparison stated |

## Commits

Appended as they land:

## Issues found

None.
