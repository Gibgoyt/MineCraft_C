/* See README.md. The Owner: it holds the memory its parts borrow, and decides when that dies. */
#include "ui/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"

#include <linux/input-event-codes.h>
#include <string.h>

enum : uint64_t {
    UI_REGION_BYTES = 2ull << 20,
    /* Plan 004 phase 09: past the two arenas, the Atlas, its colour table and its region, in the same reservation. */
    UI_ATLAS_OFFSET = UI_REGION_BYTES,
    UI_ATLAS_COLOURS_OFFSET = UI_ATLAS_OFFSET + 4096u,
    UI_ATLAS_REGION_OFFSET = UI_ATLAS_OFFSET + (2ull << 20),
    UI_RESERVE_BYTES = UI_ATLAS_REGION_OFFSET + UI_ATLAS_REGION_BYTES,
    UI_ARENA_TABLES_BYTES =
        3ull
        << 19, /* 1.5 MiB: the widgets' textures take 0.4, the row 64 KiB, the HUD's and every item's 0.3, the screens' items 0.3, and later parts the rest */
    UI_ARENA_FRAME_BYTES = UI_REGION_BYTES - UI_ARENA_TABLES_BYTES,
};
static_assert((uint64_t)UI_WIDGET_TOOLTIP_MAX <= (uint64_t)UI_ARENA_FRAME_BYTES);
static_assert(sizeof(ui_atlas_t) <= 4096u);
static_assert(UI_ATLAS_COLOURS_OFFSET + (uint64_t)BLOCK_COUNT * sizeof(uint16_t) <= UI_ATLAS_REGION_OFFSET);

/* Each block's top face's mean colour, packed: what the Atlas paints a column's top with. */
static bool ui_atlas_colours_build(uint16_t *colours, const texture_t *texture) {
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        colours[block] = 0u;
        const uint16_t face = block_defs[block].faces[BLOCK_FACE_TOP];
        uint32_t id = 0u;
        texture_info_t info;
        if (face >= BLOCK_TEXTURE_COUNT || !texture_find(texture, block_texture_names[face], &id) ||
            !texture_info_get(texture, id, &info)) {
            continue; /* air has no texture, and no colour */
        }
        const uint8_t *const rgba = info.borrowed_entry->mean_rgba;
        colours[block] = ui_atlas_rgb565_of((uint32_t)rgba[0] << 16 | (uint32_t)rgba[1] << 8 | rgba[2]);
    }
    return true;
}

bool ui_init(ui_t *ui, const texture_t *texture) {
    memset(ui, 0, sizeof(*ui));
    if (!heap_reserve(UI_RESERVE_BYTES, &ui->owned_region)) {
        return false; /* it has said why */
    }
    arena_init(&ui->arena_tables, "ui_tables", ui->owned_region.owned_base, UI_ARENA_TABLES_BYTES);
    arena_init(&ui->arena_frame, "ui_frame", ui->owned_region.owned_base + UI_ARENA_TABLES_BYTES, UI_ARENA_FRAME_BYTES);
    ui->borrowed_row = arena_alloc(&ui->arena_tables, UI_CANVAS_ROW_BYTES, alignof(uint32_t));
    if (!ui_font_init(&ui->font, &ui->arena_tables, texture) ||
        !ui_widget_init(&ui->widget, &ui->arena_tables, texture, &ui->font, &ui->arena_frame) ||
        !ui_hud_init(&ui->hud, &ui->arena_tables, texture) ||
        !ui_menu_init(&ui->menu, &ui->arena_tables, texture, &ui->widget)) {
        ui_deinit(ui);
        return false; /* it has said why */
    }
    ui->borrowed_screens = ui_screens_make(&ui->arena_tables, texture);
    if (ui->borrowed_screens == nullptr) {
        ui_deinit(ui);
        return false; /* it has said why */
    }
    ui_screens_begin(ui->borrowed_screens);
    memset(ui_atlas(ui), 0, sizeof(ui_atlas_t));
    (void)ui_atlas_colours_build((uint16_t *)(void *)(ui->owned_region.owned_base + UI_ATLAS_COLOURS_OFFSET), texture);
    return true;
}

