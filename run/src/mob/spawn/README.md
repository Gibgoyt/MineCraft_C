# run/src/mob/spawn

**Purpose:** which mobs come into the world where and when, by light level, surface, height, biome and distance, and which ones leave: hostile mobs in packs by dark, and animals in herds of both sexes by day, each under its own cap. What each mob's numbers are is in `config/mobs/`.
**Tier:** Arena (depth 2): `mob_spawn_start` takes one array from the arena its parent gives. Its parent is `run/src/game/`, whose `arena_world` it shares and whose `game_t` holds the `mob_spawn_t`: `entity`, a lower layer, may not include it. See Stage 15.3's Wiring and the phase's Issues.
**Owns:** nothing. The array is borrowed from the arena.
**Lifetime:** World.
**Threads:** the sim's, as the store is.
**Public functions** (`run/include/mob/spawn/index.h`):

- `mob_spawn_start`: empty, from a seed and an arena.
- `mob_spawn_tick`: despawn, then spawn. The one call a game makes per tick.
- `mob_spawn_check`: what the rules make of one place.
- `mob_spawn_body`, `mob_spawn_body_as`: a body of a mob, by no rule: what anything that makes a mob goes through. The first rolls an animal's sex, half and half; the second is told it.
- `mob_spawn_has_sexes`, `mob_spawn_biome_allows`: whether a mob has a male and a female, and whether its `spawn.biomes` lets it come in a biome.
- `mob_spawn_sky_darken`, `mob_spawn_cap`, `mob_spawn_animal_cap`: the rules' numbers.
- `mob_spawn_retire`: takes away every mob whose `spawn.weight` is 0, as a world opens from a save (plan 005 phase 3).

**May be called by:** `run/src/game/`, `run/src/save/state/`, and `bench/mobs/`.
**Depends on:** `entity/store`, `physics`, `world/map`, `world/column`, `world/section`, `terrain/rng`, `terrain/biome`, `arena`, `assert`, and the generated `tables/mobs/` and `tables/blocks/`.

## The spawn rules

These are the hostile mobs' rules; animals' are in "Animals: herds by day" below. Each tick tries `MOB_SPAWN_TRIES_PER_TICK` places, until the cap is reached. If the hostile weights add up to 0, as they do since plan 005 phase 3 turned off zombies, skeletons and creepers, it tries none: it only despawns, and draws nothing from its random stream. The published game looks at a place per chunk per tick and most fail, so this is the same search at a rate a tick can afford.

1. A random place within `MOB_SPAWN_REACH` of the player along x and y, in a loaded column, at a random height among the lowest 751 layers of gameplay's range, or up to the top of what the column holds if it reaches higher, so that empty sky in a taller world costs no tries. Inside something solid is a miss.
2. Down from there to the first floor, at most `MOB_SPAWN_SCAN_CELLS`. That the published game looks only at the height it rolled, and misses, is what this saves.
3. A mob by `weight`, or by `mob_spawn_view_t.weights` when that is not `nullptr` (the game passes `nullptr`; the tests and nothing else pass the old weights, to keep the rules tested while the files turn every mob off), and `pack_min` to `pack_max` of it, each a step of up to `MOB_SPAWN_PACK_STEP` from the last at that same height. Each goes through `mob_spawn_check` by itself, so a pack can come partly or not at all.

`mob_spawn_check` asks of one body's place, in this order, and the answer says which failed:

| Rule | What passes |
|---|---|
| **Distance** | From `spawn.min_distance` to `despawn.far` of the player, in three dimensions, so that nothing comes in sight or goes at once. No far for a passive mob |
| **Loaded** | Every column its box is over |
| **Surface** | Under its feet is a whole, solid, opaque block, not on its `never_on`. Glass, leaves, slabs, plants, fluid and air do not do. Neither does the bottom of the world |
| **Light** | Block light at its feet is at most `max_block_light`. The greater of that and what the sky gives (sky light less `mob_spawn_sky_darken`) is at most a roll from 0 to `max_light`, so the darker the likelier |
| **Room** | Nothing solid in its box |
| **Fluid** | No water or lava in its box |

The sky's share uses the published curve, `11 (1 - clamp(2 s + 0.5, 0, 1))`, where `s` is the sine of the sun's elevation at the player (`mob_spawn_view_t.sun_height`, plan 004 phase 02): nothing off with the sun well up, 11 off with it 14.5 degrees or more below the horizon, and open ground dark enough for a zombie once it is 6.5 degrees down. On the equator at the equinox `s` is `cos(2 pi (f - 0.5))` of the time of day `f`, so there it is the old curve exactly: nothing off at noon, 11 at midnight. Elsewhere nights are long in winter and near the poles, and in polar winter an open field fills at noon. A mob spawns at most 128 blocks from the player, 0.3 degrees of latitude, so the player's sun stands for all of them. A cave fills at any hour, and a torch keeps a patch clear for good.

## The cap

