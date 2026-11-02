# run/src/entity/grid

**Purpose:** finding the bodies near a place without looking at all of them, and the shove between two that overlap.
**Tier:** Arena. `entity_grid_start` takes its two arrays from the arena `entity` gives it, once per world. A build, a query and a push allocate nothing.
**Owns:** nothing. `entity_grid_t` is a field of `entity_t`.
**Lifetime:** World.
**Threads:** `sim`, or whichever one thread holds the store. A query writes the grid's counters, so it stays on that thread too.
**Public functions** (`run/include/entity/grid/index.h`):

- `entity_grid_start`: an empty grid, from an arena.
- `entity_grid_build`: takes in where every body is now.
- `entity_grid_query`: every body alive whose box overlapped this one when built.
- `entity_grid_push_apart`: bodies that overlapped when built shove each other.

**May be called by:** `run/src/entity/`, which builds it once a tick, and what looks for bodies: `run/src/mob/`, arrows and explosions.
**Depends on:** `arena`, `entity/store`, libm.

## What it does

**A picture of one moment.** `entity_grid_build` copies each box. So an answer is of when it was built, however the bodies have moved since, and what one mob finds does not depend on who moved before it. `entity_tick` builds it at the end of a tick, so what a mob asks on the next one is where everyone ended this one. A body spawned since is not in it, and a body removed since is left out by its handle.

**Cells in a hash.** A body is in the 4-block cell of its box's middle. Cells are not kept: a 64-bit key of the cell goes through a multiply to one of 2048 buckets, each a chain through the entries, so a world's width costs nothing. A query walks every cell its box could find a body from, which is its box grown by `reach`, the most any box reaches from its middle, and in each it takes only entries of that very cell, since a bucket holds others. So each body is met once, however many cells share a bucket.

**A wide box is a list.** With more cells to walk than bodies, it looks at each entry instead. That is also why a key's limits never come up, and why a box with an infinity or a NaN gets an answer.

**The shove** is the published one. For two whose boxes overlap, `d` is the larger of their distances apart on x and on y. If `d` is under 0.01 they have no way to go, so the pair takes one from its slots, by the golden angle. Each gets `0.05 * (x, y) / sqrt(d)` on its velocity, opposite ways, and less past a block apart. Each body shoves at most 24 of those after it, so a heap of mobs costs a bounded tick. A pair with one waiting for its column is left alone.

**Why not `item_drop`'s grid?** That is built and walked for one question, merging, on its own stacks, in the order of a walk that removes. This one answers anyone, about bodies of any size, and at any moment of a tick.