void ui_deinit(ui_t *ui) {
    if (ui->owned_region.owned_base != nullptr) {
        ui_atlas_world_close(ui);
    }
    ui_menu_deinit(&ui->menu);
    ui_hud_deinit(&ui->hud);
    ui_widget_deinit(&ui->widget);
    ui_font_deinit(&ui->font);
    arena_deinit(&ui->arena_frame);
    arena_deinit(&ui->arena_tables);
    heap_release(&ui->owned_composite);
    heap_release(&ui->owned_region);
    memset(ui, 0, sizeof(*ui));
}

bool ui_frame_begin(ui_t *ui, uint8_t *pixels, uint64_t pitch, uint32_t width, uint32_t height) {
    ASSERT(ui->widget.borrowed_canvas == nullptr);
    if (!ui_canvas_start(&ui->canvas, pixels, pitch, width, height, ui->borrowed_row)) {
        return false;
    }
    arena_reset(&ui->arena_frame);
    ui_widget_frame_begin(&ui->widget, &ui->canvas);
    return true;
}

/* A frame's pixels. A size that has been seen before costs nothing: a mode changes far less often than a frame comes. */
static bool ui_composite_ready(ui_t *ui, uint32_t width, uint32_t height, uint64_t *out_bytes) {
    const uint64_t bytes = (uint64_t)width * sizeof(uint32_t) * height;
    if (bytes == 0u || width > UI_CANVAS_MAX_WIDTH) {
        return false;
    }
    if (bytes > ui->owned_composite.bytes) {
        heap_release(&ui->owned_composite);
        if (!heap_reserve(bytes, &ui->owned_composite)) {
            return false; /* it has said why */
        }
    }
    *out_bytes = bytes;
    return true;
}

bool ui_frame_begin_over(ui_t *ui, const uint8_t *picture, uint32_t width, uint32_t height) {
    uint64_t bytes = 0u;
    if (!ui_composite_ready(ui, width, height, &bytes)) {
        return false;
    }
    memcpy(ui->owned_composite.owned_base, picture, bytes);
    return ui_frame_begin(ui, ui->owned_composite.owned_base, (uint64_t)width * sizeof(uint32_t), width, height);
}

bool ui_frame_begin_blank(ui_t *ui, uint32_t width, uint32_t height) {
    uint64_t bytes = 0u;
    if (!ui_composite_ready(ui, width, height, &bytes)) {
        return false;
    }
    memset(ui->owned_composite.owned_base, 0, bytes);
    return ui_frame_begin(ui, ui->owned_composite.owned_base, (uint64_t)width * sizeof(uint32_t), width, height);
}

const uint8_t *ui_frame_end(ui_t *ui, bool show_pointer) {
    ui_widget_frame_end(&ui->widget, show_pointer);
    return ui->canvas.borrowed_pixels;
}

/* ---- The Atlas ------------------------------------------------------------------------------------------------------ */

ui_atlas_t *ui_atlas(const ui_t *ui) {
    return (ui_atlas_t *)(void *)(ui->owned_region.owned_base + UI_ATLAS_OFFSET);
}

const uint16_t *ui_atlas_colours(const ui_t *ui) {
    return (const uint16_t *)(const void *)(ui->owned_region.owned_base + UI_ATLAS_COLOURS_OFFSET);
}

bool ui_atlas_world_open(ui_t *ui, const char *directory, uint64_t seed, job_system_t *system,
                         const climate_t *climate) {
    ui_atlas_t *const atlas = ui_atlas(ui);
    ui_atlas_store_stop(atlas);
    return ui_atlas_store_start(atlas, ui->owned_region.owned_base + UI_ATLAS_REGION_OFFSET, UI_ATLAS_REGION_BYTES,
                                directory, seed, ui_atlas_colours(ui), system, climate, &ui->font);
}

void ui_atlas_world_close(ui_t *ui) {
    ui_atlas_store_stop(ui_atlas(ui));
}

/* ---- The game screens ------------------------------------------------------------------------------------------------ */

void ui_screen_world_begin(ui_t *ui) {
    ui_screens_begin(ui->borrowed_screens);
}

