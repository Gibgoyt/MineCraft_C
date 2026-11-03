# run/src/item/equipment

**Purpose:** what tools do and how they wear, and what armor is worn and what it stops.
**Tier:** Arena. Takes nothing, as a tier may allocate less than it is allowed: `equipment_t` is a field of `item_t`.
**Owns:** nothing. What is in hand is the inventory's, and the stacks worn are in `equipment_t`.
**Lifetime:** World: `item_open` starts it and `item_close` ends it, and `run/src/save/state/` writes what is worn into a save and reads it back.
**Threads:** the sim's alone.
**Public functions** (`run/include/item/equipment/index.h`):

- `equipment_start`: nothing worn.
- `equipment_tool_held`: the tool and tier the slot in hand mines as.
- `equipment_wear_of_block`, `equipment_wear_of_attack`, `equipment_uses_left`, `equipment_attack_damage`: the rules below, for any item.
- `equipment_tool_wear`, `equipment_tool_block_broken`, `equipment_tool_attacked`: the slot in hand wears, and breaks.
- `equipment_own_drop`: what shears give on top.
- `equipment_till_result`, `equipment_hoe_till`, `equipment_use_tick`: a hoe, and the right button with a hoe or armor.
- `equipment_armor_slot_of`, `equipment_armor_worn`, `equipment_armor_points`, `equipment_armor_toughness`: what goes where, what is there, and what it adds up to.
- `equipment_armor_click`, `equipment_armor_wear_held`, `equipment_armor_quick_move_in`, `equipment_armor_quick_move_out`: putting it on and taking it off.
- `equipment_armor_absorb`, `equipment_armor_hurt`: what is left of a hit, the second with the wear.

**May be called by:** `run/src/item/` and `run/src/game/`, the screens of Stage 14.3, the HUD of Stage 14.2, and what deals damage: `run/src/player/survival/` and `run/src/player/combat/`.
**Depends on:** `item/stack`, `item/inventory`, `world/edit`, `world/map`, and the generated `tables/items/` and `tables/blocks/`.

## Tools

Everything comes from `config/items/<item>/index.yaml`. `run/src/game/` tells `run/src/player/interaction/` what the slot in hand is before each tick, so its table of speeds and harvests is what mining goes by, and starts the block again if the tool changes. These are Minecraft's published rules, written here from scratch:

| What | Uses lost |
|---|---|
| Breaking a block with a tool | 1, and 2 with a sword. None if the block breaks at a touch (`hardness: 0`), except shears on their own blocks, which lose 1 |
| Hitting something | A sword 1; a pickaxe, axe, shovel or hoe 2; shears none |
| Tilling | 1 |

A stack's `wear` is the uses gone, and one with none left is no stack: the use that takes the last one breaks it, and it leaves the slot through `inventory_selected_take`, so the inventory's books see it go. What has no `tool`, like a bow, does not wear here: the stages that use those call `equipment_tool_wear`.

A hit does `damage` half hearts, or a hand's 1 if that is more. `run/src/player/combat/` is to call `equipment_attack_damage` and `equipment_tool_attacked`.

**What tools do besides:**

- **A hoe** on `dirt` or `grass_block` makes `farmland`, and on `coarse_dirt` makes `dirt`, if only air is above it. The right button, held, does this every 4 ticks.
- **Shears** bring out the block they broke as an item, and `run/src/player/interaction/` rolls nothing of its table, so a shorn leaf gives the leaf and no sapling. Every block with `tool: shears` has such an item, as `run_item_equipment` holds.
- **Gold** mines its own blocks at the published 12, by `equipment_tool_speed_milli`, and harvests what wood harvests. A gold sword mines as any sword.

## Armor

Four slots: head, chest, legs, feet. Armor goes on with the right button, on the press alone, swapping with what was there, or by a screen with the functions above. A piece that leaves the inventory leaves its books, as a stack put in a chest does.

Points add up, and against a hit of `damage` half hearts:

    left = damage * (1 - min(20, max(points / 5, points - damage / (2 + toughness / 4))) / 25)

This is the published formula. Only diamond has toughness, 2 a piece in `config/items/`. So 20 points with none leave 30% of a hit of 5 and 60% of a hit of 20, and a full diamond set leaves 40% of that hit of 20.

`equipment_armor_hurt` is that with the wear: each piece loses a quarter of the hit in uses, rounded down, and at least 1. What goes past armor is the caller's to know: it does not call this.

**In a running game** `run/src/game/` tells `run/src/player/survival/` the points and toughness worn before each tick, so that module works out what a hit leaves, and its sum is held to this one by `run_item_equipment`. After the tick it gives what armor stood against to `equipment_armor_hurt` for the wear. On dying, what is worn spills with the rest.

## What it is not

- **A hoe works from any side,** underneath too, where the published one does not.
- **No enchantments,** and no repairing: neither is in the plan.
