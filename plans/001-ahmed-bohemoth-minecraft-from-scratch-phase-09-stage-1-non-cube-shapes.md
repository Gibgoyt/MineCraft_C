# 001-ahmed-bohemoth-minecraft-from-scratch — phase 9, stage 1: Non-cube shapes

**Status:** done   (updated 2026-11-02)

## Goal

Non-cube block shapes (slabs, stairs, plants, torches) and alpha exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-renderer-features.md`.

**Owned paths** (edit only these, plus this file):

- `cuda/{src,include}/traverse/` shape table
- `cuda/{src,include}/shade/` alpha
- `tests/cuda/shapes/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Add a shape table of boxes. It is in `cuda/include/traverse/index.hu` and not `cuda/src/traverse/index.cu`: see issue 2
- [x] Add slabs, stairs, plants and torches, and crops, ladders, vines and lily pads, which are the rest of `config/blocks/`'s shapes. Water and lava stay cubes for Stage 09.3
- [x] Add alpha handling in `cuda/src/shade/index.cu`: a ray goes on through a texel of alpha 0
- [x] Add a setting (`--no-shapes`, `--no-alpha`, `render_frame_features_set`), a recorded cost and golden images; register test `render_feature_shapes`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^render_feature_shapes$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Results

Verification on 2026-11-02, in this worktree: the build, `render_feature_shapes` and `standards_lint` pass. All 62 ctests pass on debug, and `render_feature_shapes` also passes on a clean release build. `pgrep -a minecraft_c` prints nothing.

**Against an exact trace:** 2670649 pixels with every effect on, of which 40076 show what is behind a hole, 0 wrong. With both off, shapes alone and alpha alone, about 2.67 million pixels each, 0 wrong. Coarse pass on against off: 460800 rays, 0 different, with 6777 tiles starting past 0.

**Does the test catch a wrong box?** Making the torch 2 units taller on purpose failed the by-hand case (`t 2.8125, wanted 2.875`) and the trace (247 pixels wrong). Put back.

**With both off, the old pictures still hold:** `tests/run/render/frame/fixtures/*.png` as they were before this stage, drawn with the features at 0: 57600 of 57600 pixels exact on all three. They were then made again with the features on, and looked at: the tree in `start.png` now has holes.

**Cost**, on the card's clock, mean of 30 frames at 1920x1080, on the release build. Debug gives the same within noise, since nvcc optimises device code in both. Every number is one run, and differences under about 0.03 ms are noise between runs.

| Scene | Setting | Primary ms | Shade ms | Frame ms |
|---|---|---:|---:|---:|
| Real terrain, render distance 10 | all off | 0.342 | 0.235 | 2.083 |
| | shapes | 0.339 | 0.235 | 2.085 |
| | alpha | 0.334 | 0.289 | 2.131 |
| | both | 0.326 | 0.273 | 2.075 |
| Field of grass and leaves, low view, the worst case on purpose | all off | 0.328 | 0.217 | 1.985 |
| | shapes | 0.341 | 0.217 | 1.997 |
| | alpha | 0.329 | 0.921 | 2.697 |
| | both | 0.339 | 0.942 | 2.720 |

So shapes cost about 0.01 ms in the worst case, nothing measurable on terrain, and holes up to 0.7 ms on a screen of nothing but plants and about 0.05 ms on terrain. On that terrain view, 49244 of 2073600 pixels stop on something not a cube, and turning the settings off changes 73533.

**Wiring outside the owned paths**, the least that makes a real run draw this:

