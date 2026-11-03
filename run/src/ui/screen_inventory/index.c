/* See README.md. */
#include "ui/screen_inventory/index.h"

#include "assert/index.h"
#include "tables/items/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    UI_SCREEN_DIM_COLOR = 0xA0101010u,
    UI_SCREEN_TITLE_COLOR = 0xFFFFFFFFu,
    UI_SCREEN_COUNT_COLOR = 0xFFFFFFFFu,
    UI_SCREEN_COUNT_SHADOW = 0xFF3F3F3Fu,
    UI_SCREEN_WEAR_BACK = 0xFF000000u,
    UI_SCREEN_WEAR_GOOD = 0xFF40E040u,
    UI_SCREEN_WEAR_MID = 0xFFE0C020u,
    UI_SCREEN_WEAR_BAD = 0xFFE03030u,
    UI_SCREEN_WEAR_WIDTH = 13u,
    UI_SCREEN_TOOLTIP_BYTES = 96u,
    UI_SCREEN_COUNT_BYTES = 8u,
    UI_SCREEN_ARROW_DIM_DIVIDE = 3u, /* of the arrow's alpha, for the part still to fill */
};

/* The inventory screen's own places. */
enum : int16_t {
    UI_SCREEN_INVENTORY_ARMOR_X = 8,
    UI_SCREEN_INVENTORY_ARMOR_Y = 8,
    UI_SCREEN_INVENTORY_GRID_X = 88,
    UI_SCREEN_INVENTORY_GRID_Y = 26,
    UI_SCREEN_INVENTORY_ARROW_X = 127,
    UI_SCREEN_INVENTORY_ARROW_Y = 37,
    UI_SCREEN_INVENTORY_OUTPUT_X = 150,
    UI_SCREEN_INVENTORY_OUTPUT_Y = 36,
};

static const char *const UI_SCREEN_ARMOR_TEXTURES[EQUIPMENT_ARMOR_SLOTS] = {
    "ui/slot_helmet",
    "ui/slot_chestplate",
    "ui/slot_leggings",
    "ui/slot_boots",
};

ui_screens_t *ui_screens_make(arena_t *arena, const texture_t *texture) {
    ui_screens_t *const screens = arena_alloc(arena, sizeof(ui_screens_t), alignof(ui_screens_t));
    memset(screens, 0, sizeof(*screens));
    ui_image_t *const icons = arena_alloc(arena, (uint64_t)ITEM_COUNT * sizeof(ui_image_t), alignof(ui_image_t));
    memset(icons, 0, (uint64_t)ITEM_COUNT * sizeof(ui_image_t));
    /* From 1: the empty row has no picture, and nothing draws it. */
    for (uint32_t item = 1u; item < (uint32_t)ITEM_COUNT; item++) {
        if (!ui_image_load(&icons[item], arena, texture, item_defs[item].texture)) {
            return nullptr; /* it has said why */
        }
    }
    screens->borrowed_icons = icons;
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        if (!ui_image_load(&screens->armor_empty[slot], arena, texture, UI_SCREEN_ARMOR_TEXTURES[slot])) {
            return nullptr;
        }
    }
    if (!ui_image_load(&screens->arrow, arena, texture, "ui/crafting_arrow") ||
        !ui_image_load(&screens->fire_empty, arena, texture, "ui/furnace_fire_empty") ||
        !ui_image_load(&screens->fire_full, arena, texture, "ui/furnace_fire_full")) {
        return nullptr;
    }
    /* There is one arrow in textures/ui, and a gauge needs an empty one to fill. */
    const uint64_t pixels = (uint64_t)screens->arrow.width * screens->arrow.height;
    uint32_t *const dim = arena_alloc(arena, pixels * sizeof(uint32_t), alignof(uint32_t));
    for (uint64_t i = 0u; i < pixels; i++) {
        const uint32_t pixel = screens->arrow.borrowed_pixels[i];
        dim[i] = (pixel & 0x00FFFFFFu) | (((pixel >> 24u) / UI_SCREEN_ARROW_DIM_DIVIDE) << 24u);
    }
    screens->arrow_dim = (ui_image_t){dim, screens->arrow.width, screens->arrow.height};
    return screens;
}

