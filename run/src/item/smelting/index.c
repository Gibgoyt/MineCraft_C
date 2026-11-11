/* See README.md. */
#include "item/smelting/index.h"

#include <string.h>

void smelting_start(smelting_store_t *store, arena_t *arena) {
    memset(store, 0, sizeof(*store));
    store->borrowed_furnaces =
        arena_alloc_array(arena, SMELTING_FURNACES, sizeof(smelting_furnace_t), alignof(smelting_furnace_t));
}

/* A furnace is looked for when a screen opens or a block goes, and not in a tick: so a straight look, over very few. */
smelting_furnace_t *smelting_at(smelting_store_t *store, int32_t x, int32_t y, int32_t z) {
    for (uint32_t i = 0u; i < store->count; i++) {
        smelting_furnace_t *const furnace = &store->borrowed_furnaces[i];
        if (furnace->cell[0] == x && furnace->cell[1] == y && furnace->cell[2] == z) {
            return furnace;
        }
    }
    return nullptr;
}

smelting_furnace_t *smelting_open(smelting_store_t *store, int32_t x, int32_t y, int32_t z) {
    return smelting_open_process(store, x, y, z, RECIPE_PROCESS_FURNACE);
}

smelting_furnace_t *smelting_open_process(smelting_store_t *store, int32_t x, int32_t y, int32_t z, uint8_t process) {
    smelting_furnace_t *const found = smelting_at(store, x, y, z);
    if (found != nullptr) {
        return found;
    }
    if (store->count == SMELTING_FURNACES) {
        store->refused_count++;
        return nullptr;
    }
    smelting_furnace_t *const furnace = &store->borrowed_furnaces[store->count];
    store->count++;
    memset(furnace, 0, sizeof(*furnace));
    furnace->cell[0] = x;
    furnace->cell[1] = y;
    furnace->cell[2] = z;
    furnace->process = process < RECIPE_PROCESS_COUNT ? process : (uint8_t)RECIPE_PROCESS_FURNACE;
    return furnace;
}

uint32_t smelting_remove(smelting_store_t *store, int32_t x, int32_t y, int32_t z, item_stack_t out[SMELTING_SLOTS]) {
    smelting_furnace_t *const furnace = smelting_at(store, x, y, z);
    if (furnace == nullptr) {
        return 0u;
    }
    uint32_t stacks = 0u;
    for (uint32_t i = 0u; i < SMELTING_SLOTS; i++) {
        if (furnace->slots[i].count != 0u) {
            out[stacks] = furnace->slots[i];
            stacks++;
        }
    }
    store->count--;
    *furnace = store->borrowed_furnaces[store->count];
    return stacks;
}

const recipe_smelt_t *smelting_recipe_of(uint16_t item) {
    if (item >= ITEM_COUNT || recipe_smelt_rows[item] == RECIPE_NO_SMELT) {
        return nullptr;
    }
    return &recipe_smelts[recipe_smelt_rows[item] - 1u];
}

uint32_t smelting_burn_ticks(uint16_t item) {
    return item < ITEM_COUNT ? recipe_fuels[item].ticks : 0u;
}

bool smelting_burns(const smelting_furnace_t *furnace) {
    return furnace->process >= RECIPE_PROCESS_COUNT || recipe_process_burns[furnace->process] != 0u;
}

bool smelting_is_lit(const smelting_furnace_t *furnace) {
    return furnace->burn_ticks != 0u;
}

/* Whether the input can go on now: it smelts, and the output has room for all of what it makes. */
static const recipe_smelt_t *smelting_ready(const smelting_furnace_t *furnace) {
    const item_stack_t *const input = &furnace->slots[SMELTING_SLOT_INPUT];
    if (input->count == 0u) {
        return nullptr;
    }
    const recipe_smelt_t *const recipe = smelting_recipe_of(input->item);
    if (recipe == nullptr || recipe->process != furnace->process) {
        return nullptr;
    }
    const item_stack_t made = item_stack_make(recipe->gives, recipe->count);
    return item_stack_room(&furnace->slots[SMELTING_SLOT_OUTPUT], &made) >= recipe->count ? recipe : nullptr;
}

/* A new piece of fuel catches, and is gone from its slot, except for what it leaves. */
static void smelting_burn_next(smelting_store_t *store, smelting_furnace_t *furnace) {
    item_stack_t *const fuel = &furnace->slots[SMELTING_SLOT_FUEL];
    const recipe_fuel_t burns = recipe_fuels[fuel->item];
    if (fuel->count == 0u || burns.ticks == 0u) {
        return;
    }
    furnace->burn_ticks = burns.ticks;
    furnace->burn_total = burns.ticks;
    (void)item_stack_take(fuel, 1u);
    store->burned_count++;
    /* A lava bucket stacks to one, so its slot is empty by now and there is room for what it leaves. */
    if (burns.leaves != ITEM_ID_NONE && fuel->count == 0u) {
        *fuel = item_stack_make(burns.leaves, 1u);
    }
}