- `run/include/gpu_bridge/traverse/index.h`: `traverse_shape_t`, and `traverse_world_t` carries the table. `run/include/gpu_bridge/shade/index.h`: the feature bits, and `shade_frame` takes the world.
- `run/{include,src}/render/frame/`: builds and sends the table, `render_frame_features_set`. `run/{include,src}/render/`: holds its block.
- `cuda/src/frame/index.cu`: passes the world on to shade.
- `run/{include,src}/game/`, `run/src/main/index.c`: `game_config_t.features_off`, `--no-shapes`, `--no-alpha`. `sizeof(app_t)` went from 7040 to 7104.
- `tests/run/render/frame/`: the new argument, its goldens made again, and its two terrain tests run with the features off, since they compare voxels with the map. `tests/run/render/upload/` and `tests/cuda/traverse/fixtures/`: only the sizes of structs that hold these.
- READMEs of those directories, and `plans/REPO_STRUCTURE.md`.

## Commits

Appended as they land:

## Issues found

### 1. No block has a facing, so stairs, ladders and vines each have only one

- **Expected:** stairs that can face four ways, and a ladder or vine on whichever wall it is on.
- **What actually happened:** a voxel holds a block id and nothing else, and each of `oak_stairs`, `cobblestone_stairs`, `ladder` and `vine` is one id. `config/blocks/README.md` has no field for a facing. Nothing here could tell two ladders apart.
- **Evidence:** `compile/src/game_tables/blocks/index.c` emits no state besides `block_id_t`; `run/src/world/section/` stores one `uint16_t` per voxel.
- **What was done:** left as it is, and worked around. Stairs rise towards `+y`, and a ladder or a vine is a plate on the `+y` side of its voxel, always. A vine that terrain hangs on some other wall shows on the wrong side of its voxel. Whoever adds block states (placing, Stage 12.3, is the first that needs them) adds rows to the table and a value to `traverse_shape_t`, and nothing in the ray loop changes.

### 2. The stage file puts the table in `index.cu`, and it has to be in `index.hu`

- **Expected:** "a shape table of boxes in `cuda/src/traverse/index.cu`".
- **What actually happened:** `traverse_trace` reads the table, is inline in `cuda/include/traverse/index.hu`, and is also compiled into `cuda/src/shade/index.cu`. A device variable in another file cannot be reached without device linking, for the reason that header already gives for the loop.
- **What was done:** `static __constant__` arrays in the header, so each file has its own copy on the card. Moved `traverse_view_from` there too, as shade's kernel needs the same view.

### 3. `thin` in `config/blocks/` is two different shapes

- **Expected:** one row of the table per `shape:` word.
- **What actually happened:** `lily_pad` lies on the floor, while `ladder` and `vine` stand against a wall, and all three say `shape: thin`.
- **What was done:** `frame_shape_of` in `run/src/render/frame/index.c` gives `lily_pad` by its id the flat row, and the others the plate. `config/blocks/` is not this stage's to change. A word of its own for a lily pad would remove that one line.

### 4. Plants are a `+`, not an `X`

- **Expected:** what a player of Minecraft knows, two planes on the diagonals of the block.
- **What actually happened:** the plan asks for a table of boxes, and boxes are square to the axes, so a plant's two plates run along x and along y. Each is one texel thick, so looked at along its length it shows a thin stripe of the texture.
- **What was done:** left as the plan has it. It reads as a plant in `tests/cuda/shapes/fixtures/near.png`. Ahmed may want to judge it by eye in a real run: see the needs-Ahmed note in this stage's result.

### 5. Nothing else in the game knows about shapes yet

- **Expected:** a slab you can stand on, and targeting that sees the box.
- **What actually happened:** `run/src/physics/` and `run/src/player/` do not exist yet, so nothing is wrong today. The table is on the card side, and only the renderer reads it.
- **What was done:** written down for Stages 12.1 and 12.2, which will want these same boxes on the host, and should take them from one place and not type them again. `tests/cuda/shapes/index.c` has its own copy on purpose: it is the check.

### 6. Test written wrongly at first

- **Expected:** rays from a diagonal of 36 cells from -64 all reach their blocks.
- **What actually happened:** cases 0 to 5 missed, with shapes on and off. The map loads a circle of columns, and column (-4, -4) is outside a radius of 5.
- **What was done:** a bug in the test and not the kernel. It now starts at -36 with a radius of 8, and says why.