void ui_screens_begin(ui_screens_t *screens) {
    render_exchange_start(&screens->views);
    memset(screens->view_slots, 0, sizeof(screens->view_slots));
    /* A view of zero bytes says nothing is up, and it is what the reader's slot holds until the first publish. */
    screens->borrowed_shown = &screens->view_slots[screens->views.front];
    atomic_store_explicit(&screens->ring_head, 0u, memory_order_relaxed);
    atomic_store_explicit(&screens->ring_tail, 0u, memory_order_relaxed);
    screens->sent_count = 0u;
    screens->lost_count = 0u;
    memset(screens->cell, 0, sizeof(screens->cell));
    screens->cursor = (item_stack_t){0};
    screens->opened_count = 0u;
    screens->click_count = 0u;
    screens->published_count = 0u;
    screens->kind = UI_SCREEN_NONE;
}

/* ---- Main's ------------------------------------------------------------------------------------------------------- */

void ui_screens_view_take(ui_screens_t *screens, ui_widget_t *widget) {
    uint32_t slot = 0u;
    if (!render_exchange_take(&screens->views, &slot)) {
        return;
    }
    const uint8_t before = screens->borrowed_shown->kind;
    screens->borrowed_shown = &screens->view_slots[slot];
    if (before == UI_SCREEN_NONE && screens->borrowed_shown->kind != UI_SCREEN_NONE && widget->canvas_width != 0u) {
        widget->input.pointer_x = (int32_t)(widget->canvas_width / 2u);
        widget->input.pointer_y = (int32_t)(widget->canvas_height / 2u);
    }
}

void ui_screens_send(ui_screens_t *screens, ui_screen_action_t action) {
    const uint32_t head = atomic_load_explicit(&screens->ring_head, memory_order_relaxed);
    const uint32_t tail = atomic_load_explicit(&screens->ring_tail, memory_order_acquire);
    if (head - tail >= UI_SCREEN_RING) {
        screens->lost_count++;
        return;
    }
    screens->ring[head & (UI_SCREEN_RING - 1u)] = action;
    atomic_store_explicit(&screens->ring_head, head + 1u, memory_order_release);
    screens->sent_count++;
}

ui_rect_t ui_screen_window(uint32_t width, uint32_t height) {
    const int32_t scale = (int32_t)ui_widget_scale_for(width, height);
    const ui_rect_t whole = {0, 0, (int32_t)width, (int32_t)height};
    return ui_rect_center(whole, (int32_t)UI_SCREEN_WINDOW_WIDTH * scale, (int32_t)UI_SCREEN_WINDOW_HEIGHT * scale);
}

static ui_rect_t ui_screen_place_rect(ui_rect_t window, int32_t scale, const ui_screen_place_t *place) {
    return (ui_rect_t){window.x + place->x * scale, window.y + place->y * scale, (int32_t)UI_SCREEN_SLOT * scale,
                       (int32_t)UI_SCREEN_SLOT * scale};
}

ui_screen_action_t ui_screen_hit_test(const ui_screen_place_t *places, uint32_t count, uint32_t width, uint32_t height,
                                      int32_t x, int32_t y) {
    ui_screen_action_t hit = {.op = UI_SCREEN_OP_CLICK, .zone = UI_SCREEN_ZONE_OUTSIDE, .slot = 0u, .flags = 0u};
    const ui_rect_t window = ui_screen_window(width, height);
    if (!ui_rect_contains(window, x, y)) {
        return hit;
    }
    hit.zone = UI_SCREEN_ZONE_WINDOW;
    const int32_t scale = (int32_t)ui_widget_scale_for(width, height);
    for (uint32_t i = 0u; i < count; i++) {
        if (ui_rect_contains(ui_screen_place_rect(window, scale, &places[i]), x, y)) {
            hit.zone = places[i].zone;
            hit.slot = places[i].slot;
            break;
        }
    }
    return hit;
}

