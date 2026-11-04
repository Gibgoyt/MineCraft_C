# 003-ahmed-bohemoth-initial-game-fixes — phase 11: Taller world

**Status:** blocked, waiting for Ahmed to confirm the number   (updated 2026-11-04)

## Goal

The world reaches from `z = -250` up to the height Ahmed confirms, with sea level still at `z = 0`.

**Ahmed's words:** "I asked for the world to start at 0 go down to -250 and up to +1250 I think"

## What was asked before

The original request says otherwise: "z co-ordinate at 0 for sea level until -250 and height limit at 500 please!" (`plans/PROMPT.md:70`). So the game was built to 500. **+1250 would be a new decision.** Ahmed said "I think", so this waits for his yes.

## What it would change

| Today | At +1250 |
|---|---|
| `z_max: 500` in `config/machine.yaml` | `z_max: 1250` |
| 751 layers | 1501 layers |
| Storage `-256` to `512`, in `run/include/world/column/index.h` | `-256` to `1280` |
| 48 sections per column | 96 |
| Mountains up to 454 | Up to what phase 8 tunes, possibly about 1100 |

43 source files mention these limits, so it reaches `world`, `render`, `cuda/src/world_buffers/`, `terrain`, saves and tests.

**What it should cost:** air and solid sections store no array, and sky rays leave early by column height, so empty height is cheap. **What's unmeasured:** the per-column tables double, and taller mountains mean more surface to draw and more columns' worth of sections. Frame rate is already below target, and this needs measuring, not assuming.

**Do it before phase 8's mountains** if it's going ahead, so terrain is only tuned once.

## Todo

- [ ] Ahmed confirms the height
- [ ] Change `config/machine.yaml`, and `COLUMN_Z_STORAGE_END` and `COLUMN_SECTIONS` in `run/include/world/column/index.h`, keeping sections aligned, as `static_assert`s there require
- [ ] Follow the static assertions and the compile errors through `run/`, `cuda/`, saves and tests. Update every struct size that changes
- [ ] Check column data, per-thread job buffers and stack frames against the 16 KiB limit, since `borrowed_states` and similar double
- [ ] Bump `SAVE_WORLD_VERSION`, so older worlds are refused clearly
- [ ] Check `run/src/render/lod/` and the coarse pass handle the taller range
- [ ] Add tests that `z = 1250` can be placed, and `z = 1251` can't
- [ ] Measure before and after with `bench_world` at render distance 32: FPS, VRAM and terrain columns a second. Record it here

## Verification

    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 1200 ctest --preset release --output-on-failure
    tools/test-or-build.sh timeout 600 build/release/bin/bench_world
    pgrep -a minecraft_c    # must print nothing

Ahmed: build a tower, or fly with `--test-world`, up to the limit.

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

## Issues found

**Open for Ahmed: is it +1250, or stay at +500?** I recommend +1250 only if you want mountains far taller than Minecraft's, since it costs time on everything that follows. With +500, phase 8 can still reach mountains around 450.
