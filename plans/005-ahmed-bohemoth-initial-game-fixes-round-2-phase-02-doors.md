# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 2: Doors

**Status:** built and agent-verified on phase 12's block state (option B); left: Ahmed's gate   (updated 2026-11-10)

## Goal

Doors can be placed, are two blocks tall, face the way you placed them, open and close with the right button, and block you when closed.

**Ahmed's words:** "doors can not be placed down for some reason??? let's please fix this!"

## Why it happens

**Door blocks were never added.** It's the same as copper armour was:

- `config/items/wood_door`, `iron_door` and `glass_door` have only a name and a picture, and no `block:` field.
- `config/blocks/` has no door.
- So a door in your hand counts as an item that places nothing (`run/src/game/tick/index.c:40`, `ITEM_NO_BLOCK`).

## The real problem: blocks can't face a way or open

A voxel holds only a block id. That's now the fourth time this has come up: wall torches got four blocks, stairs face one way, ladders face one way, and the bench can't turn. A door needs **2 halves × 4 facings × open or shut = 16 variants for each kind of door**.

**Option A, as we've done so far:** 16 blocks per door kind, 48 for three kinds. No new system. Each variant is a YAML file and a box shape.

**Option B, a block state:** voxels also hold a small state (facing, open, upper or lower), stored next to the id. Doors need it, and it would let stairs, ladders and benches turn too. It's a bigger change: section storage, saves, GPU encoding and traversal, light and collision all read it.

**Recommended: B, as its own phase before doors.** A is quicker now but leaves stairs, ladders and benches stuck facing one way, and makes the block table much bigger. **This is Ahmed's call** (see Issues).

## Todo

Built with option B, phase 12's block state, which was on main (Issues found 1):

- [x] Add blocks for `wood_door`, `iron_door` and `glass_door`: one block each with `facing`, `half` and `open`, a plate three sixteenths thick, and open is the plate turned on its hinge
- [x] Give each door item its block, choosing the facing from the player's direction and the hinge side from the neighbours
- [x] Place both halves together, refusing if either cell is taken. Breaking either half removes both and drops one door, like the bench
- [x] Open and close with the right button, swapping both halves at once. Iron doors stay shut by hand, as there's no redstone; say so in their README
- [x] Collision: closed doors block, open ones don't. Mobs' pathfinding treats closed doors as walls
- [x] Add textures: upper and lower for each kind, from our recipes, each mapped in `textures/import_minecraft_default.yaml`
- [x] Add tests in `tests/run/player/interaction/`: placing in each facing, both halves, opening, breaking either half, saving and reloading an open door
- [x] Add a scripted headless proof with screenshots, checked by eye

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_player_interaction.*|run_physics.*|run_mob_path|run_save_world|run_item_crafting|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: craft a wooden door, place it facing each way, open and close it, walk through, then break the top half.

## Commits

Appended as they land:

## Issues found

### 2026-11-10, the build of this phase

**1. Option B: one block per kind, on phase 12's state.** `wood_door`, `iron_door` and `glass_door` each declare `facing: horizontal`, `half: upper_lower`, `open: true` and a new shape, `door`, which the table tool refuses without all three. The upper half has `BLOCK_STATE_UPPER`, both halves have the same facing and open bit. `run/src/world/door/` (new) says which block is a door, where its other half is, which opens by hand, and the facing for the hinge. Each door item now has `block:`. `obsidian_glass_door`, an item with no recipe, still places nothing: it was not asked for.

**2. The hinge has no bit, so it is the facing.** A voxel has 4 state bits and a door needs facing, half and open, so the hinge is always at the door's left end as it faces, and a door hinged on the right faces the other way: placed looking north with the hinge to the east, it faces south, its plate on the near side of its cell, three sixteenths of a block from where Minecraft's would stand. Shut and facing north the plate is against `+y` with its hinge at the west end; open, it is that plate turned a quarter anticlockwise about the hinge, against `-x` (`block_state_drawn_facing` in `types/block_state`, used by the card's traversal and shading, the raycast and the tests' reference). The hinge side: another door beside the lower cell puts it on the other side (a pair), otherwise the side with more solid blocks beside the lower and upper cells, otherwise the left (`door_hinged_facing`).

**3. Pictures for two halves from one block's six.** The face table has six pictures per block, so the upper half shows its block's `top` and `bottom` pictures on its south and north faces, and the other way round (`block_state_picture_face`, on the card in `shade_surface_read`). So a door's file says `top`/`bottom: blocks/<kind>_door_upper` and `side: blocks/<kind>_door_lower`. The plate's thin edges show a sliver of one or the other.

**4. A bug from phase 12, fixed here: `world_edit_block_set` compared the whole voxel it was given with the id alone.** It read `before` with `world_map_block_get`, which since phase 12 gives the id, so setting a turned or open block back to its bare voxel (a door shut, facing north) was taken as no change and did nothing, and every edit record's `before` lost its state. It now reads `world_map_voxel_get`. Found by the door test shutting a door; `run_mob_path`'s cache test of opening a door needed it too. Both readers of `before` (`mob/path`, `item`) mask the id or class it.

