# run/src/entity/explosion

**Purpose:** ray-based explosions: what a blast breaks, how it hurts and pushes bodies and items, and how much a wall shields them.
**Tier:** Arena. `explosion_start` takes its two arrays from the arena `entity` gives it, once per world. A blast allocates nothing.
**Owns:** nothing. `explosion_t` is a field of `entity_t`.
**Lifetime:** World.
**Threads:** `sim`, or whichever one thread holds the map.
**Public functions** (`run/include/entity/explosion/index.h`):

- `explosion_start`: no blast yet, from an arena and a seed.
- `explosion_resistance`: how much of a blast a block stands against.
- `explosion_line_clear`: whether anything a body collides with is on a straight line.
- `explosion_exposure`: how much of a box a blast reaches.
- `explosion_blow`: what a blast does to one body.
- `explosion_begin`: finds what breaks, and hits the store's bodies and the items lying near.
- `explosion_finish`: breaks it.

**May be called by:** `run/src/entity/` for the start, and `run/src/mob/` and anything else that blows up. `explosion_line_clear` is also how a mob sees.
**Depends on:** `entity/store`, `entity/item_drop`, `physics/collision`, `world/map`, `world/edit`, `world/wall_torch`, `world/bench`, `terrain/rng`, `arena`, `assert`, the generated `tables/blocks`, libm.

## What it does

The numbers are Minecraft's published ones, in blocks and half hearts, and z is up. A creeper's power is 3.

**Two halves, because of the order.** Everything is worked out with the world as it stood: a body is shielded by the wall the same blast is about to break. So `explosion_begin` finds the cells and hits what the store holds, whoever owns other bodies (the player, for now) calls `explosion_blow` for each, and `explosion_finish` breaks the blocks. What comes out of them comes after, so a blast does not destroy its own drops.

**What breaks.** 1352 rays go out from the middle, one through each cell on the faces of a cube of 16. Each starts with `power * (0.7 + 0.6 * random)` and walks in steps of 0.3. In each step, any block that is not air takes `(resistance + 0.3) * 0.3` off it, and if any is left the block is on the list, once; then the step itself takes off 0.225. A column not loaded ends the ray, and outside gameplay's heights it goes on as through air.

**Resistance.** The published number. Most blocks' is their hardness, so only the others are written down, in a `switch` in `index.c`. Bedrock is 3600000, and water and lava 100, which no blast this size gets through, so fluids stay and what is under water is safe. Plants are 0: a ray only pays the 0.3 for them.

**What goes with it.** What stood on a broken block and needs the ground goes too, as for mining: cross, crop and torch shapes, up to 16 high, and any torch on one of its four sides, wherever the blast reached. Falling sand, flowing water and the spilling of a chest follow from the edit log, as they do for anything that changes a block.

**What comes out.** One block in `power` gives its drops, rolled as for a hand but with no tool asked for. A torch on a wall always gives its torch, whether the blast took it or the block it hung from.

**A bench is two blocks that are one.** Either half on the list takes the other with it, and what stood on or hung from that one too, and the one on the list always gives the one bench, so a blast never leaves half of one or loses the only one a player made. If the other half's column is not loaded, the half stays: see `run/src/world/bench/`.

**What it does to a body** with its feet `dist` from the middle, with `reach = 2 * power`:

| What | How |
|---|---|
| Out of reach | `dist > reach` |
| Exposure | The share of a grid of points on its box with a clear line to the middle. Spacing on each axis is `1 / (2 * size + 1)`, which for a player gives 3 by 3 by 5. Plants and fluids do not shield; what collides does, and so does a column not loaded |
| Impact | `(1 - dist / reach) * exposure` |
| Damage | `floor((impact * impact + impact) / 2 * 7 * reach + 1)`. So behind a wall, in reach, it is still 1 |
| Push | `impact`, from the middle towards its eye, added to its velocity |

A body's eye is at 0.85 of its height. A body with `ENTITY_STORE_FLAG_WAITING` is not there to be hit. Health stops at 0, and whoever owns a body takes it away: `run/src/mob/creeper/` does for creepers.

**Items** have 5 health and take one blow: at 5 or more they go, in `destroyed_count`, and otherwise they fly. The store's sums still add up.

**Same input, same bytes.** Plain `float64_t` arithmetic in a fixed order, and random numbers from one seed. The list and the removal order depend on nothing else.

**The list is bounded.** The strongest blast asked for has power 8, which is under 15 cells from its middle cell, so 31 cubed cells in all. Pages the list does not reach cost nothing. A creeper's reaches about 6 cells.

## What it does not do

- **Blocks have no `blast_resistance` field.** `config/blocks/` and its generator are not this stage's. See the stage file's Issues.
- **Nothing is drawn or heard.** There is no particle system or sound in the game.
- **TNT does not go off:** a blast breaks it like any other block, with resistance 0.