uint32_t ui_screen_player_places(ui_screen_place_t *out) {
    for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
        out[slot] = (ui_screen_place_t){.x = (int16_t)(UI_SCREEN_ROWS_X + slot * UI_SCREEN_SLOT),
                                        .y = (int16_t)UI_SCREEN_HOTBAR_Y,
                                        .zone = UI_SCREEN_ZONE_PLAYER,
                                        .slot = (uint8_t)slot,
                                        ._pad0 = {0}};
    }
    for (uint32_t slot = INVENTORY_HOTBAR_SLOTS; slot < INVENTORY_SLOTS; slot++) {
        const uint32_t index = slot - INVENTORY_HOTBAR_SLOTS;
        out[slot] =
            (ui_screen_place_t){.x = (int16_t)(UI_SCREEN_ROWS_X + (index % INVENTORY_HOTBAR_SLOTS) * UI_SCREEN_SLOT),
                                .y = (int16_t)(UI_SCREEN_ROWS_Y + (index / INVENTORY_HOTBAR_SLOTS) * UI_SCREEN_SLOT),
                                .zone = UI_SCREEN_ZONE_PLAYER,
                                .slot = (uint8_t)slot,
                                ._pad0 = {0}};
    }
    return INVENTORY_SLOTS;
}

static const item_stack_t *ui_screen_view_stack(const ui_screen_view_t *view, const ui_screen_place_t *place) {
    switch ((ui_screen_zone_t)place->zone) {
    case UI_SCREEN_ZONE_PLAYER:
        return &view->player[place->slot];
    case UI_SCREEN_ZONE_ARMOR:
        return &view->armor[place->slot];
    case UI_SCREEN_ZONE_GRID:
        return &view->grid[place->slot];
    case UI_SCREEN_ZONE_OUTPUT:
        return &view->output;
    case UI_SCREEN_ZONE_BLOCK:
        return &view->block[place->slot];
    case UI_SCREEN_ZONE_WINDOW:
    case UI_SCREEN_ZONE_OUTSIDE:
        break;
    }
    ASSERT_ALWAYS(false);
    return nullptr;
}

/* "iron_pickaxe" as a person would write it, and how many uses are left if it wears. */
static void ui_screen_tooltip_for(ui_widget_t *widget, const item_stack_t *stack) {
    char text[UI_SCREEN_TOOLTIP_BYTES];
    const item_def_t *const def = &item_defs[stack->item];
    uint32_t length = 0u;
    bool word_start = true;
    for (const char *from = def->name; *from != '\0' && length + 1u < sizeof(text); from++) {
        const char c = *from == '_' ? ' ' : *from;
        text[length++] = word_start && c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
        word_start = c == ' ';
    }
    text[length] = '\0';
    if (def->durability != 0u) {
        (void)snprintf(text + length, sizeof(text) - length, "\n%" PRIu32 " of %" PRIu32 " uses",
                       equipment_uses_left(stack), (uint32_t)def->durability);
    }
    ui_widget_tooltip(widget, text);
}

