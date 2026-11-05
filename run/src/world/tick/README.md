# run/src/world/tick

**Purpose:** the block simulation: water and lava flow, sand and gravel fall, leaves decay, wheat grows and grass spreads.
**Tier:** Arena (depth 2): `world_tick_init` takes once from an `arena_t *` the Owner gives. The section pool is reached only through `world_edit_block_set`.
**Owns:** nothing. It holds its tables as `borrowed_` from the arena.
**Lifetime:** World. `world_open` starts it and `world_close` ends it.
**Threads:** one at a time, the one holding the map: `sim`.
**Public functions** (`run/include/world/tick/index.h`):

- `world_tick_init`, `world_tick_deinit`: the tables.
- `world_tick_leaves_check`, `world_tick_sweeps_pending`: logs have gone from a box, for `run/src/player/felling/`, so its leaves are looked at now; how many such boxes are left.
- `world_tick_broken_next`: the oldest block with drops that went by itself, for whoever makes the items.
- `world_tick_seed_set`: where the random ticks' numbers come from.
- `world_tick_column_leaving`: gives back a column's levels.
- `world_tick_section_levels`, `world_tick_section_levels_restore`: a section's levels, to a save and back.
- `world_tick_column_saved`: a column just in, and which of its sections a save gave it: what has something to do as it stands wakes.
- `world_tick_now_set`: where a saved world's count of ticks takes up.
- `world_tick_step`: one tick of game time.
- `world_tick_fluid_level`: a fluid voxel's level, for whoever needs to draw or save one.
- `world_tick_pending`: voxels waiting for a tick.

**May be called by:** `run/src/world/`, `game`.
**Depends on:** `world/section`, `world/column`, `world/map`, `world/edit`, `arena`, `assert`, and the generated `tables/blocks/`.

## How a change gets noticed

This reads the edit log, with a cursor of its own, as `world/light` does. Nobody has to call it about an edit. Each record wakes the voxel and its six neighbours, and of those only what could do something gets a tick: a fluid, a block that falls with room to, wheat, farmland.

So terrain does nothing until something touches it. A column coming in writes past the log, and generated water stays as it was made, however it hangs, until a block next to it changes. That is on purpose: otherwise every cave wall with an aquifer behind it would start to run as the player walked past.

All writes go through `world_edit_block_set`, so light and the renderer see them as any other edit, and this module's own records wake what is next to them. `world_tick_step` reads the log before running what is due, for what others wrote, and after, so that each delay is exactly as long as it says. Water moves one block in 5 ticks, whoever started it.

## What is waiting

A wheel of 64 lists, one per tick modulo 64, of nodes. A voxel has one node at most, found through a set, so a wave of edits around one voxel costs one tick. A node remembers the generation of its column, and is dropped when due if that has gone. One tick runs at most 16384 of them and writes at most about that many blocks, so that the log (65536 records) never laps its readers; the rest go first next tick.

If nodes run out, a wake is dropped and `node_full_count` says so: that voxel sits still until something next to it changes again. The default of 262144 takes a lot more than the budget of a few ticks.

## Fluid levels

One block per state, for now (see `compile/src/game_tables/blocks/README.md`), and a voxel of water is `BLOCK_ID_WATER` wherever it is. So the level is kept here, half a byte per voxel, in a page per section that has any flowing fluid. A page is 2 KiB, comes from a fixed store, and goes back when its last level does, or with its column.

| Level | Means |
|---|---|
| 0 | A source. Also what any voxel with no page reads as, which is why water from terrain costs nothing here |
| 1 to 7 | Flowing: how far from what feeds it. Water uses each number. Lava goes 2, 4, 6 |
| 8 | Falling: fed from above |

If pages run out, or the pool has no room, the fluid stays where it is, the count says so, and that voxel tries again 20 ticks later.

Levels go to a save with the blocks of their section: `run/src/save/world/` reads `world_tick_section_levels` as it captures, and `run/src/game/` gives what it read back to `world_tick_section_levels_restore` as a column comes in, which wakes each flowing voxel so that what was running goes on. A level changing with no block changing flags its section for the save itself, as no log record does. The renderer still draws every fluid voxel at full height.

What is waiting for a tick is not saved. It need not be: `run/src/game/` calls `world_tick_column_saved` for every column as it comes in, and each voxel in a saved section, or touching one, is looked at as it stands: a fluid with somewhere to go or that is not a source, a block that falls with nothing under it, wheat off its farmland, farmland under something solid. Those wake, however long they had been waiting, and nothing else does, so a sea whose section was saved does not take a tick per voxel. A column remembers which sections a save gave it, so that a neighbour coming in later wakes what faces them. Ground that has only ever been generated is left at rest, as it always was. So a column that left and came back in one session goes on just the same. Measured: see Stage 16.2's issue 9.

