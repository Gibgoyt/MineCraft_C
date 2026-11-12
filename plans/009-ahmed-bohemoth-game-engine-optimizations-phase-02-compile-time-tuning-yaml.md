# 009-ahmed-bohemoth-game-engine-optimizations — phase 2: Compile-time tuning YAML

**Status:** in progress   (updated 2026-11-12)

Read first: the phase 00 file, then the master: sections 2.7, 4.2 and 6. Then the two tools this one follows:

- `compile/src/machine_config/README.md`: configure-time, four outputs, refusals with `path:line:column`, unchanged output untouched;
- `compile/src/game_tables/README.md` and `cmake/generated.cmake:95-141`: build-time, a custom command over a glob of `index.yaml` files.

Then `compile/src/yaml/README.md`, the parser's subset and `yaml_get_decimal`, and `plans/REPO_STRUCTURE.md` sections 2, 5 and 10.

## Goal

Every number that tunes the renderer's speed or look, and that shapes no data format, lives in a YAML file under `compile/config/`. Each file is turned into a header of `MC_TUNE_*` defines at build time. Editing a number and running one build is all it takes to try a new value. When this phase is done:

- `compile/config/` holds:
  - `README.md`;
  - `render/README.md`;
  - five files: `render/lod/index.yaml`, `render/traverse/index.yaml`, `render/shade/index.yaml`, `render/temporal/index.yaml`, `render/budget/index.yaml`.

  Each key carries `value`, `type`, `unit`, `min`, `max` and `why`.
- `compile/src/tuning/` (a tool, `mc_tool_tuning`) writes `build/<preset>/generated/tuning/<path>/index.h` and `index.hu` for each file, and `generated/tuning/index.h` with `MC_TUNE_HASH`. It refuses a bad file at its line and writes nothing.
- **The constants the master's section 4.2 lists are moved** from `enum`s and `#define`s in code to YAML. Each code site reads `MC_TUNE_*`. Every golden picture and every settled `bench_world` hash is the same bytes as before.
- `--report` prints every tuning value and the hash. `bench/world/history.tsv` gains a `tuning` column.
- `plans/REPO_STRUCTURE.md` and `plans/CODING_STANDARD.md` say where tuning numbers go, and the rule for what counts as one.

## Where this fits

**The vision.** Ahmed: "config settings should be read at compile time from a yaml file so that we can very very easily change things up! and fine tune what we need to!!!" Phases 03 to 07 each add numbers (pixel error, the near guarantee, the shadow cone, the dim-light cut, the upscale jitter). They must land in YAML from their first commit, so the file format exists first.

**What reads it:** every later phase; `bench_world` (the hash per row); Ahmed, tuning E and the internal scale by eye.

**What this phase must NOT build:**

- No new tuning number with a new behaviour. Moving is all. A moved number keeps its value to the bit: `F32(1.25)` becomes `1.25F` from the YAML's exact decimal.
- No runtime override, environment variable or settings key (Q11: compile time, as asked).
- No change to `config/machine.yaml` or `compile/src/machine_config/` (Q3: leave the game keys where they are).
- No number that shapes a format or a struct: `TRAVERSE_TILE_PIXELS`, `WORLD_BUFFERS_*`, `LOD_LEVELS_MAX`, `TRAVERSE_COUNT_*`, array sizes in structs. Those stay in code with their static_asserts.

## Ahmed's words

- "config settings should be read at compile time from a yaml file so that we can very very easily change things up! and fine tune what we need to!!! I think maybe at ./compile/config/<FILEPATH> yaml files! this way in the future we can very easily change numbers to get the sweet spot of exactly what we need!!"

---

## Background, explained

### Two kinds of YAML this repository already compiles

| | `config/machine.yaml` | `config/<kind>/<name>/index.yaml` |
|---|---|---|
| Tool | `compile/src/machine_config/` | `compile/src/game_tables/` |
| When | **Configure** (`cmake/machine.cmake` compiles and runs it), because `-march` and the CUDA architecture come from it | **Build** (`cmake/generated.cmake`, a custom command) |
| Output | `generated/machine/index.{h,hu,inc,cmake}`, `MC_*` defines | `generated/tables/<kind>/index.{h,c}`, C tables |
| Schema | A table in the tool (`machine_keys`) | Per kind, in the tool |

