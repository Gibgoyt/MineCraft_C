# run/src/player/combat

**Purpose:** the player's attack: what the left button aims at among the world's bodies, whether it is a press, how strong the swing is, and what it comes to. It does not hurt anything itself: it says what the swing does, and `run/src/game/` gives that to `run/src/mob/combat/`.
**Tier:** Arena by its depth. Takes nothing, as a tier may allocate less than it is allowed: all it needs is in `player_combat_t`.
**Owns:** nothing.
**Lifetime:** Process for the struct, inside `player_t`. `player_combat_start` starts it for a world.
**Threads:** the one holding the map, which is `sim` in a real run.
**Public functions** (`run/include/player/combat/index.h`):

- `player_combat_start`: a hand that has just come up.
- `player_combat_attack_speed`: full swings a second with an item.
- `player_combat_strength`: how far ticks since a swing have got back to a full one.
- `player_combat_aim`: the nearest body along a line.
- `player_combat_tick`: one tick of the hand.
- `player_combat_landed`: what a swing's landing does to the striker's own body.

**May be called by:** `run/src/player/`, and tests.
**Depends on:** `entity/store`, its siblings `player/movement` for the body and `player/camera` for the line the eye looks along, `physics/raycast`, `world/map`, `machine`, the generated `tables/blocks` and `tables/items`, libm.

## The numbers

These are the published ones of the game whose damage numbers `config/items/` holds, where a swing has to come back.

| Name | Value | Published as |
|---|---|---|
| `PLAYER_COMBAT_REACH` | 3 | Reach at a body, from the eye. Less than a block's 4.5 |
| Sword | 1.6 swings a second | `attack_speed`, of whatever it is made |
| Shovel | 1 | |
| Pickaxe | 1.2 | |
| Axe | 0.8 wood, stone; 0.9 iron; 1 gold, diamond | |
| Hoe | 1 wood, gold; 2 stone; 3 iron; 4 diamond | |
| `PLAYER_COMBAT_HAND_SPEED`: a hand, or anything else | 4 | |
| Strength | (ticks + 0.5) / (20 / speed), up to 1 | So a sword is back to full after 12 ticks |
| `PLAYER_COMBAT_WEAKEST` | 0.2 | Damage is its own × (0.2 + 0.8 × strength²) |
| `PLAYER_COMBAT_FULL_OVER` | 0.9 | Strength over this counts as full |
| `PLAYER_COMBAT_CRITICAL` | 1.5 | Full, falling, not on the ground, not on a ladder, not in water, and not sprinting |
| `PLAYER_COMBAT_SPRINT_KNOCKBACK` | 0.5 | Full, at a sprint: one level of knockback at 0.5 a level |
| `PLAYER_COMBAT_SPRINT_SLOWS` | 0.6 | What is left of the sprinter's own speed across |
| `PLAYER_COMBAT_TIRES` | 0.1 | Exhaustion for a hit that lands |

## How a click goes

Every tick the swing's strength gets a tick back, unless what is in hand has changed, which starts it over.

- **A press on a body** swings, once. Holding the button does not swing again.
- **A press at the air** wastes the swing: strength starts over, as published.
- **A press on a block** is mining's, and costs nothing here.
- **The button held on a body** sets `aiming_body`, which keeps `player_interact_tick` from mining what is behind it. Move off the body with the button still held, and mining starts.
- **A dead or sleeping body** does not hit. The button still counts as held, so that holding on past the press that brought it back is a hold and not a new press.

What is nearest along the line counts, within 3 blocks, and in front of whatever the ray in `physics/raycast` stops at. So tall grass shields what is behind it, as anything else that can be aimed at does. A column not loaded stops it there too.

## What this does not do

No sword sweep: one swing hits one body. No enchantments and no potions, as there are none. It does not swing an arm, since nothing draws one, and nothing outlines the body aimed at. The player has no bow.
