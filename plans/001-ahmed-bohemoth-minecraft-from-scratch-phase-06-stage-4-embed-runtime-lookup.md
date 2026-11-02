# 001-ahmed-bohemoth-minecraft-from-scratch — phase 6, stage 4: Embedding and runtime lookup

**Status:** done   (updated 2026-11-02)

## Goal

Mip chains exist, the blob is embedded through `incbin`, and runtime lookup works.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-textures.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/texture/index.*` and `README.md` (the Owner tier)
- `asm/{src,include}/embed_assets/`
- `run/{src,include}/texture/*`
- `tests/run/texture/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create the `texture` Owner tier: `run/src/texture/index.c` with paired `texture_init` and `texture_deinit`, owning the regions and arenas its parts borrow
- [x] Generate mip chains and the texture blob in `build/generated/` with `add_custom_command`. In `cmake/generated.cmake`, by a new tool: see issue 2
- [x] Embed the blob with `incbin` in `asm/src/embed_assets/index.asm`
- [x] Implement runtime lookup under `run/src/texture/`: parts `blob` and `registry`
- [x] Register test `run_texture`. Also `compile_texture_blob`, for the new tool
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_texture$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

All run on 2026-11-02, and all passed. `pgrep` printed nothing. Issue 12 lists what else was run.

## Commits

Appended as they land:

## Issues found

All 2026-11-02, by this stage.

1. **The integration branch did not compile when I merged it.** Expected: `7e39d5a` builds. Actual: `run/include/display/headless/index.h:38: static assertion failed: sizeof(display_headless_t) == 288`, and `display_offscreen_t` then needed 56 bytes of padding. Cause: merge `183a1b1` grew `display_buffer_t` by 8 bytes and merge `c7411db` kept the older asserts. Done, outside my paths because nothing builds otherwise: `display_headless_t` 296, `display_offscreen_t` 448 with `_pad0[71]`. No field changed. Stage 06.3 passed on its own branch, before those merges.
2. **A new tool, which no owned path covers.** The todo wants mip chains generated, and that must run at build time, so it must be in `compile/`, which this stage does not own. I added `compile/{src,include}/texture_blob/` and `tests/compile/texture_blob/`, rather than a fourth command in Stage 06.3's `texture_vector`, so as to leave that stage's files alone.
3. **Where the format lives.** Expected a header in `run/include/texture/`. Actual: `compile/` may include from `run/` only under `types/` (`include_run_from_compile` in the lint, and `plans/REPO_STRUCTURE.md` section 11), and two copies could drift. So it is `run/include/types/texture_blob/index.h`, outside the owned paths. `run/` keeps its own FNV-1a, since it may not include `mctex_hash`; `hash_is_fnv1a_64` holds it to the published vectors, and any disagreement fails `texture_init`.
4. **Where the C declarations of the embedded data live.** `run/include/simd/embed_assets/index.h`, outside the owned paths, because `plans/REPO_STRUCTURE.md` section 8 and `run/src/simd/README.md` put what `asm/` exports there, one directory per file in `asm/src/`. They hold no kernels, so nothing in `tests/asm/` or `simd_self_check` covers them; `run_texture` does.
5. **My first dependency on the blob was refused by the lint, and was not needed.** I wrote `set_source_files_properties(... OBJECT_DEPENDS ...)` on the belief that NASM reports no dependency on what it `incbin`s. Lint: `cmake/generated.cmake:25: [cmake_flags_outside]`. That rule is aimed at flags, but the belief was wrong too: CMake's rule passes `-MD`, and the depfile lists the blob. Removed. Tested: `touch textures/blocks/dirt/index.mctex`, then a build, ran `Generating the texture blob`, `Building ASM_NASM ... embed_assets`, and `Linking ... minecraft_c`. A second build did neither.
6. **My first rule for cutout mips looked fine in tests and was wrong on real data.** It chose one alpha threshold per level to keep level 0's share of solid texels. Reading the chain back, `blocks/glass` went 44%, 25%, 0%, 100%, 0% down its five levels, because its level 2 and 3 texels all average to one alpha, and a threshold takes all or none. Glass would have blinked between gone and solid with distance. Now each level gets exactly the share, highest alpha first and ties in Bayer order: glass is 44%, 44%, 44%, 50%, 0%. `run_texture` holds all 303 cutout textures to it, every level of every frame, to the texel. **What this does not give:** the holes at far levels are a pattern, not the true shape, as the sheet shows. Whether that looks right is for Ahmed, once there is a renderer.
7. **Far thin things vanish, on purpose.** A share rounding to no texels gives none: `blocks/torch_floor`'s 1 by 1 level is empty, and so is glass's. The other choice was a solid cube of the mean colour hanging where a torch is. The renderer may want to keep such a block's level above its last: not decided here.
8. **Vector data has no mips.** The todo says "mip chains", and the plan's shading uses a mip chosen by distance only for "texture lookup". Colour has a chain, and height, normal and emissive are level 0 only. A later stage that wants them at distance has to add them, and bump `TEXTURE_BLOB_VERSION`.
9. **Stage 06.3's alpha warning is carried over, by fixing it here.** Its issue 5 said a cutout test must use a threshold, not `alpha == 0`, since leaves are 5 where absent and the lit furnace has 252. The blob now promises otherwise: cutout is exactly 0 or 255 and opaque is exactly 255, at every level. Level 0's RGB is still byte for byte what `.mctex` holds, and `visible_pixels` still counts the raw alpha, as `.mcvec` does.
10. **A test expectation of mine was wrong, and the data right.** `an_animation_has_frames_that_differ` said frame 0 and frame 1 differ. `water_flow` and `lava_flow` have 16 frames with 8 distinct, each held twice, in the pack. The test now asks that some frame differs.
11. **Tests checked by breaking the code on purpose.** A one-off script, not committed, made 24 one-line changes and restored each. 22 are caught. On the way:
    - Three changes to the tool looked like survivors, because the script rebuilt the test and not the tool the test runs. Rebuilt, the two that compile are caught.
    - `no hole colour` did not compile at first, as an unused parameter is an error. Rewritten, it is caught.
    - One gap was real: taking one texel too many of a tie survived. I added `cutout_takes_no_more_of_a_tie_than_it_wants`, and it is caught.
    - **Not caught:** removing `entry->name_length == length` from the lookup. The 64-bit hash and `memcmp` still decide, so only a hash collision could show it. It stays as a cheap guard.
    - **Not run:** removing the `qsort` in the tool does not compile, as `tb_compare_text` is then unused. The parser refuses names out of order, and `the_tool_lays_a_blob_out_as_the_format_says` makes them in another order, but that pairing was not shown to catch it.
12. **Beyond the Verification list, also run.** All of `ctest --preset debug`, 42 of 42. `run_texture` and `compile_texture_blob` in `sanitize` and `release`. The blob is byte-identical in `debug`, `sanitize` and `release`, by `cmp`. A headless run, `minecraft_c --headless --ticks 2 --width 64 --height 36`, logged `texture: 511 textures, 3459932 bytes embedded`, exit 0, nothing live.
13. **What it costs.** 3459932 bytes, in every program that links `mc_asm`, tests too. 1725624 are pixels with mips, against 1291660 without. Startup hashes all of it once. That was not timed alone: release's whole `run_texture`, with more than 50 such passes, takes 0.31 s.
14. **Edits outside my owned paths, each the smallest I could make:**
    - `run/include/main/index.h`, `run/src/main/index.c` and its README: `texture_t` in `app_t`, 2240 bytes now with issue 1, and `texture_init` between `simd` and `job`. Only `--headless` and the default run start it, as with the other systems.
    - `tests/run/types/index.c`: the game now holds 3 heap regions and not 2, in two expected strings.
    - `cmake/targets.cmake`: one line, so `mc_asm` waits for the blob. `cmake/generated.cmake` is new, the file `CMakeLists.txt` already includes if present. Stage 07.1 will add to it.
    - `plans/REPO_STRUCTURE.md`, `textures/README.md`, `run/include/types/README.md`: what was added.
    - `run/include/display/`: issue 1.
15. **Process.** The worktree isolation check refused a few compound shell commands as too complex to verify. I re-ran them as plain separate commands, and did the file edits with the edit tools or a Python script in the scratchpad. They touched only this worktree. All GPU and display code was left alone: only headless mode was run.
