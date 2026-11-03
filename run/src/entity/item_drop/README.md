# run/src/entity/item_drop

**Purpose:** dropped items: a stack lying in the world, how it moves, how two merge, and how one is picked up.
**Tier:** Arena. `item_drop_store_start` takes its three arrays from the arena `entity` gives it, once per world. A tick and a pickup allocate nothing.
**Owns:** nothing. `item_drop_store_t` is a field of `entity_t`.
**Lifetime:** World.
**Threads:** `sim`, or whichever one thread holds the map. `run/src/game/` copies them into `run/src/render/entities/` on that thread, so nothing here is read from another.
**Public functions** (`run/include/entity/item_drop/index.h`):

- `item_drop_store_start`: an empty store, from an arena and a seed.
- `item_drop_spawn`, `item_drop_spawn_worn`: items thrown out of a broken block; the same for what has been used, which is as worn when picked up again.
- `item_drop_tick`: one tick of every stack.
- `item_drop_pickup`, with an `item_drop_take_t`: gives a taker what lies in reach, and removes what it takes.
- `item_drop_box`: a stack's box at a place.
- `item_drop_on_ground`: how many items are lying there.

**May be called by:** `run/src/entity/`, `run/src/entity/explosion/` and `run/src/mob/zombie/`, which drop things, and `run/src/game/`, which brings the player's drops and its reach to it. It knows nothing of the player: what takes an item is a function it is handed.
**Depends on:** `physics`, `world/map`, `world/column`, `terrain/rng`, `arena`, `assert`, and the generated `tables/blocks`.

## What it does

The numbers are Minecraft's published ones, in blocks and ticks at 20 Hz. z is up.

| What | How |
|---|---|
| Box | 0.25 on a side. Its position is its middle |
| Coming out of a block | Within 0.25 of the cell's middle on each axis, so still inside the cell. Up at 0.2, and up to 0.1 either way sideways, from the store's own random numbers |
| Falling | 0.04 is taken off z's speed, it moves, then all speeds are kept by 0.98 |
| On the ground | Sideways speed is kept by 0.98 times what is under it: 0.6, or 0.98 on ice and packed ice, or 0.989 on blue ice. It lands without a bounce |
| Lying still | Sideways speed squared under 1e-5, on the ground: it looks at the world only every 4th tick, on one of its own. So it can take that long to notice that what it lay on has gone |
| In water | Water over 0.1 above its bottom: no gravity, sideways kept by 0.99, and up by 0.0005 a tick while under 0.06. It comes up and bobs at the top. Flowing water does not carry it |
| Under a block | If a block now overlaps it and its middle is in that block, it goes 0.2 a tick by the nearest face with nothing solid behind it, or up if none has, straight through whatever is there. Once its middle is in an open cell, it moves wholly inside that, so nothing overlaps it, and falls as usual |
| Lava or a cactus | Touching one ends it, and it is counted in `destroyed_count` |
| A column not loaded | A box over the line waits, and does not age. Otherwise the edge is a wall, as for everything else that moves |
| Old age | Gone at 6000 ticks, in `despawned_count` |
| Merging | Every 2nd tick. Two of one item, neither full, with middles under 0.75 apart on x and on y and under 0.25 on z. The smaller goes into the larger as far as it fits, up to 64, and what is left stays where it was. The result has the younger age and the longer wait |
| Picking up | Once its 10 ticks of waiting are over, when its box overlaps the reach it is offered. What the taker does not take stays |
| A full store | Holds 4096 stacks. The oldest goes to make room for a new one, in `lost_count` |

`dropped_count` always equals `picked_up_count + despawned_count + destroyed_count + lost_count` plus what is on the ground. The test holds it to that.

**Merging is not every pair against every other.** Each pass puts the stacks in a grid of 4096 buckets, by the cell of each one's middle, and looks in the 27 cells around each. A reach is under a cell on every axis, so whatever one can reach is in one of them. Cells that share a bucket only cost a look. After one pass no two that could merge are left in reach, and the test checks that against every pair.

**Same input, same bytes.** Everything is `float64_t` arithmetic in a fixed order with numbers from one seed. Stacks are removed by moving the last one into the gap, always walking from the end, so the order depends on nothing but what happened.

## What is elsewhere

- **Drawing them** is `cuda/src/entity_rays/`'s, from what `run/src/game/` hands `render_entities_add_item_drop`.
- **Who takes an item.** With an inventory the player's taker is `inventory_add_stack`, in `run/src/game/`. With none, as in tests of the hands alone, it is `player_interaction_hand_take`: an item that is no block, or another block than the hand holds, stays on the ground.