static uint32_t ui_screen_places_of(uint8_t kind, ui_screen_place_t *out) {
    switch ((ui_screen_kind_t)kind) {
    case UI_SCREEN_INVENTORY:
        return ui_screen_inventory_places(out);
    case UI_SCREEN_CRAFTING:
    case UI_SCREEN_WOODWORKING:
        return ui_screen_crafting_places(out);
    case UI_SCREEN_FURNACE:
        return ui_screen_furnace_places(out);
    case UI_SCREEN_CHEST:
        return ui_screen_chest_places(out);
    case UI_SCREEN_NONE:
        break;
    }
    return 0u;
}

bool ui_screen_input(ui_t *ui, const input_frame_t *frame, uint32_t width, uint32_t height) {
    ui_screens_t *const screens = ui->borrowed_screens;
    ui_screens_view_take(screens, &ui->widget);
    const uint8_t kind = screens->borrowed_shown->kind;
    if (kind == UI_SCREEN_NONE) {
        if (input_keys_has(&frame->pressed, KEY_E)) {
            ui_screens_send(
                screens, (ui_screen_action_t){.op = UI_SCREEN_OP_OPEN_INVENTORY, .zone = 0u, .slot = 0u, .flags = 0u});
        }
        return false;
    }
    ui_widget_input_feed(&ui->widget, frame);
    if (input_keys_has(&frame->pressed, KEY_E) || input_keys_has(&frame->pressed, KEY_ESC)) {
        ui_screens_send(screens, (ui_screen_action_t){.op = UI_SCREEN_OP_CLOSE, .zone = 0u, .slot = 0u, .flags = 0u});
        return true;
    }
    const bool left = input_keys_has(&frame->pressed, BTN_LEFT);
    const bool right = input_keys_has(&frame->pressed, BTN_RIGHT);
    if (!left && !right) {
        return true;
    }
    ui_screen_place_t places[UI_SCREEN_PLACES_MAX];
    const uint32_t count = ui_screen_places_of(kind, places);
    ui_screen_action_t action =
        ui_screen_hit_test(places, count, width, height, ui->widget.input.pointer_x, ui->widget.input.pointer_y);
    if (action.zone == UI_SCREEN_ZONE_WINDOW) {
        return true;
    }
    if (input_keys_has(&frame->down, KEY_LEFTSHIFT) || input_keys_has(&frame->down, KEY_RIGHTSHIFT)) {
        action.flags |= UI_SCREEN_FLAG_SHIFT;
    }
    if (left) {
        ui_screens_send(screens, action);
    }
    if (right) {
        action.flags |= UI_SCREEN_FLAG_SECONDARY;
        ui_screens_send(screens, action);
    }
    return true;
}

bool ui_screen_up(ui_t *ui) {
    ui_screens_t *const screens = ui->borrowed_screens;
    ui_screens_view_take(screens, &ui->widget);
    return screens->borrowed_shown->kind != UI_SCREEN_NONE;
}

void ui_screen_draw(ui_t *ui) {
    ui_screens_t *const screens = ui->borrowed_screens;
    const ui_screen_view_t *const view = screens->borrowed_shown;
    ui_screen_place_t places[UI_SCREEN_PLACES_MAX];
    const uint32_t count = ui_screen_places_of(view->kind, places);
    ui_screen_draw_base(&ui->widget, screens, view, places, count);
    switch ((ui_screen_kind_t)view->kind) {
    case UI_SCREEN_INVENTORY:
        ui_screen_inventory_draw(&ui->widget, screens, view);
        break;
    case UI_SCREEN_CRAFTING:
        ui_screen_crafting_draw(&ui->widget, screens, view, "Crafting");
        break;
    case UI_SCREEN_WOODWORKING:
        ui_screen_crafting_draw(&ui->widget, screens, view, "Woodworking");
        break;
    case UI_SCREEN_FURNACE:
        ui_screen_furnace_draw(&ui->widget, screens, view);
        break;
    case UI_SCREEN_CHEST:
        ui_screen_chest_draw(&ui->widget);
        break;
    case UI_SCREEN_NONE:
        break;
    }
    ui_screen_draw_cursor(&ui->widget, screens, view);
}