/* In this order, as Minecraft's: what burns goes down a tick first, and so fuel lasts exactly its number of ticks of cooking. */
bool smelting_tick(smelting_store_t *store, smelting_furnace_t *furnace) {
    item_stack_t *const input = &furnace->slots[SMELTING_SLOT_INPUT];
    if (furnace->burn_ticks != 0u) {
        furnace->burn_ticks--;
    }
    /* Something else in the input starts again from nothing, even with the same recipe. */
    if (input->count == 0u || input->item != furnace->cooking) {
        furnace->cook_ticks = 0u;
        furnace->cooking = input->item;
    }
    const recipe_smelt_t *const recipe = smelting_ready(furnace);
    if (!smelting_burns(furnace)) {
        /* A hide tans with time alone, and what is no longer there to work starts again. */
        if (recipe == nullptr) {
            furnace->cook_ticks = 0u;
            return false;
        }
        furnace->cook_ticks++;
        if (furnace->cook_ticks >= recipe->ticks) {
            furnace->cook_ticks = 0u;
            item_stack_t made = item_stack_make(recipe->gives, recipe->count);
            (void)item_stack_move(&made, &furnace->slots[SMELTING_SLOT_OUTPUT], recipe->count);
            (void)item_stack_take(input, 1u);
            store->smelted_count += recipe->count;
        }
        return false;
    }
    if (furnace->burn_ticks == 0u && recipe != nullptr) {
        smelting_burn_next(store, furnace);
    }
    if (furnace->burn_ticks != 0u && recipe != nullptr) {
        furnace->cook_ticks++;
        if (furnace->cook_ticks >= recipe->ticks) {
            furnace->cook_ticks = 0u;
            item_stack_t made = item_stack_make(recipe->gives, recipe->count);
            (void)item_stack_move(&made, &furnace->slots[SMELTING_SLOT_OUTPUT], recipe->count);
            (void)item_stack_take(input, 1u);
            store->smelted_count += recipe->count;
        }
    } else if (furnace->burn_ticks != 0u) {
        /* Burning with nothing it can do: all that had been done is gone. */
        furnace->cook_ticks = 0u;
    } else if (furnace->cook_ticks != 0u) {
        /* Gone out: it cools at twice the rate it cooked. */
        furnace->cook_ticks = furnace->cook_ticks > 2u ? furnace->cook_ticks - 2u : 0u;
    }
    return furnace->burn_ticks != 0u;
}

float32_t smelting_cook_fraction(const smelting_furnace_t *furnace) {
    const recipe_smelt_t *const recipe = smelting_recipe_of(furnace->cooking);
    if (recipe == nullptr || furnace->cook_ticks == 0u) {
        return 0.0f32;
    }
    return (float32_t)furnace->cook_ticks / (float32_t)recipe->ticks;
}

float32_t smelting_burn_fraction(const smelting_furnace_t *furnace) {
    return furnace->burn_total == 0u ? 0.0f32 : (float32_t)furnace->burn_ticks / (float32_t)furnace->burn_total;
}

void smelting_click(smelting_furnace_t *furnace, uint32_t slot, item_stack_t *cursor, bool secondary) {
    if (slot >= SMELTING_SLOTS) {
        return;
    }
    item_stack_t *const stack = &furnace->slots[slot];
    if (slot == SMELTING_SLOT_OUTPUT) {
        if (item_stack_room(cursor, stack) >= stack->count) {
            (void)item_stack_move(stack, cursor, stack->count);
        }
        return;
    }
    if (slot == SMELTING_SLOT_FUEL && cursor->count != 0u &&
        (smelting_burn_ticks(cursor->item) == 0u || !smelting_burns(furnace))) {
        return;
    }
    if (secondary) {
        item_stack_click_secondary(stack, cursor);
    } else {
        item_stack_click_primary(stack, cursor);
    }
}

uint32_t smelting_quick_move_in(smelting_furnace_t *furnace, inventory_t *inventory, uint32_t slot) {
    if (slot >= INVENTORY_SLOTS) {
        return 0u;
    }
    const item_stack_t *const from = inventory_slot_read(inventory, slot);
    if (from->count == 0u) {
        return 0u;
    }
    const recipe_smelt_t *const recipe = smelting_recipe_of(from->item);
    const uint32_t to = recipe != nullptr && recipe->process == furnace->process           ? SMELTING_SLOT_INPUT
                        : smelting_burns(furnace) && smelting_burn_ticks(from->item) != 0u ? SMELTING_SLOT_FUEL
                                                                                           : SMELTING_SLOTS;
    if (to == SMELTING_SLOTS) {
        return 0u;
    }
    return inventory_slot_move_out(inventory, slot, &furnace->slots[to], from->count);
}

uint32_t smelting_quick_move_out(smelting_furnace_t *furnace, inventory_t *inventory, uint32_t slot) {
    return slot < SMELTING_SLOTS ? inventory_add_stack(inventory, &furnace->slots[slot]) : 0u;
}
