# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 12: Blocks that face a way

**Status:** in progress: built and agent-verified (all 151 debug tests, release build); left: Ahmed's gate, a `bench_world` rerun on a cool card (Issues found 13), and the stoneworking bench, which waits on phase 6   (updated 2026-11-11)

## Goal

Stairs, the woodworking bench, and every block with a front face the way you're looking when you place them, and you can turn them before placing.

**Ahmed's words:** "the issue with rotating blocks should be for stairs! stairs, as well as the wood working table, currently only place due north or something weird! we need to properly rotate it please! default facing in the direction the user is facing and the user can rotate before they place it!"

## Why it happens

**A voxel holds a block id and nothing else,** so no block can remember which way it faces. Plan 001's Stage 09.1 issue 1 recorded the workarounds:

- **Stairs** always rise towards `+y`, which is north here, so "due north" is right.
- **The woodworking bench** always lies along `x`, with its west half where you click (plan 003 phase 4).
- **Ladders and vines** always sit on the `+y` side.
- **Wall torches** got four separate blocks to fake it (plan 003 phase 2).

Doors (phase 2), the smith table (phase 6) and rotating the preview (phase 10) all need the same thing.

## Approach: a block state beside the id

**Recommended,** and the answer to phase 2's open question:

- Each voxel gets a small **state** next to its id. It says which way the block faces (4 ways), plus a few bits a block can use: a door's open or shut, and upper or lower half.
- A block's YAML says which states it has: `facing: horizontal`, `half: upper_lower`, `open: true`. Blocks without one ignore the state, so nothing else changes.
- **Everything that reads blocks has to read it:** sections, saves, encoding for the card, ray traversal and shapes, collision, light, the physics raycast, and drops. Drops ignore the state, so a stair drops one stair whichever way it faced.

**Why not more blocks instead** (option A): stairs alone would need 4 blocks per wood (32), the bench 8, and a door 16 per kind. Every one is another YAML file and another id, and the table gets hard to read. A state does it once for all of them.

## How placing faces

- **By default, a block faces the player,** using the nearest of the four directions to where they're looking. Stairs' steps rise away from you, as in Minecraft. A bench lies across your view. A door opens towards you.
- **R turns it a quarter turn** before placing, shown in the preview (phase 10). The turn is kept until you change what you're holding.
- **Upside-down stairs,** by clicking a block's upper half or its underside: Minecraft does this, and it isn't asked for here. Left for later.

## Todo