**5. Collision and paths by state.** A door is not `solid` in its file. `collision_block_boxes` gives a shut door its plate and an open one nothing, so an open door is walked through and a shut one stops a body (tests in `run_physics`, `run_player_interaction_door`). `mob/path` reads the voxel: a shut door is `MP_CLASS_BLOCKED` (no way through or on top), an open one `MP_CLASS_OPEN`; opening it is an edit whose class changes, so a cached way is searched again. The raycast aims at the plate as drawn, open or shut.

**6. The placement plan, minimally.** `player_interaction_place_plan` gives a door as two cells, `cells[1]` straight above `cells[0]`, `voxels[1]` the upper half, `count` 2, so phase 10's preview draws both halves and a door is `BLOCKED` when either cell is taken or the body is in either plate. The only changes: the bench's second-cell code became `interaction_pair_other` (bench beside, or door above), and a door's lower voxel is turned for its hinge before the plan is filled. `player_interaction_tick` still places exactly what the plan says. Mining goes through the same pair code, so mining either half takes both and the half mined drops the one door; blasts do the same as for the bench (`entity/explosion`).

**7. Opening once a press.** The right button on a door that opens by hand opens or shuts both halves, one edit each, with a place sound. `player_interaction_t.place_held` (in what was padding, so old state files read 0) remembers the button was down last tick, so holding it after placing a door, or after opening one, does nothing more. The hand never places onto such a door. **An iron door stays shut by hand** (no redstone yet), and the button places against it as against any block: `run/src/world/door/README.md` and `config/blocks/README.md` say so.

**8. Not done:** a door does not break when the block under it goes, and can be placed with nothing under it, as the bench can. Upside-down or double-wide doors, and redstone, are not in this plan.

**9. Textures: six recipes of our own,** `blocks/{wood,iron,glass}_door_{upper,lower}`, looked at enlarged before committing: a panelled plank door with a four-pane window above and an iron handle opposite the hinge; riveted iron plates with barred dark windows; clear glass (alpha 72) in a thin pale frame with a kick rail and a bar handle. Each is in `vector.yaml`'s needs and in `textures/import_minecraft_default.yaml`: wood from `block/oak_door_top.png` and `block/oak_door_bottom.png`, iron from `block/iron_door_top.png` and `block/iron_door_bottom.png`, and glass kept, as Minecraft has no glass door. Minecraft's door pictures put the handle where its own model mirrors them, so with the default pack the handle may show on the hinge side: not checked here, as the pack is not on this machine. 227 recipes now (`textures/README.md`).

**10. The three doors renumbered the land, and nothing else.** `glass_door`, `iron_door` and `wood_door` moved the ids after each of them in the alphabet, and seven pins failed: biome, density, feature's generator row and forest, hydrology and its flood, and ore. A temporary back-conversion (each voxel's id lowered by the number of door ids below it, state bits kept, counting any door seen) at each digest gave every one of the seven exactly its pin from main: biome `0x7347D2E2EB87F8B2`, density `0xA0ADF8098E10E291`, generator row `0xE91D8A079B8A9907`, forest `0xC02077A1494E53C4`, hydrology `0xDDE694659EE1A8CB`, flood `0x5B83274074AD61E1`, ore `0x42C989080FAB0EA9`. So they are repinned, each with a "Plan 005 phase 2" line, and `TERRAIN_GENERATOR_VERSION` stays 1, as phase 12 did; the generator row's comment keeps its old value. The back-conversion is not in the tree.

**11. Golden pictures.** `render_feature_shapes` has a third scene, every kind of door shut and open, two tall, facing each way, and a pair, against its exact trace from all sides: 2673045 pixels compared, 0 wrong. `tests/cuda/shapes/fixtures/doors.png` and `doors_back.png` are new, looked at before adding. Writing them rewrote `near.png` within its tolerance; that one was put back. The other pictures did not move.

**12. The proof in the built program:** `tests/run/player/interaction/door/fixtures/door.script`, run by `run_playthrough` (whose timeout went from 1350 to 1650 s), seed 36: three doors from six planks on the bench, one placed beside the bench (hinged on the bench's side, by the wall rule), opened, shut, opened and shut again, then its top half mined by hand: both halves go and one door drops. Looked at in its screenshots. Walking through it is not in the script: the player looks diagonally there, and walked round the door's end rather than into it, so walking through is held by the tests (`run_physics`, `run_player_interaction_door`) and Ahmed's gate.

**Open for Ahmed: option A (16 blocks per door) or option B (a real block state, then doors)?** B is my recommendation, because it also fixes stairs and ladders facing one way, and lets benches turn.

**2026-11-06: B is now phase 12.** Ahmed asked for stairs and the woodworking bench to face the player and turn before placing, which needs a block state. So doors build on phase 12, and option A isn't needed unless Ahmed says otherwise.