Tuning numbers are like the second in timing, since nothing in CMake needs them, and like the first in shape: defines, not tables. The new tool takes one more step. **The schema is in the YAML itself** (each key's type, unit and range), so a new number needs no edit to the tool.

### Why defines, at compile time, and not a file read at start

- A define is a constant to nvcc. A loop bound like `MC_TUNE_RENDER_SHADE_ALPHA_LAYERS` unrolls, and a `0` feature compiles away. A value read at start is a register and a branch in every pixel's loop.
- Ahmed asked for compile time.
- The cost is a rebuild per change. Only files that include the changed file's header rebuild, which is why there is a header per YAML file and not one header for all.

### How a build re-runs only what changed

The custom command's output is a stamp file, and the headers are its `BYPRODUCTS`. The tool rewrites a header only if its bytes change, and Ninja's `restat` then sees the untouched headers as unchanged, so their dependents do not rebuild. `game_tables` touches every output instead (`cmake/generated.cmake:124-128`), which rebuilds everything that includes any table; tuning must not do that, as a CUDA rebuild is minutes.

The list of input files goes in `generated/tuning/inputs.txt`, written by `file(CONFIGURE)` as `game_tables` does, so a file added or removed re-runs the tool.

### `compile/config/` and the repository's rules

`plans/REPO_STRUCTURE.md` section 2 says `compile/` holds C tools. A directory under it holding only YAML and a README has no code, so the lint's module rules do not apply to it: `tree_check_directory` in `compile/src/standards_lint/rules_tree/index.c` returns when `code_count == 0`. And `tree_module_roots` treats only `src/` and `include/` under `compile` as module parts. So Ahmed's path needs no lint change, only a REPO_STRUCTURE row and a README in each directory, as `config/` has. If Ahmed answers Q1 with B, the same files go to `config/tuning/` and nothing else here changes.

---

## Design

### A file

```yaml
# compile/config/render/shade/index.yaml
# Numbers that tune shade's speed and look. See compile/config/README.md for the fields.
alpha_layers:
  value: 16
  type: u32
  unit: count
  min: 1
  max: 64
  why: "Surfaces a primary ray may go through by holes in leaves and the like before it takes the mean colour."
ao_reach:
  value: 3.0
  type: f32
  unit: blocks
  min: 0.5
  max: 16.0
  why: "How far an AO ray looks."
```

| Field | Rule |
|---|---|
| key | Lower snake case, unique in the file |
| `value` | Of `type`; within `min`..`max` |
| `type` | `u32`, `i32`, `f32` or `bool` |
| `unit` | One of: `blocks`, `pixels`, `chunks`, `voxels`, `levels`, `bricks`, `frames`, `count`, `steps`, `ratio`, `degrees`, `ms`, `bytes`, `kib`, `mib` |
| `min`, `max` | Numbers of the same type; absent for `bool` |
| `why` | Non-empty text; what it does, and who set it if it was a decision |

### The tool: `compile/src/tuning/`

- **Shape:** `index.c` with `int32_t main(`, so the build makes it `mc_tool_tuning` (REPO_STRUCTURE section 10). Its README carries the eight fields. `compile/include/tuning/index.h` holds the exit statuses: 0, 1 refused, 2 could not run.
- **Command line:** `tuning --config compile/config --out <generated>/tuning --stamp <file>`.
- **Walk:** finds every `index.yaml` below `--config` in sorted order, so output never depends on directory order.
- **Per file**, it writes:

  ```c
  /* Generated from compile/config/render/shade/index.yaml by compile/src/tuning. Do not edit. */
  #ifndef MC_TUNE_RENDER_SHADE_INDEX_H
  #define MC_TUNE_RENDER_SHADE_INDEX_H
  #define MC_TUNE_RENDER_SHADE_ALPHA_LAYERS 16u /* count, 1..64 */
  #define MC_TUNE_RENDER_SHADE_AO_REACH 3.0F /* blocks, 0.5..16.0 */
  #endif
  ```

  `index.hu` holds the same text, for CUDA-only code, as `machine_config` writes both.
- **The hash:** `generated/tuning/index.h` holds `MC_TUNE_HASH`, FNV-1a 64 over `path key value` lines in sorted order, and `MC_TUNE_TEXT`, a string of every line, for `--report`.
- **Exact numbers:** an `f32` comes out as the YAML's decimal text with `F`, via `yaml_get_decimal`'s exact mantissa and exponent, so 0.1 stays the literal `0.1F` and gives the same float the code had.
- **Errors:** `path:line:column: message`, every error in the run, then exit 1 with nothing written.

### CMake

`cmake/generated.cmake` gains a block:

- a glob of `compile/config/*/index.yaml`, `*/*/index.yaml` and `*/*/*/index.yaml` with `CONFIGURE_DEPENDS`;
- `inputs.txt`;
- a custom command whose output is the stamp, with the headers as BYPRODUCTS;
- a target `mc_tuning`, added to `mc_prebuild`'s dependencies so every compile sees the headers.

`build/<preset>/generated` is already on every include path, so code includes `"tuning/render/shade/index.h"`.

### What moves, and where

| From | Key | Default (unchanged) |
|---|---|---|
| `run/include/render/lod/index.h:19-36` | `render/lod`: `radius_columns`, `margin_columns`, `features_shift_max`; `render/budget`: `lod_jobs`, `lod_pool_kib_per_column`, `lod_frame_bytes` | 16, 2, 2; 48, 64, upload budget |
| `run/include/gpu_bridge/lod/index.h:29` | `render/lod`: `skips_max` | 32 |
| `run/include/gpu_bridge/shade/index.h:35-37, 90-105, 140` | `render/shade`: `alpha_layers`, `sun_layers`, `ao_turns`, `ao_fresh_rays`, `ao_samples_max`, `ao_reach`, `shaft_steps`, `water_seen_blocks` | 16, 8, 4, 4, 32, 3.0, 6, 48.0 |
| `cuda/src/traverse/index.cu:13-21` | `render/traverse`: `coarse_long_stretch`, `coarse_short_stretch`, `coarse_long_per_block`, `coarse_pad`, `coarse_pad_per_block`, `coarse_back_off` | 16.0, 4.0, 0.25, 0.0625, 4e-6 written as 0.000004, 0.25 |
| `run/include/gpu_bridge/post/index.h:128-132` | `render/temporal`: `taa_blend`, `taa_clip_sigma`, `taa_still` | 0.1, 1.25, 0.002 |
| `run/include/render/upload/index.h:18` | `render/budget`: `upload_bytes_per_frame` | 4194304 |

**Some numbers size arrays.** `SHADE_AO_SAMPLES_MAX` packs into a field, and `SHADE_AO_STEPS` derives from `SHADE_AO_REACH`. Each such use keeps its own named constant in code, defined from the `MC_TUNE_*` one, with a `static_assert` that the field still holds it, so a YAML change that would overflow fails the build at the line that would break. Step 1 lists every use before moving anything.

### `--report` and the bench

- **`run/src/main/`'s `--report`** prints `tuning hash 0x...` and every line of `MC_TUNE_TEXT`. F3 shows nothing new: it is full.
- **`bench_world`'s history gains a `tuning` column** at the end of its first line (its README's rule for new columns). Old lines stay.

