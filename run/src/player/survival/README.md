# run/src/player/survival

**Purpose:** health, hunger, food, damage, death, respawn and the bed: what keeps a body going, and what happens when nothing does.
**Tier:** Arena. Takes nothing, as a tier may allocate less than it is allowed: all it needs is in `player_survival_t`.
**Owns:** nothing.
**Lifetime:** Process for the struct, inside `player_t`. `player_survival_start` starts it for a world.
**Threads:** the one holding the map, which is `sim` in a real run. Main sees `player_vitals_read` in `run/src/player/`.
**Public functions** (`run/include/player/survival/index.h`):

- `player_survival_start`: full health and food, with the place to come back to.
- `player_survival_buttons`: before the hands' tick: a press brings it back or wakes it, the right button uses a bed or eats, and what is left goes to the hands.
- `player_survival_tick`: after the body's tick: what it touches hurts, food wears down, it heals or starves, and it sleeps on.
- `player_survival_hurt`: a hit, from here or from outside, such as Stage 15's mobs and explosions.
- `player_survival_eat`, `player_survival_exhaust`: the two rules of food by themselves.
- `player_survival_hold_food`, `player_survival_day_set`, `player_survival_armor_set`: what it is told, as it knows nothing of items or the sky.
- `player_survival_after_armor`, `player_survival_fall_damage`, `player_survival_is_night`: the sums, for any numbers.
- `player_survival_holds_body`, `player_survival_can_sprint`: what `run/src/player/` does to the keys.
- `player_survival_died_take`, `player_survival_morning_take`, `player_survival_armor_hit_take`: what `run/src/game/` acts on: what was carried spills, the day moves on, and what is worn wears.

**May be called by:** `run/src/player/`, `run/src/game/` for what it is told and what it acts on, and `run/src/mob/combat/` and `run/src/mob/creeper/`, which call `player_survival_hurt`.
**Depends on:** `physics`, `world/map`, `player/camera` and `player/movement` for their types, and the generated `tables/blocks`.

## The rules

Minecraft's published rules for its normal difficulty, written here from scratch.

| What | Rule |
|---|---|
| Start, and after coming back | 20 health, 20 food, 5 saturation, 300 ticks of air |
| Tiring | Sprinting on the ground 0.1 a block; in water 0.01 a block; a jump 0.05, 0.2 on a sprint; a block mined 0.005; a hit from lava, a cactus or outside 0.1 |
| Worn down | Past 4 tiredness, 4 of it goes and takes one saturation, or with none left one food |
| Healing | At 20 food with saturation: every 10 ticks up to 1 health, for as much as 6 saturation's worth of tiredness. At 18 or more: 1 health every 80 ticks, for 6 tiredness |
| Starving | At 0 food: 1 health every 80 ticks, down to 1. Hunger alone does not kill |
| No sprint | At 6 food or less, or while eating |
| Eating | The right button held for 32 ticks with food in hand and under 20 food: food and saturation as in `config/items/`, with saturation never over food. It goes on to the next one if held. Letting go or changing what is in hand starts over. A stew leaves its bowl |
| Falling | `distance - 3`, rounded up, on landing. Half that on a bed. Water and ladders break a fall |
| Lava | 4 a hit, and alight for 15 s after |
| Alight | 1 every 20 ticks. Water puts it out |
| Cactus | 1 a hit, touching it from any side |
| Drowning | 300 ticks of air with the eyes under, then 2 every 20 ticks. Back at 4 air a tick |
| Smothering | 1 a hit, with the eyes in a full cube that stops light |
| One hit at a time | For 10 ticks after a hit, only what is more than that hit counts, and only by the difference. So lava is 4 every 10 ticks |
| Armor | Helps against lava, cactus and outside hits: `hit * (1 - min(20, max(points / 5, points - hit / (2 + toughness / 4))) / 25)`. `run/src/game/` tells it what is worn each tick, and each piece loses uses for every such hit |

Falling out of the world cannot hurt, as `physics/collision` makes below the world bedrock.

## Death and coming back

At 0 health the body is dead. It keeps falling, and nothing it is told moves it. `run/src/game/` spills what it carried and wore, once, where it lies, each tool as worn as it was, and it picks nothing up. After 20 ticks a press of either mouse button or jump, not one already held, brings it back with everything full.

**Where.** The last bed it used, or otherwise where the world started it. The ground there may not be loaded, so it holds still until it is. Then a bed that has gone counts for nothing, and it goes to the world's place. It stands beside a bed if it can and on it if not, and at the world's place it goes up as far as it takes to fit.

There is no death screen: the view lowers to the ground and stays until a press, with a line across the top of the HUD saying what to press, which `run/src/game/` draws.

## The bed

`config/blocks/bed/`: one cell long, half a block high, because blocks here have no facing and no block is made of two cells.

The right button on one, within 3 blocks across and 2 up of it, and not sneaking, makes it the place to come back to. A sneak places against it instead, as for any other block. If it is night, from 12542 to 23459 ticks after sunrise, the body also lies on it with its eye low. After 100 ticks asleep `player_survival_morning_take` says so, the day goes to sunrise, and it stands beside the bed. Any press wakes it early, with no morning. So does the bed going, where it stays and falls, and a hit, where it stays on it.

Monsters nearby do not stop it.
