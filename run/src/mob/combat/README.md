# run/src/mob/combat

**Purpose:** what hurts a mob and what comes of it: damage, the moment after a hit when it cannot be hurt again, knockback, falls and lava. Its death is the health reaching 0, which this does and counts; the body's own mind (`zombie`, `skeleton`, `creeper`) then takes it away on its next tick and leaves what it drops.
**Tier:** Arena (depth 2): `mob_combat_start` takes one array from the arena its Owner gives.
**Owns:** nothing. The array is borrowed from the arena.
**Lifetime:** World: `mob_open` starts it and `mob_close` ends it.
**Threads:** the sim's, as the store is.
**Public functions** (`run/include/mob/combat/index.h`):

- `mob_combat_start`: nobody hurt yet, from an arena.
- `mob_combat_hurt`: a hit on a body at an index.
- `mob_combat_knockback`: a push on a body at an index.
- `mob_combat_strike`: a blow on a handle: the hit, and its pushes.
- `mob_combat_tick`: one tick of every body, after they have moved.

**May be called by:** `run/src/mob/` for the start, `run/src/game/` for the rest, and tests.
**Depends on:** `entity/store`, `arena`, `player/survival` for the sum a fall comes to, libm.

## The numbers

| Name | Value | Published as |
|---|---|---|
| `MOB_COMBAT_INVULNERABLE_TICKS` | 10 | A hurt body's 20 ticks of `invulnerable_time`, in the half of them that count |
| `MOB_COMBAT_KNOCKBACK` | 0.4 | A living thing's own knockback from a hit, blocks per tick |
| `MOB_COMBAT_KNOCKBACK_UP_MAX` | 0.4 | The most a push lifts it, and only from the ground |
| `MOB_COMBAT_LAVA_DAMAGE` | 4 | Lava's hit |
| a fall | what `player_survival_fall_damage` says | 3 blocks safe, then one half heart a block, rounded up |

## How a hit goes

1. A body with no health, or a hit of no size, is nothing.
2. **A whole hit**, with none in the last 10 ticks, takes its size, starts the 10 ticks and pushes the body. It remembers its size.
3. **Inside the 10 ticks**, a hit no bigger than the last does nothing, and a bigger one takes the difference and does not push, though a blow's own `knockback`, a sprint's, still does.

A push halves the speed it had across and adds its strength away from the striker. On the ground it also sets its upward speed to half of what that was plus the strength, up to 0.4, which is what lifts it into the air, where there is less friction and it goes further. In the air it does not lift it. Two pushes in a blow, as with a sprint, go one after another, as published.

## What this does not do

- **Arrows and blasts** take health directly, in `run/src/entity/projectile/` and `run/src/entity/explosion/`, as they did before this stage, so the 10 ticks do not stand against them, nor do those hits start them.
- **Burning, drowning, cactus and suffocating** do not hurt mobs. Falls and lava do.
- **There is no death animation.** A body goes on the next tick its mind has, with no time lying down.
- **Being hit does not make a mob chase.** A mob near enough to be hit nearly always notices within a few ticks, by its own mind's rules.