const uint8_t *ui_screen_over(ui_t *ui, const uint8_t *picture, uint32_t width, uint32_t height) {
    if (!ui_screen_up(ui) || !ui_frame_begin_over(ui, picture, width, height)) {
        return picture;
    }
    ui_screen_draw(ui);
    return ui_frame_end(ui, true);
}

ui_screen_kind_t ui_screen_kind_of_block(uint16_t block) {
    if (block == BLOCK_ID_CRAFTING_TABLE) {
        return UI_SCREEN_CRAFTING;
    }
    if (block == BLOCK_ID_WOODWORKING_BENCH_WEST || block == BLOCK_ID_WOODWORKING_BENCH_EAST) {
        return UI_SCREEN_WOODWORKING;
    }
    if (block == BLOCK_ID_CHEST) {
        return UI_SCREEN_CHEST;
    }
    return block == BLOCK_ID_FURNACE || block == BLOCK_ID_FURNACE_LIT ? UI_SCREEN_FURNACE : UI_SCREEN_NONE;
}

ui_screen_kind_t ui_screen_sim_kind(const ui_t *ui) {
    return (ui_screen_kind_t)ui->borrowed_screens->kind;
}

const int32_t *ui_screen_sim_cell(const ui_t *ui) {
    return ui->borrowed_screens->cell;
}

/* The sim tells main how things now are, by looking its stores up again: a chest or a furnace moves when another goes. */
static void ui_screen_publish(ui_screens_t *screens, item_t *item) {
    ui_screen_view_t *const view = ui_screens_view_next(screens, item);
    const int32_t *const cell = screens->cell;
    switch ((ui_screen_kind_t)screens->kind) {
    case UI_SCREEN_INVENTORY:
        ui_screen_inventory_view(item, view);
        break;
    case UI_SCREEN_CRAFTING:
        ui_screen_crafting_view(&item->crafting_table, view);
        break;
    case UI_SCREEN_WOODWORKING:
        ui_screen_crafting_view(item->borrowed_crafting_bench, view);
        break;
    case UI_SCREEN_FURNACE: {
        const smelting_furnace_t *const furnace = smelting_at(&item->furnaces, cell[0], cell[1], cell[2]);
        if (furnace != nullptr) {
            ui_screen_furnace_view(furnace, view);
        }
        break;
    }
    case UI_SCREEN_CHEST: {
        const container_chest_t *const chest = container_at(&item->chests, cell[0], cell[1], cell[2]);
        if (chest != nullptr) {
            ui_screen_chest_view(chest, view);
        }
        break;
    }
    case UI_SCREEN_NONE:
        break;
    }
    ui_screens_view_publish(screens);
}

bool ui_screen_sim_open(ui_t *ui, item_t *item, ui_screen_kind_t kind, const int32_t cell[3]) {
    ui_screens_t *const screens = ui->borrowed_screens;
    if (screens->kind != UI_SCREEN_NONE) {
        return false;
    }
    if ((kind == UI_SCREEN_CHEST && container_open(&item->chests, cell[0], cell[1], cell[2]) == nullptr) ||
        (kind == UI_SCREEN_FURNACE && smelting_open(&item->furnaces, cell[0], cell[1], cell[2]) == nullptr)) {
        return false;
    }
    memcpy(screens->cell, cell, sizeof(screens->cell));
    screens->kind = kind;
    screens->opened_count++;
    ui_screen_publish(screens, item);
    return true;
}

/* Out it goes, by the same road as what a broken chest held. */
static void ui_screen_spill(const ui_screen_sim_t *sim, const item_stack_t *stack) {
    if (!item_stack_is_empty(stack)) {
        sim->spill(sim->borrowed_context, stack, sim->cell);
    }
}