### Documents

- **`plans/REPO_STRUCTURE.md`:**
  - section 2 gains `compile/config/`: "tuning YAML, read by `compile/src/tuning/` at build time";
  - section 5 gains the `tuning` tool;
  - section 4 gains a "Where a number goes" line: "facts about the machine: `config/machine.yaml`; what the game is: `config/<kind>/`; how fast or how fine it draws: `compile/config/`".
- **`plans/CODING_STANDARD.md`** gains one rule: *a number that changes speed or look and shapes no format or struct is a tuning YAML key; a new magic number of that kind in code is a review failure.*

## Owned paths

| Path | Change |
|---|---|
| `compile/config/**` | New: READMEs and five YAML files |
| `compile/src/tuning/`, `compile/include/tuning/` | New tool |
| `cmake/generated.cmake` | The tuning block |
| `tests/compile/tuning/` | New ctest `compile_tuning`, with fixtures under `fixtures/` |
| The files in "What moves" | `MC_TUNE_*` in place of literals |
| `run/src/main/` | `--report` lines |
| `bench/world/index.c`, `bench/world/README.md` | `tuning` column |
| `plans/REPO_STRUCTURE.md`, `plans/CODING_STANDARD.md` | Rows and the rule |

## Steps

1. **List every use of each constant to move**, with `grep -rn`, into this file's Issues found as a table, marking those that size an array or a field. *Why:* a moved constant that sizes a field must keep a static_assert.
2. **Write the tool and its test first, with fixtures only.** *Why:* the format is settled before any real number depends on it.
3. **Wire CMake; prove an unchanged build rebuilds nothing.** Two builds in a row; the second compiles nothing (`ninja -n` lists nothing after it). Then edit one value in one file: only its dependents rebuild, listed by `ninja -d explain`. *Why:* Ahmed's "very very easily" is the rebuild time.
4. **Move the constants, one YAML file per commit.** After each, build and run the golden tests. *Why:* a picture that moves is found at the file that moved it.
5. **`--report`, the history column, the documents.**
6. **Full `bench_world` run;** every settled hash the same as the last history line's.

