# run/src/mob/spawn

**Purpose:** which mobs come into the world where and when, by light level, surface and distance, and which ones leave. What each mob's numbers are is in `config/mobs/`.
**Tier:** Arena (depth 2): `mob_spawn_start` takes one array from the arena its parent gives. Its parent is `run/src/game/`, whose `arena_world` it shares and whose `game_t` holds the `mob_spawn_t`: `entity`, a lower layer, may not include it. See Stage 15.3's Wiring and the phase's Issues.
**Owns:** nothing. The array is borrowed from the arena.
**Lifetime:** World.
**Threads:** the sim's, as the store is.
**Public functions** (`run/include/mob/spawn/index.h`):

- `mob_spawn_start`: empty, from a seed and an arena.
- `mob_spawn_tick`: despawn, then spawn. The one call a game makes per tick.
- `mob_spawn_check`: what the rules make of one place.
- `mob_spawn_body`: a body of a mob, by no rule: what anything that makes a mob goes through.
- `mob_spawn_sky_darken`, `mob_spawn_cap`: two of the rules' numbers.

**May be called by:** `run/src/game/`, `run/src/save/state/`, and `bench/mobs/`.
**Depends on:** `entity/store`, `physics`, `world/map`, `world/column`, `world/section`, `terrain/rng`, `arena`, `assert`, and the generated `tables/mobs/` and `tables/blocks/`.

## The spawn rules

Each tick tries `MOB_SPAWN_TRIES_PER_TICK` places, until the cap is reached. The published game looks at a place per chunk per tick and most fail, so this is the same search at a rate a tick can afford.

1. A random place within `MOB_SPAWN_REACH` of the player along x and y, in a loaded column, at a random height in gameplay's range. Inside something solid is a miss.
2. Down from there to the first floor, at most `MOB_SPAWN_SCAN_CELLS`. That the published game looks only at the height it rolled, and misses, is what this saves.
3. A mob by `weight`, and `pack_min` to `pack_max` of it, each a step of up to `MOB_SPAWN_PACK_STEP` from the last at that same height. Each goes through `mob_spawn_check` by itself, so a pack can come partly or not at all.

`mob_spawn_check` asks of one body's place, in this order, and the answer says which failed:

| Rule | What passes |
|---|---|
| **Distance** | From `spawn.min_distance` to `despawn.far` of the player, in three dimensions, so that nothing comes in sight or goes at once |
| **Loaded** | Every column its box is over |
| **Surface** | Under its feet is a whole, solid, opaque block, not on its `never_on`. Glass, leaves, slabs, plants, fluid and air do not do. Neither does the bottom of the world |
| **Light** | Block light at its feet is at most `max_block_light`. The greater of that and what the sky gives (sky light less `mob_spawn_sky_darken`) is at most a roll from 0 to `max_light`, so the darker the likelier |
| **Room** | Nothing solid in its box |
| **Fluid** | No water or lava in its box |

The sky's share uses the published curve: nothing off at noon, 11 off at midnight. So an open field fills at night, a cave at any hour, and a torch keeps a patch clear for good.

## The cap

`mob_spawn_cap`: `MOB_SPAWN_CAP` at `MOB_SPAWN_CAP_COLUMNS` loaded columns or more, in proportion below that, as in the published game. Only kinds that are a `mob_id_t` count. So a short render distance has fewer about, and the store's 1024 is never what stops them.

## The despawn rules

Each tick, for each mob, by `despawn` in its file:

- Past `far` of the player: gone, whatever else. A save keeps the mobs the store holds at that moment, through `run/src/save/state/`, and not one that has gone.
- Within `near`: its idle count starts over, and it stays.
- Between: its idle count goes up, and once past `idle_ticks` it has a chance of one in `one_in`, per tick, of going. A body waiting for its column neither counts nor rolls, since it is not there.

## What this does not do

Biomes do not say which mobs come or stop any, and nothing persists a mob. A blow does not start the idle count over, which the published game does: Stage 15.7 can write `borrowed_idle` by the slot.