- [x] Add a state to each voxel in `run/src/world/section/`, sized to fit in what a section stores, with its layout and padding written out per the coding standard
- [x] Add `facing`, `half` and `open` fields to block YAML, read by `compile/src/game_tables/blocks/`
- [x] Carry the state through saves (`run/src/save/world/`, with a version bump that keeps phase 1's name maps), GPU encoding (`run/src/render/encode/`, `cuda/src/world_buffers/`), traversal and shapes (`cuda/src/traverse/`), collision and raycast (`run/src/physics/`), light and ticks
- [x] Turn shapes in the box table by facing, so stairs, ladders and vines turn
- [ ] Place facing the player: stairs, ladders, the woodworking bench and the stoneworking bench. Done for all but the stoneworking bench, which does not exist yet: placing is generic, so phase 6 only writes `facing: horizontal` in its file (Issues found 7)
- [x] Turn the woodworking bench's two halves with its facing: along `x` or along `y`
- [x] Replace the four wall torch blocks with one wall torch and a facing, translating old saves through phase 1's name map
- [x] Add R to turn the held block, with phase 10 showing it. R is done; `player_interaction_place_plan` is what phase 10 draws (Issues found 6)
- [x] Add tests: stairs facing each way look and collide right; a bench turns both ways; a saved world keeps every facing; old worlds open with the old blocks facing as before; terrain digests unchanged where nothing faces
- [x] Add golden pictures of stairs and a bench in all four facings

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1500 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    pgrep -a minecraft_c    # must print nothing

The whole suite, as this touches nearly everything. Then a release build, which checks main's 16 KiB stack frame.

Ahmed: place stairs and a woodworking bench facing each way, and turn them with R before placing.

## Commits

Appended as they land:

- `9f8f7db7` Hold a block's state beside its id in the voxel, and keep it through the map, the columns and the saves.
- `5b9fa655` Turn blocks by their facing in the table, the card and the traversal.
- `8bd44971` Collide, cast rays and light by the block's id and facing.
- `6779bcd6` Place a turning block facing the way the player looks, turn the held block with R, and lay the bench to the right.
- `80482568` Make the four torches on walls one wall_torch that faces.
- `e74fdba9` Test block state: turned shapes, saves, placement, the wall torch, and repin density and biome.
- `1c11c37a` Repin the feature, hydrology and ore digests after wall_torch, and wrap the density and biome notes.
- `41bd4842` Aim the bench playthrough at the bench's west half again, now that the bench runs to the placer's right.
- `c9e70f67` Add golden pictures of stairs, benches, ladders and wall torches in all four facings, and redraw all_shapes.

## Issues found

**The biggest phase in plan 005.** It touches storage, saves, the renderer and physics. It goes before doors (phase 2), the smith table (phase 6) and rotation in the preview (phase 10).

**Speed:** a state per voxel adds memory and work for the card. Measure with `bench_world` before and after, and record it here.

**Open for Ahmed:** this is phase 2's question, answered by "we need to properly rotate it". Confirm a block state over more blocks? Default: a block state.


### 2026-11-10, the build of this phase

**1. The state lives in the 4 top bits of the voxel's `uint16_t`, which were always 0.** The table tool never allowed more than 4096 blocks, so a voxel's top 4 bits were free in every section, save and slab. They now hold the state: bits 12-13 facing, bit 14 upper half, bit 15 open (`run/include/types/block_state/`, a header compiled by GCC and nvcc both). So "sized to fit in what a section stores" is exact: no struct grew, no format grew, the section's palette tells two facings apart as it tells two blocks apart, and a section costs more only when it holds a block in more than one facing. `compile/src/game_tables/blocks/` now asserts `GB_MAX_BLOCKS == BLOCK_STATE_ID_LIMIT`, and the generated header asserts `BLOCK_COUNT <= BLOCK_STATE_ID_LIMIT`.

**2. Reading: ids by default, voxels where the facing matters.** `world_map_block_get`, `column_block_get` and `collision_cell_read` now give the id alone, so the 76 places that index `block_defs` by what they read stayed right without a change. `world_map_voxel_get`, `column_voxel_get` and `collision_cell_read_voxel` give the id with its state, and are used by: collision (`collision_gather`), the raycast (`raycast_hit_t.voxel`, beside `block`, which stays the id), the hands (bench halves, placing), the explosion (bench halves, wall torches), the tick (whether a wall torch still has its wall), and the zombie's and arrow's line tests. What reads sections or the edit log directly masks: light (`light_block_def`), the tick's random ticks, game/cover, mob/path's classes, item's chest and furnace, the far field's saved lines (`save_world_far_line`), and the Atlas's columns.

**3. The card needed no new data.** The encoder already sent the whole `uint16_t` as the material, so the state reaches the card in what it already has. `TRAVERSE_VALUE_MATERIAL_MASK` became `BLOCK_STATE_ID_MASK` (0x0FFF), so everything that used the material for a table (shapes, faces, water, the far field) gets the id, and `TRAVERSE_VALUE_VOXEL_MASK` (0xFFFF) is new. `traverse_voxel_hit` turns each box of a non-cube shape with `block_facing_box_turn` before cutting the ray with it; a cube pays nothing. `shade_surface_read` reads a turned block's own face and the place on its picture (`block_facing_face_to_local`, `block_facing_point_to_local`), with light, sun and AO on the world's face. The test fixture in `tests/cuda/traverse/fixtures/world.h` packed random 16-bit materials, and now uses `TRAVERSE_VALUE_VOXEL_MASK`.

**4. Saves: version 6, and version 5 opens unchanged.** A version 6 state number is a place in the world's list in its low 12 bits and the state above, so the list is limited to 4096 names (`SAVE_WORLD_NAMES_MAX`, refused as `SAVE_WORLD_ERROR_TABLES` past it). A version 5 number was a place below that with nothing above, which is a version 6 record of blocks facing north, so a version 5 manifest and its delta files are read as they are, and a record carried into a new file keeps its bytes; the next save writes version 6. Phase 1's name maps are kept: the place is mapped, the state carried, and on reading `block_state_clean` keeps only what the block declares now. Tested by forging a version 5 world (`a_world_from_before_states_opens_with_every_block_as_it_was`).

**5. The four wall torches are one `wall_torch`, which faces its wall.** `torch_wall_east` (leaning east, wall to the west) is `wall_torch` facing west, and so on. A world saved with the old names reads them through `save_names_renamed_state` in `run/src/save/names/`: `save_names_map` finds `wall_torch` for an old name this build lacks, and `save/world` adds the facing in `borrowed_file_to_live` only. The old name keeps its place in the list, and `wall_torch` joins it when needed. `torch_wall_south` maps to facing north, which is no state, so such a list is this build's numbering under another name and `names_same` is 1. The renamed state applies only when this build lacks the old name (found by a failing test in the first stage, when both still existed).

**6. Placing faces the way the player looks, turned by R.** `player_interaction_input_t.look_facing` is the nearest of four to the yaw main last sent (`run/src/player/`), and a turning block goes down `block_facing_turn(look_facing, turns)`. A ladder or vine put on the side of a block faces that side (so it lies on the wall aimed at). A torch on a wall faces its wall, and R does nothing to it. `player_interaction_place_plan` is the whole decision, pure, for phase 10's preview; `player_interaction_tick` places exactly what it says. R is `KEY_R` pressed (`game_loop_input_frame`, and the headless script), `player_turn_send` → a `PLAYER_HAND_TURN` bit in `hands_latch` → `player_interaction_turn` on the next tick; the turns live in `player_interaction_t.turns` (in what was padding, so old state files read 0) and go back to 0 when the hand is given a different block.

**7. The stoneworking bench and the smith table do not exist yet.** Nothing was done for them beyond making placing generic: phase 6 declares `facing: horizontal` in their files and they are placed facing the player with no code.

**8. The bench's halves turn.** `bench_half_other(voxel, &step_x, &step_y, &other)`: the second half is to the placer's right, `x + 1` facing north (as before), `y - 1` east, `x - 1` south, `y + 1` west. Each half's pictures turn with it on the card.

**9. Not done, as the phase says:** upside-down stairs; doors' shapes (phase 2 uses `half` and `open`); the preview itself (phase 10).

**10. The four torches' renumbering moved every terrain digest, and the land is the same.** Removing `torch_wall_east`, `_north`, `_south` and `_west` and adding `wall_torch` moved the number of every block after them in the alphabet, so seven pins failed: density, biome, feature's generator row and forest, hydrology and its flood, and ore. A temporary back-conversion (each voxel's id turned back into the numbering from before: air at 0, `wall_torch` taken out, the four old names put back in order, state bits dropped) gave each of the seven its old pin exactly: density `0x7FDAB60241EF2D7A`, biome `0x28DECF2E02905D79`, feature generator `0xEEC1F6DAF60AD0E7`, forest `0x73D8B07F1C6CDBEB`, hydrology `0x98FFE66BE73EC420`, flood `0x0579E0C4C7F4EFCA`, ore `0xF48FD4AD68F5DFA6`. None of that land holds a voxel with state bits or a wall torch. So they are repinned, each with a "Plan 005 phase 12" line, and `TERRAIN_GENERATOR_VERSION` stays 1, as plan 003 phases 2 and 3 and plan 004 phase 8 did; the generator row's comment keeps its old value. The back-conversion is not in the tree.

**11. Two playthroughs aimed by the bench's old orientation.** The bench now runs to the placer's right. In `stairs.script` the 150 turn away from the bench still met it, so the right click opened it again and no stairs went down; the turn is now 300 (and the turn back 325). That turn back meets the west half, so the test's half is now `woodworking_bench_west` (its point, that the half aimed at is the one mined, holds). In `bench.script` "the far half, the west" aim met the seam and took the east half; 55 further left puts the cross well inside the west half, looked at in its screenshots, and the test is unchanged.

**12. The shapes test's cases now stack.** Every case is drawn four ways, so there are more cases than the diagonal holds: they now go 48 to a diagonal, the next 48 lifted 8, and no ray of one meets another. `all_shapes.png` moved by 3 pixels, at most 3 of 255, all on a wall torch's flame, which is now one block turned by its facing instead of four leaned by hand: redrawn. `near.png` moved by one pixel by 1, within the tolerance, and is kept. `facings.png` and `facings_back.png` are new, looked at before adding: stairs, benches, ladders and wall torches each in four different facings.

**13. Speed: `bench_world`, not settled; Ahmed should rerun it on a cool card.** Release, the default rows, `--record` to a scratch file. Before (`before-phase-12`, 2026-11-10 14:17Z, on the base of this branch) and after (`after-phase-12`, 2026-11-11, at `c9e70f67`, straight after the full debug suite and a release build). **Every one of the 11 rows they share drew the same picture hash before and after.** The frame rates fell to a third (`plains/orbit` 80.7 to 24.9 fps, `deep_ocean/orbit` 47.0 to 12.0, `jagged_peaks/orbit` 163.9 to 68.4), but the passes this phase does not touch fell as much: coarse took 1.85 to 4.35 times as long and post about 3.5 times, and just after the run `nvidia-smi` read 77 degrees C and 300 MHz of 2100 idle, the slowed card `bench/world/README.md` describes. Measured against the coarse pass of the same run, the shade pass, which now turns non-cube boxes, is within -4% to +6% of before on 9 rows (`jagged_peaks/orbit` -50% and `cave/orbit` +6% move with the slowdown within the run). By design a cube pays nothing, and the state rides in the 4 bits the card already had (issues 1 and 3), so no cost is expected; this run can neither show nor rule out a small one.

**14. Tests, 2026-11-11.** `tools/test-or-build.sh timeout 1800 ctest --preset debug --output-on-failure`: 125 of 151 passed before the 1800 s ran out, then `-I 126,151`: 26 of 26. All 151 pass, `run_render_distance_live` among them; the whole suite takes about 38 minutes on this machine, longer than the timeout in Verification. `tools/test-or-build.sh cmake --preset release` and `cmake --build --preset release`: exit status 0. `pgrep` of this worktree's build: nothing.
