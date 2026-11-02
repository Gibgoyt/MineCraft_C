/* See README.md. Plain numbers and no memory: a grid is held by whoever holds it. */
#include "item/crafting/index.h"

#include <stdbit.h>
#include <string.h>

/* Whether this item goes in this cell of a recipe. A cell's choices are few, so a straight look is fastest. */
static bool crafting_choice_has(uint16_t cell, uint16_t item) {
    const recipe_choice_t *const choice = &recipe_choices[cell - 1u];
    for (uint32_t i = 0u; i < choice->count; i++) {
        if (recipe_choice_items[choice->first + i] == item) {
            return true;
        }
    }
    return false;
}

/* The grid's box against a shaped recipe of its size, the one way round or the other. */
static bool crafting_shaped_fits(const recipe_def_t *recipe, const item_stack_t *cells, uint32_t side, uint32_t left,
                                 uint32_t top, bool mirrored) {
    for (uint32_t y = 0u; y < recipe->height; y++) {
        for (uint32_t x = 0u; x < recipe->width; x++) {
            const uint32_t from = mirrored ? (uint32_t)recipe->width - 1u - x : x;
            const uint16_t cell = recipe->cells[y * recipe->width + from];
            const item_stack_t *const stack = &cells[(top + y) * side + left + x];
            if (cell == RECIPE_CELL_EMPTY) {
                if (stack->count != 0u) {
                    return false;
                }
            } else if (stack->count == 0u || !crafting_choice_has(cell, stack->item)) {
                return false;
            }
        }
    }
    return true;
}

/* Whether these items fill the recipe's cells, one for one, in any order. Which of its cells the first n items can fill is a set: this
 * walks those sets, at most 2^9 of them, and so does not depend on trying the orders. */
static bool crafting_shapeless_fits(const recipe_def_t *recipe, const uint16_t *items, uint32_t count) {
    uint8_t reachable[1u << RECIPE_CELLS];
    const uint32_t all = (1u << count) - 1u;
    memset(reachable, 0, (uint64_t)all + 1u);
    reachable[0] = 1u;
    for (uint32_t mask = 0u; mask < all; mask++) {
        if (reachable[mask] == 0u) {
            continue;
        }
        const uint16_t item = items[stdc_count_ones(mask)];
        for (uint32_t cell = 0u; cell < count; cell++) {
            if ((mask & (1u << cell)) == 0u && crafting_choice_has(recipe->cells[cell], item)) {
                reachable[mask | (1u << cell)] = 1u;
            }
        }
    }
    return reachable[all] != 0u;
}

uint32_t crafting_match(const item_stack_t *cells, uint32_t side) {
    uint16_t items[RECIPE_CELLS];
    uint32_t count = 0u, left = side, right = 0u, top = side, bottom = 0u;
    for (uint32_t y = 0u; y < side; y++) {
        for (uint32_t x = 0u; x < side; x++) {
            const item_stack_t *const stack = &cells[y * side + x];
            if (stack->count != 0u) {
                items[count] = stack->item;
                count++;
                left = x < left ? x : left;
                right = x > right ? x : right;
                top = y < top ? y : top;
                bottom = y > bottom ? y : bottom;
            }
        }
    }
    if (count == 0u) {
        return CRAFTING_NO_RECIPE;
    }
    const uint32_t width = right - left + 1u, height = bottom - top + 1u;
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        const recipe_def_t *const recipe = &recipe_defs[row];
        if (recipe->cell_count != count) {
            continue;
        }
        if (recipe->kind == RECIPE_KIND_SHAPELESS) {
            if (crafting_shapeless_fits(recipe, items, count)) {
                return row;
            }
        } else if (recipe->width == width && recipe->height == height &&
                   (crafting_shaped_fits(recipe, cells, side, left, top, false) ||
                    crafting_shaped_fits(recipe, cells, side, left, top, true))) {
            return row;
        }
    }
    return CRAFTING_NO_RECIPE;
}

void crafting_grid_start(crafting_grid_t *grid, uint32_t side) {
    memset(grid, 0, sizeof(*grid));
    grid->side = (uint8_t)side;
    grid->recipe = CRAFTING_NO_RECIPE;
}

uint32_t crafting_grid_cells(const crafting_grid_t *grid) {
    return (uint32_t)grid->side * grid->side;
}

item_stack_t *crafting_grid_cell(crafting_grid_t *grid, uint32_t cell) {
    return cell < crafting_grid_cells(grid) ? &grid->cells[cell] : nullptr;
}

void crafting_grid_changed(crafting_grid_t *grid) {
    grid->recipe = crafting_match(grid->cells, grid->side);
    grid->output = grid->recipe == CRAFTING_NO_RECIPE
                       ? (item_stack_t){0}
                       : item_stack_make(recipe_defs[grid->recipe].gives, recipe_defs[grid->recipe].count);
}

void crafting_grid_click_primary(crafting_grid_t *grid, uint32_t cell, item_stack_t *cursor) {
    item_stack_t *const stack = crafting_grid_cell(grid, cell);
    if (stack != nullptr) {
        item_stack_click_primary(stack, cursor);
        crafting_grid_changed(grid);
    }
}

void crafting_grid_click_secondary(crafting_grid_t *grid, uint32_t cell, item_stack_t *cursor) {
    item_stack_t *const stack = crafting_grid_cell(grid, cell);
    if (stack != nullptr) {
        item_stack_click_secondary(stack, cursor);
        crafting_grid_changed(grid);
    }
}

/* One of each cell goes, and the output is whatever is left. */
static void crafting_grid_consume(crafting_grid_t *grid) {
    const uint32_t cells = crafting_grid_cells(grid);
    for (uint32_t i = 0u; i < cells; i++) {
        (void)item_stack_take(&grid->cells[i], 1u);
    }
    crafting_grid_changed(grid);
}

uint32_t crafting_grid_take(crafting_grid_t *grid, item_stack_t *cursor) {
    const uint32_t making = grid->output.count;
    if (making == 0u || item_stack_room(cursor, &grid->output) < making) {
        return 0u;
    }
    item_stack_t output = grid->output;
    (void)item_stack_move(&output, cursor, making);
    grid->crafted_count += making;
    crafting_grid_consume(grid);
    return making;
}

uint32_t crafting_grid_take_all(crafting_grid_t *grid, inventory_t *inventory) {
    uint32_t taken = 0u;
    /* Ends: every round takes one from each cell that holds any, and the cells hold few. */
    while (grid->output.count != 0u) {
        const uint32_t making = grid->output.count;
        if (inventory_room(inventory, grid->output.item, making) < making) {
            break;
        }
        (void)inventory_add(inventory, grid->output.item, making);
        grid->crafted_count += making;
        taken += making;
        crafting_grid_consume(grid);
    }
    return taken;
}

uint32_t crafting_grid_give_back(crafting_grid_t *grid, inventory_t *inventory) {
    const uint32_t cells = crafting_grid_cells(grid);
    uint32_t stayed = 0u;
    for (uint32_t i = 0u; i < cells; i++) {
        if (grid->cells[i].count != 0u) {
            (void)inventory_add_stack(inventory, &grid->cells[i]);
            stayed += grid->cells[i].count;
        }
    }
    crafting_grid_changed(grid);
    return stayed;
}