## Todo

- [x] Uses of every constant to move listed in Issues found, array and field sizes marked
- [x] `compile/src/tuning/` and `compile/include/tuning/`, README with the eight fields
- [x] `tests/compile/tuning/` (ctest `compile_tuning`): every refusal at its line, exact decimals, unchanged files untouched, the hash, sorted order
- [x] `cmake/generated.cmake`: glob, `inputs.txt`, stamp with BYPRODUCTS, `mc_tuning` before every compile
- [ ] A no-change build compiles nothing; a one-value change rebuilds only dependents (recorded)
- [x] `compile/config/README.md` (schema, units, workflow, what counts as tuning) and `compile/config/render/README.md`
- [ ] `render/lod`, `render/traverse`, `render/shade`, `render/temporal`, `render/budget` files, each moved in its own commit with golden tests passing
- [ ] Field-sizing uses keep static_asserts against the tuned value
- [ ] `--report` prints the hash and every value; `history.tsv` has a `tuning` column
- [ ] REPO_STRUCTURE (sections 2, 4, 5) and CODING_STANDARD rule
- [ ] Full `bench_world`: every settled hash unchanged
- [ ] Ahmed changes `ao_turns` from 4 to 2, rebuilds, and sees it in `--report`; then back

## Tests

### `tests/compile/tuning/` — ctest `compile_tuning`, no card

- `a_good_file_gives_the_defines_it_says`: a fixture with one key of each type; the header's text is compared byte for byte with the expected text.
- `a_decimal_comes_out_as_written`: `0.1`, `1.25`, `0.000004`, `16.0` come out as `0.1F`, `1.25F`, `0.000004F`, `16.0F`.
- `each_mistake_is_refused_at_its_line`, one fixture each, with line and column checked:
  - an unknown field;
  - a missing `why`;
  - a value over `max`;
  - an `f32` written `2`;
  - a `u32` written `-1`;
  - `"true"` in quotes for a bool;
  - an unknown unit;
  - a key in camel case;
  - two files making the same define;
  - a tab in the indentation.
- `a_refused_run_writes_nothing`: the output directory is unchanged after a refusal.
- `an_unchanged_file_is_not_rewritten`: run twice; the second leaves the header's mtime alone.
- `the_hash_moves_with_one_value_and_not_with_order`: change one value, the hash changes; reorder keys, it does not.
- `every_define_is_used`: over the real `compile/config/`, each define is found in `run/`, `cuda/` or `bench/` (a grep in the test).
- `the_real_files_are_accepted`: the committed `compile/config/` passes.