void ui_screen_sim_close(ui_t *ui, const ui_screen_sim_t *sim) {
    ui_screens_t *const screens = ui->borrowed_screens;
    if (screens->kind == UI_SCREEN_NONE) {
        return;
    }
    item_t *const item = sim->borrowed_item;
    crafting_grid_t *const grid = screens->kind == UI_SCREEN_INVENTORY     ? &item->crafting_hands
                                  : screens->kind == UI_SCREEN_CRAFTING    ? &item->crafting_table
                                  : screens->kind == UI_SCREEN_WOODWORKING ? item->borrowed_crafting_bench
                                                                           : nullptr;
    if (grid != nullptr && crafting_grid_give_back(grid, &item->inventory) != 0u) {
        for (uint32_t c = 0u; c < crafting_grid_cells(grid); c++) {
            item_stack_t *const cell = crafting_grid_cell(grid, c);
            ui_screen_spill(sim, cell);
            memset(cell, 0, sizeof(*cell));
        }
        crafting_grid_changed(grid);
    }
    (void)inventory_add_stack(&item->inventory, &screens->cursor);
    ui_screen_spill(sim, &screens->cursor);
    screens->cursor = (item_stack_t){0};
    screens->kind = UI_SCREEN_NONE;
    ui_screen_publish(screens, item);
}

/* A click outside the window throws out what the pointer carries, all of it or one. */
static void ui_screen_throw(ui_screens_t *screens, const ui_screen_sim_t *sim, uint32_t flags) {
    item_stack_t thrown = screens->cursor;
    if (item_stack_is_empty(&thrown)) {
        return;
    }
    if ((flags & UI_SCREEN_FLAG_SECONDARY) != 0u) {
        thrown.count = 1u;
        (void)item_stack_take(&screens->cursor, 1u);
    } else {
        screens->cursor = (item_stack_t){0};
    }
    ui_screen_spill(sim, &thrown);
}

static void ui_screen_click(ui_screens_t *screens, const ui_screen_sim_t *sim, ui_screen_action_t action) {
    item_t *const item = sim->borrowed_item;
    const int32_t *const cell = screens->cell;
    if (screens->kind == UI_SCREEN_NONE) {
        return; /* it went before this click arrived */
    }
    screens->click_count++;
    if (action.zone == UI_SCREEN_ZONE_OUTSIDE) {
        ui_screen_throw(screens, sim, action.flags);
        return;
    }
    switch ((ui_screen_kind_t)screens->kind) {
    case UI_SCREEN_INVENTORY:
        ui_screen_inventory_click(item, action, &screens->cursor);
        break;
    case UI_SCREEN_CRAFTING:
        ui_screen_crafting_click(item, &item->crafting_table, action, &screens->cursor);
        break;
    case UI_SCREEN_WOODWORKING:
        ui_screen_crafting_click(item, item->borrowed_crafting_bench, action, &screens->cursor);
        break;
    case UI_SCREEN_FURNACE: {
        smelting_furnace_t *const furnace = smelting_at(&item->furnaces, cell[0], cell[1], cell[2]);
        if (furnace != nullptr) {
            ui_screen_furnace_click(item, furnace, action, &screens->cursor);
        }
        break;
    }
    case UI_SCREEN_CHEST: {
        container_chest_t *const chest = container_at(&item->chests, cell[0], cell[1], cell[2]);
        if (chest != nullptr) {
            ui_screen_chest_click(item, chest, action, &screens->cursor);
        }
        break;
    }
    case UI_SCREEN_NONE:
        break;
    }
}

void ui_screen_sim_step(ui_t *ui, const ui_screen_sim_t *sim, bool tick) {
    ui_screens_t *const screens = ui->borrowed_screens;
    bool changed = false;
    ui_screen_action_t action;
    while (ui_screens_next(screens, &action)) {
        switch ((ui_screen_op_t)action.op) {
        case UI_SCREEN_OP_OPEN_INVENTORY:
            if (screens->kind == UI_SCREEN_NONE && sim->may_open != 0u) {
                screens->kind = UI_SCREEN_INVENTORY;
                screens->opened_count++;
                changed = true;
            }
            break;
        case UI_SCREEN_OP_CLOSE:
            /* Publishes for itself. */
            ui_screen_sim_close(ui, sim);
            break;
        case UI_SCREEN_OP_CLICK:
            ui_screen_click(screens, sim, action);
            changed = true;
            break;
        }
    }
    if (changed || (tick && screens->kind != UI_SCREEN_NONE)) {
        ui_screen_publish(screens, sim->borrowed_item);
    }
}