void ui_screen_draw_stack(ui_widget_t *widget, const ui_screens_t *screens, ui_rect_t slot, const item_stack_t *stack) {
    if (item_stack_is_empty(stack)) {
        return;
    }
    ui_canvas_t *const canvas = widget->borrowed_canvas;
    const int32_t scale = (int32_t)widget->scale;
    const ui_rect_t icon = {slot.x + scale, slot.y + scale, (int32_t)UI_SCREEN_ICON * scale,
                            (int32_t)UI_SCREEN_ICON * scale};
    ui_compose_image_whole(canvas, &screens->borrowed_icons[stack->item], icon);
    const uint32_t durability = item_defs[stack->item].durability;
    if (durability != 0u && stack->wear != 0u) {
        const uint32_t left = equipment_uses_left(stack);
        const int32_t full = (int32_t)UI_SCREEN_WEAR_WIDTH * scale;
        const int32_t shown = (int32_t)((int64_t)full * left / durability);
        const ui_rect_t back = {slot.x + 2 * scale, slot.y + 14 * scale, full, 2 * scale};
        ui_compose_fill(canvas, back, UI_SCREEN_WEAR_BACK);
        const uint32_t color = left * 2u >= durability   ? UI_SCREEN_WEAR_GOOD
                               : left * 4u >= durability ? UI_SCREEN_WEAR_MID
                                                         : UI_SCREEN_WEAR_BAD;
        ui_compose_fill(canvas, (ui_rect_t){back.x, back.y, shown, scale}, color);
    }
    if (stack->count > 1u) {
        char text[UI_SCREEN_COUNT_BYTES];
        (void)snprintf(text, sizeof(text), "%" PRIu32, (uint32_t)stack->count);
        const ui_text_size_t size = ui_font_measure(widget->borrowed_font, text, widget->scale);
        const ui_text_style_t style = {
            .color = UI_SCREEN_COUNT_COLOR, .shadow = UI_SCREEN_COUNT_SHADOW, .scale = widget->scale};
        ui_font_draw(widget->borrowed_font, canvas,
                     slot.x + (int32_t)UI_SCREEN_SLOT * scale - (int32_t)size.width - scale,
                     slot.y + (int32_t)UI_SCREEN_SLOT * scale - (int32_t)size.height - scale, text, style);
    }
}

void ui_screen_draw_base(ui_widget_t *widget, const ui_screens_t *screens, const ui_screen_view_t *view,
                         const ui_screen_place_t *places, uint32_t count) {
    ui_canvas_t *const canvas = widget->borrowed_canvas;
    const int32_t scale = (int32_t)widget->scale;
    ui_compose_fill(canvas, (ui_rect_t){0, 0, (int32_t)canvas->width, (int32_t)canvas->height}, UI_SCREEN_DIM_COLOR);
    const ui_rect_t window = ui_screen_window(canvas->width, canvas->height);
    ui_widget_panel(widget, window);
    for (uint32_t i = 0u; i < count; i++) {
        const ui_rect_t rect = ui_screen_place_rect(window, scale, &places[i]);
        const uint32_t bits = ui_widget_slot(widget, rect);
        const item_stack_t *const stack = ui_screen_view_stack(view, &places[i]);
        if (places[i].zone == UI_SCREEN_ZONE_ARMOR && item_stack_is_empty(stack)) {
            const ui_rect_t icon = {rect.x + scale, rect.y + scale, (int32_t)UI_SCREEN_ICON * scale,
                                    (int32_t)UI_SCREEN_ICON * scale};
            ui_compose_image_whole(canvas, &screens->armor_empty[places[i].slot], icon);
        }
        ui_screen_draw_stack(widget, screens, rect, stack);
        if ((bits & UI_SLOT_HOVER) != 0u && !item_stack_is_empty(stack) && item_stack_is_empty(&view->cursor)) {
            ui_screen_tooltip_for(widget, stack);
        }
    }
}

void ui_screen_draw_cursor(ui_widget_t *widget, const ui_screens_t *screens, const ui_screen_view_t *view) {
    const int32_t size = (int32_t)UI_SCREEN_SLOT * (int32_t)widget->scale;
    const ui_rect_t slot = {widget->input.pointer_x - size / 2, widget->input.pointer_y - size / 2, size, size};
    ui_screen_draw_stack(widget, screens, slot, &view->cursor);
}

void ui_screen_draw_title(ui_widget_t *widget, ui_rect_t window, int32_t x, int32_t y, const char *text) {
    const int32_t scale = (int32_t)widget->scale;
    ui_widget_label(widget, window.x + x * scale, window.y + y * scale, text, UI_SCREEN_TITLE_COLOR, true);
}