## The rules

**Flowing fluid** works out what it is from what feeds it, and goes if nothing does:

1. Water with two or more sources beside it and either something solid or a source under it becomes a source. Lava never does.
2. With its own kind above, it is falling.
3. Otherwise its level is the nearest neighbour's of its own kind plus its step, with falling counting as 0: step 1 for water, 2 for lava. Past 7, it is gone.

If a neighbouring column is not loaded, step 3 cannot be trusted, so it keeps its level and tries again in 20 ticks. So a fluid heading into a column that has not come yet carries on when it does.

**Then it spreads.** Down, if it can. If it went down, then not sideways, unless three or more sources are beside it, which is what makes the edge of a sea run. If it could not go down, then a source spreads sideways, and flowing fluid does unless its own kind is below it.

Sideways it goes only towards the nearest way down within reach, 3 steps past the voxel beside it for water and 1 for lava, and to every direction that ties. With none in reach, it goes all ways. It runs through what it could enter and through its own kind, but not through a source.

**What it enters:** air, and things small enough to wash away: `cross`, `crop` and `torch` shapes. Not `kelp` or `seagrass`, which live in water, and not `thin`: a ladder holds water back. What water washes away, a falling block buries, leaves that decay and a crop left with no farmland under it go in a ring, for `world_tick_broken_next`: `run/src/game/` rolls their drops and puts them on the ground. Lava burns what it enters to nothing, and a flowing fluid does not yet carry an item along.

**Lava and water.** Lava with water beside it or above it hardens when its tick comes: a source to obsidian, flowing lava to cobblestone. Lava going down into water leaves stone there. Water does not enter lava.

**A bucket's worth:** placing water with `world_edit_block_set` on empty voxels makes a source. Setting it on a voxel already holding flowing water changes nothing, because the block is the same. Whoever adds buckets needs a call for that.

## Falling blocks

`BLOCK_FLAG_FALLS`: sand, red sand and gravel. A falling block is not an entity: it is a block that moves down one voxel every 2 ticks. It falls into what a fluid could enter, and through fluids, which it replaces. A stack falls one block at a time from the bottom.

## Random ticks

Each tick, in each column within 8 chunks of the map's center, each section picks 3 voxels. A section all of something that does nothing is skipped without a look. The numbers come from the seed, the tick, the column and the section and nothing else, and columns go in order of place: so the same columns give the same results, whichever order they streamed in.

- **A box of leaves.** `world_tick_leaves_check` is for a tree that has just come down, with no need to wait for random ticks to find what it held. The box, grown by the 6 steps a leaf can reach, is gone through from the top in `WORLD_TICK_SWEEP_TICKS` ticks, however big, and each leaf is put to the same rule as a random tick, with the same drops. So a whole crown goes over a second and a half, and another tree's leaves in the box stay. A vine goes if nothing solid other than leaves is beside it or above it and no vine is above it: it goes from the top, so the one above has had its turn. Nothing else makes a vine go, and a tree's would otherwise stay floating. While the ring of what went by itself is half full the box waits, so that nothing in it is lost. Up to `WORLD_TICK_SWEEPS` boxes at once, and past that the last takes in the new one. Boxes are not saved: what they do not get to, random ticks do.
- **Leaves** (`BLOCK_FLAG_DECAYS`) go if no log is within 6 steps through other leaves. A column not loaded counts as if it had one, so a tree on the edge of the world does not fall apart. Leaves a player places decay the same way until something can say who placed them.
- **Wheat** needs light 9, at the crop. With water within 4 sideways of its farmland, at that height or one above, it goes up a stage in 1 tick of 7. Without, 1 of 13.
- **Grass** turns to dirt under anything opaque or any fluid. Otherwise, with light 9 above it, it makes 4 tries at a random voxel at most 1 away sideways, from 3 below to 1 above: dirt there with light 4 above it, and nothing opaque or fluid, turns to grass.
- **Farmland** with no water near and no crop on it goes to dirt in 1 tick of 8.

Two rules need no randomness, and run off a neighbour's change: wheat with no farmland under it goes, and farmland under something solid goes to dirt.

## Cost

One step with nothing to do looks at every column in range, 289 at most, and at the sections with more than one block type. The test prints what it measured.
