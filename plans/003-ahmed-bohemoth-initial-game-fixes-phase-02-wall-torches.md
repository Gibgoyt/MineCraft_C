# 003-ahmed-bohemoth-initial-game-fixes — phase 2: Wall torches

**Status:** in progress: everything an agent can do is done and checked, the whole debug suite 108 of 108. Only Ahmed's own check is left   (updated 2026-11-05)

## Goal

Aiming a torch at the side of a block puts it on that wall, leaning out, lit and drawn correctly. It drops a torch if it's broken or if the block holding it goes.

**Ahmed's words:** "torches can not be put onto a wall let's please make that possible!"

## Why it happens

- `interaction_place` in `run/src/player/interaction/index.c:304` always places the same block, whichever face you aim at.
- `interaction_needs_ground` (same file, line 199) requires solid ground below any `BLOCK_SHAPE_TORCH`, so aiming at a wall with nothing below fails.
- A voxel holds a block id and nothing else, so no block has a facing (Stage 09.1 issue 1). There's one torch box, standing in the middle (`cuda/include/traverse/index.hu:73`).

## Approach

Follow the existing one-id-per-variant model, with no new block state: four new blocks, one per wall. The `blocks/torch_wall` texture already exists and is unused.

## Todo

- [x] Add `config/blocks/torch_wall_{east,west,north,south}/index.yaml`: light 14, drops `torch`, texture `blocks/torch_wall`. All but the texture, which is two new recipes: see issue 1
- [x] Add four leaning boxes to the table in `cuda/include/traverse/index.hu`, the `TRAVERSE_SHAPE_*` list in `run/include/gpu_bridge/traverse/index.h`, and the shape names in `compile/src/game_tables/blocks/index.c`. Five boxes to a shape, as the table holds only boxes along the axes, so each leans by steps
- [x] Add the matching `BLOCK_SHAPE_*` cases to collision and targeting in `run/src/physics/collision/` and `run/src/physics/raycast/`. Targeting has them. Collision needs no case: it returns no boxes for what is not solid before it looks at a shape
- [x] In `interaction_place`, choose the block from the face hit: a side gives the matching wall torch, a top gives today's floor torch, and a bottom places nothing
- [x] A wall torch needs the solid block behind it. When that block is mined, or destroyed by an explosion, the torch breaks and drops, as `interaction_stack_break` does for what stands on top. Felling too, by the same function, and anything else that takes the wall: see issue 3
- [x] Handle the new shapes wherever `BLOCK_SHAPE_TORCH` is checked: `tick_is_open` in `run/src/world/tick/index.c:95`, `run/src/world/light/`, `run/src/entity/explosion/`, `run/src/mob/spawn/`, `run/src/render/lod/`, `run/src/render/frame/`. Light and spawning look at no shape, and need no change: a test holds each to what it does
- [x] Add tests in `tests/run/player/interaction/`: each of the four faces gives the right block, a top face gives a floor torch, a bottom face gives nothing, and breaking the block behind drops the torch. New ctest `run_player_interaction_wall_torch`, as that test is past the size for splitting, and tests added to `render_feature_shapes`, `run_mob_creeper`, `run_player_felling`, `run_world_sim`, `run_world_light` and `run_mob_spawn`
- [x] Add a scripted headless screenshot of all four, and check it by eye. Taken by the new test from the game's own headless scene, not by a script file: see issue 4
- [ ] Ahmed: craft torches, place them on all four sides of a block and on the floor, then mine the block behind one

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_player_interaction|run_player_interaction_wall_torch|run_world_light|run_world_sim|run_physics.*|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: craft torches, place them on all four sides of a block and on the floor, then mine the block behind one.

    tools/test-or-build.sh cmake --build --preset release
    ./build/release/bin/minecraft_c --windowed

Results, on this phase's code:

| Check | Result |
|---|---|
| Whole debug suite, `ctest --preset debug -j1`, which holds the commands above | 108 of 108, in 1939 s |
| Sanitize: `run_player_interaction_wall_torch`, `run_player_felling`, `run_mob_creeper`, `run_world_light` | All passed |
| `pgrep -af minecraft_c` after the last run | Printed nothing |
| The pictures, by eye | All four from each corner at midnight: each on its own side, leaning out, flame up, foot at the wall, and the stone lit by them. Issue 4 has how to make them |
| A real world from before this phase | Opened with the same pictures, byte for byte: issue 5 |
| 15 lines of the code broken one at a time | All 15 caught in the end, after two new test cases: issue 6 |

## Commits

Appended as they land:

- `a0cb295` Let a torch go on the side of a block, leaning out from it as one of four new blocks, lit and drawn by steps, and dropped when the block behind it goes by mining, a blast or anything else.
- `78a5859` List the wall torch directory and its test in the repository structure.

## Issues found