void ui_screen_draw_arrow(ui_widget_t *widget, const ui_screens_t *screens, ui_rect_t window, int32_t x, int32_t y,
                          float32_t fraction) {
    ui_canvas_t *const canvas = widget->borrowed_canvas;
    const int32_t scale = (int32_t)widget->scale;
    const ui_rect_t rect = {window.x + x * scale, window.y + y * scale, (int32_t)UI_SCREEN_ARROW_WIDTH * scale,
                            (int32_t)UI_SCREEN_ARROW_HEIGHT * scale};
    if (fraction < 1.0f32) {
        ui_compose_image_whole(canvas, &screens->arrow_dim, rect);
    }
    if (fraction > 0.0f32) {
        const int32_t filled = fraction >= 1.0f32 ? rect.width : (int32_t)(fraction * (float32_t)rect.width);
        const ui_rect_t shown = {rect.x, rect.y, filled, rect.height};
        const ui_rect_t outer = ui_canvas_clip_set(canvas, ui_rect_intersect(shown, canvas->clip));
        ui_compose_image_whole(canvas, &screens->arrow, rect);
        (void)ui_canvas_clip_set(canvas, outer);
    }
}

/* ---- The sim's ---------------------------------------------------------------------------------------------------- */

bool ui_screens_next(ui_screens_t *screens, ui_screen_action_t *out) {
    const uint32_t tail = atomic_load_explicit(&screens->ring_tail, memory_order_relaxed);
    if (tail == atomic_load_explicit(&screens->ring_head, memory_order_acquire)) {
        return false;
    }
    *out = screens->ring[tail & (UI_SCREEN_RING - 1u)];
    atomic_store_explicit(&screens->ring_tail, tail + 1u, memory_order_release);
    return true;
}

ui_screen_view_t *ui_screens_view_next(ui_screens_t *screens, const item_t *item) {
    ui_screen_view_t *const view = &screens->view_slots[screens->views.back];
    memset(view, 0, sizeof(*view));
    view->kind = screens->kind;
    view->cursor = screens->cursor;
    if (screens->kind != UI_SCREEN_NONE) {
        memcpy(view->player, item->inventory.borrowed_slots, sizeof(view->player));
        memcpy(view->armor, item->equipment.armor, sizeof(view->armor));
    }
    return view;
}

void ui_screens_view_publish(ui_screens_t *screens) {
    (void)render_exchange_publish(&screens->views);
    screens->published_count++;
}

void ui_screen_player_click(inventory_t *inventory, uint32_t slot, uint32_t flags, item_stack_t *cursor) {
    item_stack_t *const stack = inventory_slot(inventory, slot);
    const item_stack_t before = *stack;
    if ((flags & UI_SCREEN_FLAG_SECONDARY) != 0u) {
        item_stack_click_secondary(stack, cursor);
    } else {
        item_stack_click_primary(stack, cursor);
    }
    /* The inventory's books count what is in its slots, and what the pointer carries is not. */
    if (before.item != stack->item || before.wear != stack->wear) {
        inventory->removed_count += before.count;
        inventory->added_count += stack->count;
    } else if (stack->count > before.count) {
        inventory->added_count += (uint32_t)stack->count - before.count;
    } else {
        inventory->removed_count += (uint32_t)before.count - stack->count;
    }
}

void ui_screen_grid_click(crafting_grid_t *grid, inventory_t *inventory, ui_screen_action_t action,
                          item_stack_t *cursor) {
    const bool shift = (action.flags & UI_SCREEN_FLAG_SHIFT) != 0u;
    if (action.zone == UI_SCREEN_ZONE_OUTPUT) {
        if (shift) {
            (void)crafting_grid_take_all(grid, inventory);
        } else {
            (void)crafting_grid_take(grid, cursor);
        }
        return;
    }
    if (shift) {
        item_stack_t *const cell = crafting_grid_cell(grid, action.slot);
        if (cell != nullptr) {
            (void)inventory_add_stack(inventory, cell);
            crafting_grid_changed(grid);
        }
    } else if ((action.flags & UI_SCREEN_FLAG_SECONDARY) != 0u) {
        crafting_grid_click_secondary(grid, action.slot, cursor);
    } else {
        crafting_grid_click_primary(grid, action.slot, cursor);
    }
}