### Existing tests, unchanged in result

`render_feature_*`, `run_render_frame`, `cuda_traverse`: every golden picture the same bytes. `run_main` (or wherever `--report` is tested) gains the tuning lines.

## Verification

    tools/test-or-build.sh cmake --preset debug
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(compile_tuning|compile_yaml|compile_machine_config)$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(render_feature_.*|cuda_traverse|run_render_frame|run_render_lod)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --record bench/world/history.tsv --label "009 phase 02: tuning YAML, no change"
    tools/test-or-build.sh cmake --build --preset release
    pgrep -af "$PWD/build"                                                       # must print nothing

## Gate

Ahmed opens `compile/config/render/shade/index.yaml` and changes `ao_turns` from 4 to 2. Then:

    tools/test-or-build.sh cmake --build --preset release
    ./build/release/bin/minecraft_c --report | grep -i tuning

He checks that the new value and a new hash are there. He plays a minute, and puts it back. He says whether the files read clearly and whether the rebuild was quick enough.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Q1: `compile/config/` or `config/tuning/` | `compile/config/` |
| A moved literal changes a float's bits | Exact decimals; golden pictures to the byte |
| A field-sized constant overflows after a YAML change | static_assert at the field |
| Ninja's restat does not stop rebuilds with nvcc's dependency files | Measured in step 3; if it fails, write each header to a temporary file and copy only on change, as `machine_config` does |
| A tuning header included in a widely shared header rebuilds the world | Each header included only where used; step 3 lists dependents |
| Someone adds a magic number later anyway | The CODING_STANDARD rule; review |

## Commits

Appended as they land:

- `a92c08d7` Add the tuning tool, which turns compile/config/<path>/index.yaml into MC_TUNE_* headers at build time, with its test and its CMake step.
- `c1178a41` Write the tuning hash to generated/tuning/hash/index.h, as tuning/index.h is the tool's own header's name and the lint and the compile tests would find that one.

## Issues found

### 1. Every use of each constant to move (step 1)

`grep -rnw` over `run cuda bench tests compile asm`, on main at `ec4554c9`. **Field** marks a use that sizes a field or an array, or that a
derived constant depends on; each of those keeps a `static_assert` against the tuned value.

