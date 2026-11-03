# run/src/world/light

**Purpose:** sky light and block light: flood fill across sections and columns, and incremental updates on edit.
**Tier:** Arena (depth 2): `world_light_init` takes once from an `arena_t *` the Owner gives. Section arrays come from the section pool, through `section/`.
**Owns:** nothing. It holds its tables as `borrowed_` from the arena.
**Lifetime:** World. `world_open` starts it and `world_close` ends it.
**Threads:** one at a time, the one holding the map: `sim`. See "Threads" below.
**Public functions** (`run/include/world/light/index.h`):

- `world_light_init`, `world_light_deinit`: the tables.
- `world_light_column_added`, `world_light_column_leaving`: a column's light as it comes and goes.
- `world_light_update`: every edit in the log since the last call.
- `world_light_sections_replaced`: for blocks written past the log.
- `world_light_rebuild`: everything from nothing, with no queue.

**May be called by:** `run/src/world/`, and `game`, through `world`'s header.
**Depends on:** `world/section`, `world/column`, `world/map`, `world/edit`, `arena`, `assert`, and the generated `tables/blocks/`.

## The rules

Each voxel holds one number per channel, 0 to 15. Light is the **least** set of numbers for which all this holds, so there is exactly one answer, and the order things arrived in cannot matter:

- Going into a voxel from a neighbour costs its `opacity`, or 1 if that is 0. A voxel of opacity 15 passes nothing on.
- **Block light** in a voxel is at least what its block gives off, even in a block of opacity 15: a lit furnace shines out.
- **Sky light** falls from above the top of storage at 15. Sky at 15 going straight **down** into a voxel of opacity 0 stays 15, and that is the only move that costs nothing. Below the bottom of storage there is nothing.
- Only columns told of by `world_light_column_added` count. There is nothing beyond them, so a neighbour that arrives later can only add light, and one that leaves takes its share with it.

Storage's whole height is lit, not only gameplay's, since a section does not know where gameplay stops.

## What a block holds

Air is not on the card, so the byte a ray finds is the one in the voxel it stopped in, and `cuda/src/shade/` lights that voxel's faces with it: see "The light on a surface" there. So an opaque block holds **what its faces see, the brightest of its neighbours that are not opaque**, at no cost, per channel. Past the top of storage counts as a neighbour holding 15 of sky.

That number is for showing only. It goes no further: a neighbour takes nothing from an opaque block but what the block gives off, so light does not leak through a wall. The one exception is an opaque block that gives off block light: in that channel it holds what it gives off and nothing else, as otherwise light that came from it could not be told from light its faces saw. A lit furnace beside a brighter torch shows its own 13.

One byte serves all six faces, so a wall one block thick shows its brighter side's light on its dark side as well. The sun ray still knows better. Stage 09.2 chose this over putting air on the card.

## A pass

`light_pass` does this, once per channel, for a set of voxels that may be wrong (the seeds):

1. **Dark.** Each seed goes to 0. Then, from each voxel gone dark, any neighbour holding exactly what this one gave it goes dark as well. A neighbour holding more got it elsewhere and is left alone. So every voxel still lit got its value from a voxel still lit. No cycle can hold itself up: every move but one loses light, and that one only goes down. An opaque neighbour whose faces saw exactly this much goes dark too, and spreads no dark, since nothing leans on it.
2. **Look.** Each voxel gone dark, or merely asked about, takes the best of what it gives off and what its neighbours offer. An opaque block takes what its faces see.
3. **Bright.** Whatever got brighter offers it on, the brightest first, so that a voxel is rarely written twice. An opaque neighbour's faces see it, and it stops there.

The cost goes by the voxels that really change, plus six looks each. One block costs what its light reached, not what the world holds.

Every call first takes all the log has, as one pass per channel, however many records, and then does what it was called for. So no pass ever meets a block whose record it has not read: once a block is opaque, what it holds says what its faces saw, and that could not be told from what its neighbours had been given by what was there before.

A pass has fixed room. If it runs out, it stops, and `world_light_rebuild` works everything out again: correct, and in that one call slow, with `overflow_count` saying so. So does a log that lost records (`lost_count`). A rebuild needs no room, because it sweeps every section with anything to work out, forward and back, until nothing changes.

Running out of the section pool is not fatal. The status says so, the voxels that would not fit stay wrong, and the first `world_light_update` after the pool holds less than that call left it holding rebuilds.

## A new column

Falling sky comes first, one section at a time from the top: a section of air under full sky, or a solid one, takes one value and no array. Then a pass runs that makes nothing dark and looks only where something could change. A section whose light is one value is skipped if nothing beside it could raise it. That leaves the few sections around the surface and caves.

## Flags

A call writes to the sections, and at its end flags the ones it wrote for RENDER and has `section_compact` look at them, which is how light that has settled on one value gives its array back. Nothing here goes in the edit log, and nothing is flagged for SAVE, since light is worked out again on load.

## Threads

`plans/REPO_STRUCTURE.md` lists `world/light` among the workers' jobs. A section's light may need an array from the pool, and a pool has one thread at a time, so all of this runs on the thread holding the map. The stage file has what that costs.
