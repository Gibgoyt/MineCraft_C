# run/src/mob/creeper

**Purpose:** what a creeper does: strolls, notices the player, walks at it, runs its fuse, bursts, and leaves gunpowder if something else kills it first.
**Tier:** Arena. `mob_creeper_start` takes one array from the arena `mob` gives it, once per world. A tick allocates nothing.
**Owns:** nothing. `mob_creeper_t` is a field of `mob_t`.
**Lifetime:** World.
**Threads:** `sim`, or whichever one thread holds the map.
**Public functions** (`run/include/mob/creeper/index.h`):

- `mob_creeper_start`, `mob_creeper_seed_set`: nothing yet; where its numbers come from.
- `mob_creeper_tick`: one tick of every creeper in the store.

**May be called by:** `run/src/mob/` for the start, and `run/src/game/` for the tick, which comes after `mob_path_tick` and before `entity_tick`.
**Depends on:** `entity` (`store`, `item_drop`, `explosion`), `mob/path`, `player/movement`, `player/survival`, `item/stack`, `world/edit`, `terrain/rng`, `arena`, the generated `tables/mobs`, libm.

## What it does

The numbers are Minecraft's published ones, in blocks and ticks at 20 Hz. Speed, health and drops are in `config/mobs/creeper/index.yaml`.

| What | How |
|---|---|
| Sight | A straight line from its eye, 0.85 of its height, to the player's, by `explosion_line_clear`. A plant does not stop it |
| Noticing | With the player within 16 and in sight, a chance of 1 in 10 each tick |
| Giving up | Past 16, out of sight for over 60 ticks, or once the player is dead or waiting to come back |
| Chasing | By `mob_path` at full speed, to within 1 cell of the player. It asks again when the player's cell has changed or its way has run out, 4 to 11 ticks apart |
| Lighting | Chasing, in sight and nearer than 3. It stands still and looks at the player |
| Putting out | Further than 7, out of sight, or when it gives up. The fuse then runs back a tick at a time |
| The fuse | 30 ticks. `borrowed_swell` is its share, from 0 to 1, which is what `entity_animation` swells it by |
| Bursting | At 30, a blast of power 3 at the middle of the soles of its feet. It leaves nothing |
| Strolling | With nobody to chase, a chance of 1 in 120 each tick: to somewhere within 10 along x and y and 7 along z, at 0.8 of its speed |
| Dying | Health at 0, from a blast or anything else: it goes, leaving `mob_drops`' rows of it, which is 0 to 2 gunpowder |

**The player** is not in the store, so a burst hits it here, between the two halves of a blast: `explosion_blow`, then `player_survival_hurt` as `PLAYER_SURVIVAL_CAUSE_ATTACK`, so through armor, and the push on its velocity. A body dead or waiting to come back is left alone, and a sleeping one wakes.

**Only 16 bursts in a tick.** A blast of power 3 reaches under 6 cells, so writes at most 11 cubed blocks. So 16 of them cannot lap the edit log, whose readers read once a tick. The rest stay at the end of their fuses and go first thing the next tick.

**Same input, same bytes.** One stream of numbers from one seed, taken in the store's order from the end.

**A body waiting for its column** holds, fuse and all.

## Not here yet

- **It does not flash.** It is drawn as every mob is, by `run/src/game/`, and swells as its fuse runs. There is no white flash, and no particles or sound for a blast.
- **Other kinds with no health left stay in the store** until their own stages, 15.4, 15.5 and 15.7, take them away: see the stage file's Issues.
- **It does not run from cats, or turn on what hits it.** There are no cats, and being hit belongs to Stage 15.7.