| Constant | Defined | Read by | Field or derived |
|---|---|---|---|
| `RENDER_LOD_RADIUS_COLUMNS` 16 | `run/include/render/lod/index.h:23` | `run/src/render/lod/index.c:70,75` | **Field**: stored in `render_lod_plan_t.radii[]` and `render_lod_level_t.radius`, both `uint8_t`; and must exceed the margin (existing assert) |
| `RENDER_LOD_MARGIN_COLUMNS` 2 | `render/lod/index.h:25` | `run/src/render/lod/index.c:58,74,581`; `tests/cuda/lod/index.c:313,314,333,506,561,1417` | Below the radius (existing assert) |
| `RENDER_LOD_FEATURES_SHIFT_MAX` 2 | `render/lod/index.h:34` | `run/src/render/lod/index.c:194`; `bench/lod/index.c:45`; `tests/run/render/lod/index.c:44,60` | No |
| `RENDER_LOD_JOBS` 48 | `render/lod/index.h:27` | `run/src/render/lod/index.c:100,314-315,375,413,437,533,675,767,770`; `tests/cuda/lod/index.c:593` | Sizes an arena array (`borrowed_jobs`, worked out at run time from it), not a struct |
| `RENDER_LOD_POOL_BYTES_PER_COLUMN` 64 KiB | `render/lod/index.h:39` | `run/src/render/lod/index.c:94` | No (a pool size worked out at run time) |
| `RENDER_LOD_FRAME_BYTES` = upload budget | `render/lod/index.h:41` | `run/src/render/lod/index.c:686` | No |
| `LOD_SKIPS_MAX` 32 | `run/include/gpu_bridge/lod/index.h:29` | `cuda/src/lod/index.cu:124` (and a comment at 235) | No |
| `SHADE_ALPHA_LAYERS` 16 | `run/include/gpu_bridge/shade/index.h:35` | `cuda/src/shade/index.cu:857`; `cuda/src/lod/index.cu:250`; `tests/cuda/sky/index.c:497,514`; `tests/cuda/water/index.c:537`; `tests/cuda/shapes/index.c:1029,1101,1396` | No: loop bounds only |
| `SHADE_SUN_LAYERS` 8 | `gpu_bridge/shade/index.h:37` | `cuda/src/shade/index.cu:46`; `tests/cuda/sky/index.c:456`; `tests/cuda/water/index.c:382` | No |
| `SHADE_AO_TURNS` 4 | `gpu_bridge/shade/index.h:90` | `gpu_bridge/shade/index.h:318` (`shade_ao_turn`); `cuda/src/shade/index.cu:260,605`; `tests/cuda/post/index.c:41,209,216,223,470` | **Derived**: `shade_ao_turn` maps `frame % SHADE_AO_TURNS` onto the four places of a 2 x 2 group of warps by a fixed table of 4, so the value can only be 1 to 4 (YAML range 1..4) |
| `SHADE_AO_FRESH_RAYS` 4 | `gpu_bridge/shade/index.h:92` | `cuda/src/shade/index.cu:607,613,621`; `tests/cuda/post/index.c:464,507-508,524-525,1333` | **Field**: written into `shade_ao_pixel_t.samples`, a `uint16_t` (`index.cu:621`) |
| `SHADE_AO_SAMPLES_MAX` 32 | `gpu_bridge/shade/index.h:94` | `cuda/src/shade/index.cu:264`; `tests/cuda/post/index.c:41` | **Field**: `shade_ao_pixel_t.samples`, `uint16_t` |
| `SHADE_AO_REACH` 3.0 | `gpu_bridge/shade/index.h:105` | `gpu_bridge/shade/index.h:356` (`shade_ao_near`); `cuda/src/shade/index.cu:142,157`; `tests/cuda/post/index.c:202-204,410` | **Derived**: `SHADE_AO_STEPS` 12 must cover reach x sqrt(3) + 3 planes |
| `SHADE_SHAFT_STEPS` 6 | `gpu_bridge/shade/index.h:101` | `cuda/src/shade/index.cu:1029,1031`; `tests/cuda/post/index.c:271-272,1080-1082` | No |
| `SHADE_WATER_SEEN_BLOCKS` 48.0 | `gpu_bridge/shade/index.h:140` | `cuda/src/shade/index.cu:65,699,949`; `cuda/src/lod/index.cu:272`; `tests/cuda/water/index.c:84,392,603` | No |
| `TRAVERSE_COARSE_LONG_STRETCH` 16.0, `_SHORT_STRETCH` 4.0, `_LONG_PER_BLOCK` 0.25, `_PAD` 0.0625, `_PAD_PER_BLOCK` 4e-6, `_BACK_OFF` 0.25 | `cuda/src/traverse/index.cu:13-21` | the same file, `:125,178,189-199,204` only | No |
| `POST_TAA_BLEND` 0.1, `POST_TAA_CLIP` 1.25, `POST_TAA_STILL` 0.002 | `run/include/gpu_bridge/post/index.h:128-132` | `cuda/src/post/index.cu:94,159,161` | No |
| `RENDER_UPLOAD_BUDGET_BYTES_DEFAULT` 4 MiB | `run/include/render/upload/index.h:18` | `run/src/game/index.c:430`; `render/lod/index.h:41`; `tests/run/render/frame/index.c:376`; `tests/cuda/sky/fixtures/rig.h:65`; `tests/cuda/shapes/index.c:110` | No |

None of them sizes an array inside a struct. Every constant keeps its name in code, defined from its `MC_TUNE_*` define, so no
use site changes (see issue 2).