`mob_spawn_cap`: `MOB_SPAWN_CAP` at `MOB_SPAWN_CAP_COLUMNS` loaded columns or more, in proportion below that, as in the published game. Only kinds that are a `mob_id_t` count. So a short render distance has fewer about, and the store's 1024 is never what stops them.

70 is the published game's cap for monsters, and counts only hostile mobs. Its animals have a cap of their own, 10, as they do not despawn and so pile up, and so do they here (plan 005 phase 4): `mob_spawn_animal_cap` is `MOB_SPAWN_ANIMAL_CAP` at `MOB_SPAWN_CAP_COLUMNS` loaded columns, in proportion below, and counts only passive mobs in loaded columns, not one waiting for its column. Neither cap stops the other's kind.

## Animals: herds by day

A mob whose file says `mind: passive` comes by these, after the hostile tries, and only if the passive weights add up to more than 0 (so none is drawn from the random stream until one does):

- **When:** on a tick with a chance of one in `MOB_SPAWN_ANIMAL_ONE_IN`, as the published game looks every 400 ticks; **on the world's first day every tick** (`mob_spawn_view_t.first_day`, which `run/src/game/` sets while the world clock is in its first day), so that a few herds are about at once, for a bed.
- **Where:** `MOB_SPAWN_ANIMAL_TRIES` columns within `MOB_SPAWN_REACH` along x and y, or `MOB_SPAWN_FIRST_DAY_REACH` on the first day, down from the top of the column's highest section that holds anything to its first floor: the ground under the sky, or a tree's leaves, which then fail the surface rule.
- **What:** a passive mob by weight, and `pack_min` to `pack_max` of it, each a step from the last, as a pack. **A herd of a mob with sexes is male first, female second and then either, half and half**, so every herd of two or more has both. Everywhere else `mob_spawn_body` rolls a sex half and half; a body's box is its sex's `scale` times the mob's.
- **The light roll** is the mob's `max_light`, not a roll from 0, so daylight is not less likely than dusk; `min_light` is what keeps it to the day. On the first day it may come as near as `MOB_SPAWN_FIRST_DAY_NEAR`, where its `min_distance` is more.
- **Never taken away:** a passive mob has no `despawn`, its `despawn_far` is 0, and the despawn rules pass it by. `mob_spawn_retire` still takes one whose weight is 0, as the stand-in pig's is until stage 3.

The rules `mob_spawn_check` adds for any mob that sets them, in the order below, after the surface:

| Rule | What passes | Status |
|---|---|---|
| **On** | With `spawn.on`, only those blocks under its feet: grass for an animal of the fields | `MOB_SPAWN_BAD_SURFACE` |
| **Height** | Its feet from `spawn.min_height` to `max_height` blocks above the sea: high up for a mountain goat | `MOB_SPAWN_BAD_HEIGHT` |
| **Biome** | With `spawn.biomes`, the biome of its column (`terrain_biome_column_at` on `mob_spawn_view_t.biomes`) is one of them. A view with no biomes, as in tests of something else, lets any | `MOB_SPAWN_BAD_BIOME` |
| **Too dark** | The light (as for **Light**) is at least `spawn.min_light` | `MOB_SPAWN_TOO_DARK` |
| **Room** | Checked for its larger sex's box | `MOB_SPAWN_NO_ROOM` |

So each animal of plan 005 says where it lives in its own file: cattle, sheep, pigs and chickens `biomes: [plains, sunflower_plains, meadow, forest, flower_forest, birch_forest, savanna]` and `on: [grass_block]`, a forest's edge being its grass under open sky; goats `biomes: [windswept_hills, windswept_gravelly_hills, windswept_forest, windswept_savanna, grove, meadow]`; mountain goats `biomes: [stony_peaks, jagged_peaks, frozen_peaks, snowy_slopes]` with a `min_height` and `on` stone and snow.

## The despawn rules

Each tick, for each hostile mob, by `despawn` in its file:

- Past `far` of the player: gone, whatever else. A save keeps the mobs the store holds at that moment, through `run/src/save/state/`, and not one that has gone.
- Within `near`: its idle count starts over, and it stays.
- Between: its idle count goes up, and once past `idle_ticks` it has a chance of one in `one_in`, per tick, of going. A body waiting for its column neither counts nor rolls, since it is not there.

## Turned off

`mob_spawn_retire` takes away every body whose kind is a mob with `spawn.weight` 0, from the end of the store as despawning does, and counts it by kind; nothing that is not a mob is touched, and nothing is counted as despawned. `game_open` calls it after `save_state_apply`, and logs a line for each kind it took, so that a world saved while zombies, skeletons and creepers came does not keep them.

## What this does not do

A blow does not start the idle count over, which the published game does: Stage 15.7 can write `borrowed_idle` by the slot. Nothing breeds animals (Ahmed: not in plan 005). Animals left far behind stay in the store, waiting for their columns, and a long journey can fill its 1024: see plan 005 phase 4's Issues.