**1. `blocks/torch_wall` cannot be used, and two new textures replace it.** Expected, from the plan: the unused texture fits the new boxes. Actual: it is a side view of a leaning torch (its foot at column 14, row 11, its flame at about column 8), drawn for a mesh Minetest has and we do not. `shade_face_uv` places a texture by where in the voxel a point is, so `u` runs opposite ways on opposite faces: `u = 1 - x` on `+y` and `u = x` on `-y`. So for a torch leaning east the picture is right from the north, and from the south it is back to front, and each box then samples empty texels and is invisible. The picture also leaves column 15 empty, a gap of a texel at the wall. Mirroring it would need a tool change or a second copy of the pack's pixels, and would keep the gap. Done: `blocks/torch_lean_side` and `blocks/torch_lean_cap`, ours by recipe, owing no credit, with no holes: a side's colour goes by height alone, so one texture serves all four sides of all four blocks with no change to the shader. `blocks/torch_wall` stays unused, as it was, and `vector.yaml` still needs it. See `run/src/world/wall_torch/README.md`.

**2. Adding blocks changed pinned digests, and it was only the numbers.** Expected: phase 1 said a new block would move ids, and the digests would be this phase's to update. Actual: the first whole suite failed on 7 pins in 5 tests, `run_terrain_biome`, `_density`, `_feature` (2), `_hydrology` (2) and `_ore`, with `torch_wall_*` sorting ahead of `tuff`, `water` and the rest. To tell that from a change of land: the four blocks were renamed `zz_torch_wall_*` for one build, which sorts them last so that no id moves, and all 5 tests passed on the **old** pins, unchanged. The rename was undone, and the pins updated with a line each. `TERRAIN_GENERATOR_VERSION` stays 1, as the land is the same.

**3. A wall can go by more ways than the plan listed.** Expected, from the plan: mining and explosions. Actual: sand under a torch falls, leaves decay, and logs go to the tree feller, and a torch left floating would be a bug. Done: felling goes through `player_interaction_stack_break`, which now takes them; `run/src/world/tick/` wakes a wall torch beside any change and breaks it if the block behind is no longer solid, through the same ring as wheat. A floor torch has no such thing, and a torch on sand stays in the air when the sand falls, as it did before this phase. Left alone: not asked for. `felling_has_room` now leaves room for 4 more drops a log, and a test holds it to that: no drop lost.

**4. A script cannot put a torch in a hand.** Expected: a script file drives the built program to the picture. Actual: `--headless --test-world --walk` starts with empty hands, and coal and a mined path to it would be a long script tied to terrain, as phase 1's issue 11 found. Done: the test `all_four_are_drawn_where_they_hang_and_show_their_flames` runs the game's own `game_headless_scene`, as `display_headless_run` does, with the hands taking each torch by a real aim from `player_camera_target`. To see them, as they were checked:

    mkdir -p /tmp/wall_torches
    MC_WALL_TORCH_SHOTS=/tmp/wall_torches tools/test-or-build.sh timeout 280 ctest --preset debug -R '^run_player_interaction_wall_torch$' --output-on-failure

They also go through `render_feature_shapes` against its exact reference, written apart from the table, so its golden pictures were made again, with all four in the scene. Compared with the old ones: `all_shapes.png` differs in 21 pixels of 57600 and `near.png` in 58, all in the row where the new blocks are.

**5. A real world, made before this phase, through the real new blocks.** Expected: phase 1's promise holds for the first real blocks added. The tree at `d949225` was built apart with `git archive`, and `tests/run/playthrough/fixtures/day.script`, seed 36, played on it. A copy of that world was then opened in both builds with one script. New build: "was saved with other blocks: 4 new here, 0 gone from here", "0 past reading", 0 bytes live. `world.png` and `inventory.png` from the two builds were **byte for byte the same** (sha256 `e582271ae84fb0...`, `c4415275cd2559...`). This goes a step past Ahmed's check in phase 1, and does not replace it: that box stays his.

**6. Whether the tests notice.** 15 lines were broken one at a time, and the test for each run. Caught at once: 13, among them the leaning reversed, a wall that is not solid, a torch taken that was not hanging from the block, no drop, no blast, no wake in the simulation, water not washing it away, a box moved, a box left out, the wrong shape sent to the card and the aim's box on the wrong side. **Missed, two:** an underside giving a floor torch, as the test had no floor under the cell; and the 4 more drops of room in the felling ring. Fixed: a floor in that test, and the test `a_log_with_a_torch_on_every_side_waits_for_room_in_the_ring_and_nothing_is_lost`. Both broken again were caught. The code was right.

**7. A floor torch shows no flame, for Ahmed.** Seen in the picture: the wall torches have a flame, and beside them the floor torch is a dark stick with an ember at the top. Its box stops at 20 of 32, and the pack's flame is in the rows above that, so it has been this way since Stage 09.1. It still gives its light. Left alone, as it was not asked for and would change the golden pictures: raising the box's top would be a one-line fix if Ahmed wants it to match.

**8. The plan's Verification named a test that does not exist.** `run_world_tick` matches nothing: `tests/run/world/tick/` is ctest `run_world_sim`, by its `test.cmake`. Corrected above, with the new ctest added.

**9. Sizes.** `tests/run/player/interaction/index.c` was at 1573 lines before this phase, so the new tests went in a directory of their own. `tests/run/world/tick/index.c` is now over 1900, under the 2000 that fail the build. Left for whoever adds to it next. `cuda/src/shade/index.cu` was at 1461 and was not touched.