/* ---- The inventory screen ----------------------------------------------------------------------------------------- */

uint32_t ui_screen_inventory_places(ui_screen_place_t *out) {
    uint32_t count = ui_screen_player_places(out);
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        out[count++] =
            (ui_screen_place_t){.x = UI_SCREEN_INVENTORY_ARMOR_X,
                                .y = (int16_t)(UI_SCREEN_INVENTORY_ARMOR_Y + (int32_t)(slot * UI_SCREEN_SLOT)),
                                .zone = UI_SCREEN_ZONE_ARMOR,
                                .slot = (uint8_t)slot,
                                ._pad0 = {0}};
    }
    for (uint32_t cell = 0u; cell < CRAFTING_SIDE_HANDS * CRAFTING_SIDE_HANDS; cell++) {
        out[count++] = (ui_screen_place_t){
            .x = (int16_t)(UI_SCREEN_INVENTORY_GRID_X + (int32_t)((cell % CRAFTING_SIDE_HANDS) * UI_SCREEN_SLOT)),
            .y = (int16_t)(UI_SCREEN_INVENTORY_GRID_Y + (int32_t)((cell / CRAFTING_SIDE_HANDS) * UI_SCREEN_SLOT)),
            .zone = UI_SCREEN_ZONE_GRID,
            .slot = (uint8_t)cell,
            ._pad0 = {0}};
    }
    out[count++] = (ui_screen_place_t){.x = UI_SCREEN_INVENTORY_OUTPUT_X,
                                       .y = UI_SCREEN_INVENTORY_OUTPUT_Y,
                                       .zone = UI_SCREEN_ZONE_OUTPUT,
                                       .slot = 0u,
                                       ._pad0 = {0}};
    return count;
}

void ui_screen_inventory_view(const item_t *item, ui_screen_view_t *view) {
    memcpy(view->grid, item->crafting_hands.cells, sizeof(view->grid));
    view->output = item->crafting_hands.output;
}

void ui_screen_inventory_draw(ui_widget_t *widget, const ui_screens_t *screens, const ui_screen_view_t *view) {
    const ui_canvas_t *const canvas = widget->borrowed_canvas;
    const ui_rect_t window = ui_screen_window(canvas->width, canvas->height);
    ui_screen_draw_title(widget, window, UI_SCREEN_INVENTORY_GRID_X, UI_SCREEN_INVENTORY_GRID_Y - 10, "Crafting");
    ui_screen_draw_arrow(widget, screens, window, UI_SCREEN_INVENTORY_ARROW_X, UI_SCREEN_INVENTORY_ARROW_Y,
                         item_stack_is_empty(&view->output) ? 0.0f32 : 1.0f32);
}

void ui_screen_inventory_click(item_t *item, ui_screen_action_t action, item_stack_t *cursor) {
    const bool shift = (action.flags & UI_SCREEN_FLAG_SHIFT) != 0u;
    switch ((ui_screen_zone_t)action.zone) {
    case UI_SCREEN_ZONE_PLAYER:
        if (!shift) {
            ui_screen_player_click(&item->inventory, action.slot, action.flags, cursor);
        } else if (equipment_armor_quick_move_in(&item->equipment, &item->inventory, action.slot) == 0u) {
            (void)inventory_quick_move(&item->inventory, action.slot);
        }
        break;
    case UI_SCREEN_ZONE_ARMOR:
        if (shift) {
            (void)equipment_armor_quick_move_out(&item->equipment, &item->inventory, action.slot);
        } else {
            (void)equipment_armor_click(&item->equipment, action.slot, cursor);
        }
        break;
    case UI_SCREEN_ZONE_GRID:
    case UI_SCREEN_ZONE_OUTPUT:
        ui_screen_grid_click(&item->crafting_hands, &item->inventory, action, cursor);
        break;
    case UI_SCREEN_ZONE_WINDOW:
    case UI_SCREEN_ZONE_OUTSIDE:
    case UI_SCREEN_ZONE_BLOCK:
        break;
    }
}
