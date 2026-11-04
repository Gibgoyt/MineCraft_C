/* ctest run_ui_screens. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "display/headless/index.h"
#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "item/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "main/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    US_WIDTH = 320u, /* scale 1, so that a golden picture is small and a hand can work out where things are */
    US_HEIGHT = 240u,
    US_EVENTS = 1024u,
    US_SCRIPT_BYTES = 16384u,
    US_FIXTURE_PATH_BYTES = 256u,
    US_ALL_STACKS = 128u,
    US_PROGRAM_TIMEOUT_MS = 120000u,
};

/* A ui and what a player carries, with no card and no world: all of this runs on one thread, in the order the two threads would. */
typedef struct us_rig_t {
    texture_t *texture;
    ui_t *ui;
    item_t *item;
    uint8_t *picture; /* a gradient, for what is under the window */
    ui_screen_sim_t sim;
    uint64_t spilled_items;
    item_stack_t spilled_last;
    uint8_t _pad0[8];
} us_rig_t;
static_assert(sizeof(us_rig_t) == 96);

static void us_spill(void *borrowed_rig, const item_stack_t *stack, const int32_t cell[3]) {
    (void)cell;
    us_rig_t *const rig = borrowed_rig;
    rig->spilled_items += stack->count;
    rig->spilled_last = *stack;
}

static bool us_rig_start(test_context_t *t, us_rig_t *rig) {
    memset(rig, 0, sizeof(*rig));
    rig->texture = test_world_aligned(t, sizeof(texture_t));
    rig->ui = test_world_aligned(t, sizeof(ui_t));
    rig->item = test_world_aligned(t, sizeof(item_t));
    rig->picture = test_alloc(t, (uint64_t)US_WIDTH * US_HEIGHT * 4u);
    uint32_t *const pixels = (uint32_t *)(void *)rig->picture;
    for (uint32_t y = 0u; y < US_HEIGHT; y++) {
        for (uint32_t x = 0u; x < US_WIDTH; x++) {
            pixels[y * US_WIDTH + x] =
                0xFF000000u | ((x * 255u / US_WIDTH) << 16u) | ((y * 255u / US_HEIGHT) << 8u) | 0x60u;
        }
    }
    if (!texture_init(rig->texture) || !ui_init(rig->ui, rig->texture) || !item_init(rig->item)) {
        return false;
    }
    item_open(rig->item);
    rig->sim = (ui_screen_sim_t){.borrowed_item = rig->item,
                                 .spill = us_spill,
                                 .borrowed_context = rig,
                                 .cell = {1, 2, 3},
                                 .may_open = 1u,
                                 ._pad0 = {0}};
    return true;
}

static void us_rig_stop(us_rig_t *rig) {
    item_close(rig->item);
    item_deinit(rig->item);
    ui_deinit(rig->ui);
    texture_deinit(rig->texture);
}

static void us_key_set(input_keys_t *keys, uint32_t code) {
    keys->words[code / 64u] |= 1ull << (code % 64u);
}

/* One poll's click at (x, y), with the pointer got there the way a mouse would. */
static void us_click_at(us_rig_t *rig, int32_t x, int32_t y, uint32_t button, bool shift) {
    input_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.mouse_dx = (int64_t)x - rig->ui->widget.input.pointer_x;
    frame.mouse_dy = (int64_t)y - rig->ui->widget.input.pointer_y;
    us_key_set(&frame.pressed, button);
    us_key_set(&frame.down, button);
    if (shift) {
        us_key_set(&frame.down, KEY_LEFTSHIFT);
    }
    (void)ui_screen_input(rig->ui, &frame, US_WIDTH, US_HEIGHT);
    ui_screen_sim_step(rig->ui, &rig->sim, false);
    (void)ui_screen_over(rig->ui, rig->picture, US_WIDTH, US_HEIGHT);
}

static uint32_t us_places_of(ui_screen_kind_t kind, ui_screen_place_t *out) {
    switch (kind) {
    case UI_SCREEN_INVENTORY:
        return ui_screen_inventory_places(out);
    case UI_SCREEN_CRAFTING:
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

static const int32_t US_CELL[3] = {5, 6, 7};

/* Brings a screen up as the game would, and has main see it, with the canvas known. */
static void us_open(us_rig_t *rig, ui_screen_kind_t kind) {
    if (kind == UI_SCREEN_INVENTORY) {
        input_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        us_key_set(&frame.pressed, KEY_E);
        (void)ui_screen_input(rig->ui, &frame, US_WIDTH, US_HEIGHT);
        ui_screen_sim_step(rig->ui, &rig->sim, false);
    } else {
        (void)ui_screen_sim_open(rig->ui, rig->item, kind, US_CELL);
    }
    (void)ui_screen_over(rig->ui, rig->picture, US_WIDTH, US_HEIGHT);
}

/* ---- Layout ---------------------------------------------------------------------------------------------------------------- */

typedef struct us_screen_count_t {
    uint32_t count;
    uint8_t kind;
    uint8_t _pad0[3];
} us_screen_count_t;
static_assert(sizeof(us_screen_count_t) == 8);

TEST(each_screen_has_the_slots_it_should_in_the_window_and_none_on_another) {
    static const us_screen_count_t screens[] = {
        {.count = 36u + 4u + 4u + 1u, .kind = UI_SCREEN_INVENTORY},
        {.count = 36u + 9u + 1u, .kind = UI_SCREEN_CRAFTING},
        {.count = 36u + 3u, .kind = UI_SCREEN_FURNACE},
        {.count = 36u + 27u, .kind = UI_SCREEN_CHEST},
    };
    static const uint32_t sizes[][2] = {{320u, 240u}, {640u, 480u}, {1920u, 1080u}, {321u, 241u}};
    ui_screen_place_t places[UI_SCREEN_PLACES_MAX];
    for (uint32_t s = 0u; s < sizeof(screens) / sizeof(screens[0]); s++) {
        const uint32_t count = us_places_of((ui_screen_kind_t)screens[s].kind, places);
        CHECK_EQ_U64(count, screens[s].count);
        for (uint32_t i = 0u; i < count; i++) {
            CHECK(places[i].x >= 0 && places[i].x + (int32_t)UI_SCREEN_SLOT <= (int32_t)UI_SCREEN_WINDOW_WIDTH);
            CHECK(places[i].y >= 0 && places[i].y + (int32_t)UI_SCREEN_SLOT <= (int32_t)UI_SCREEN_WINDOW_HEIGHT);
            for (uint32_t j = i + 1u; j < count; j++) {
                const bool apart = abs(places[i].x - places[j].x) >= (int32_t)UI_SCREEN_SLOT ||
                                   abs(places[i].y - places[j].y) >= (int32_t)UI_SCREEN_SLOT;
                CHECK(apart);
                CHECK(places[i].zone != places[j].zone || places[i].slot != places[j].slot);
            }
        }
        /* The middle and the corners of every slot, at every size, say that slot. */
        for (uint32_t z = 0u; z < sizeof(sizes) / sizeof(sizes[0]); z++) {
            const uint32_t width = sizes[z][0], height = sizes[z][1];
            const int32_t scale = (int32_t)ui_widget_scale_for(width, height);
            const ui_rect_t window = ui_screen_window(width, height);
            CHECK(window.x >= 0 && window.x + window.width <= (int32_t)width);
            CHECK(window.y >= 0 && window.y + window.height <= (int32_t)height);
            for (uint32_t i = 0u; i < count; i++) {
                const int32_t left = window.x + places[i].x * scale;
                const int32_t top = window.y + places[i].y * scale;
                const int32_t side = (int32_t)UI_SCREEN_SLOT * scale;
                const int32_t xs[3] = {left, left + side / 2, left + side - 1};
                const int32_t ys[3] = {top, top + side / 2, top + side - 1};
                for (uint32_t k = 0u; k < 3u; k++) {
                    const ui_screen_action_t hit = ui_screen_hit_test(places, count, width, height, xs[k], ys[k]);
                    CHECK_EQ_U64(hit.zone, places[i].zone);
                    CHECK_EQ_U64(hit.slot, places[i].slot);
                    CHECK_EQ_U64(hit.op, UI_SCREEN_OP_CLICK);
                }
            }
            CHECK_EQ_U64(ui_screen_hit_test(places, count, width, height, window.x - 1, window.y).zone,
                         UI_SCREEN_ZONE_OUTSIDE);
            CHECK_EQ_U64(ui_screen_hit_test(places, count, width, height, window.x + window.width, window.y).zone,
                         UI_SCREEN_ZONE_OUTSIDE);
            CHECK_EQ_U64(ui_screen_hit_test(places, count, width, height, window.x, window.y).zone,
                         UI_SCREEN_ZONE_WINDOW);
        }
    }
}

TEST(the_player_rows_are_where_a_hand_says) {
    ui_screen_place_t places[UI_SCREEN_PLACES_MAX];
    CHECK_EQ_U64(ui_screen_player_places(places), INVENTORY_SLOTS);
    /* Slot 0 is the hotbar's left, below the rows; slot 9 is the top row's left, and 35 the bottom row's right. */
    CHECK_EQ_I64(places[0].x, 8);
    CHECK_EQ_I64(places[0].y, 142);
    CHECK_EQ_I64(places[8].x, 8 + 8 * 18);
    CHECK_EQ_I64(places[9].x, 8);
    CHECK_EQ_I64(places[9].y, 84);
    CHECK_EQ_I64(places[35].x, 8 + 8 * 18);
    CHECK_EQ_I64(places[35].y, 84 + 2 * 18);
}

TEST(a_block_opens_only_its_own_screen) {
    uint32_t screens = 0u;
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        screens += ui_screen_kind_of_block((uint16_t)block) != UI_SCREEN_NONE ? 1u : 0u;
    }
    CHECK_EQ_U64(screens, 4u);
    CHECK_EQ_U64(ui_screen_kind_of_block(BLOCK_ID_CRAFTING_TABLE), UI_SCREEN_CRAFTING);
    CHECK_EQ_U64(ui_screen_kind_of_block(BLOCK_ID_FURNACE), UI_SCREEN_FURNACE);
    CHECK_EQ_U64(ui_screen_kind_of_block(BLOCK_ID_FURNACE_LIT), UI_SCREEN_FURNACE);
    CHECK_EQ_U64(ui_screen_kind_of_block(BLOCK_ID_CHEST), UI_SCREEN_CHEST);
}

/* ---- The Settings screen's box for coordinates ----------------------------------------------------------------------------- */

/* One frame of the Settings screen, with what the pointer did. The picture it made is left in out. */
static ui_screen_settings_action_t us_settings_frame(us_rig_t *rig, const input_frame_t *frame, uint8_t *out) {
    ui_t *const ui = rig->ui;
    ui_widget_input_feed(&ui->widget, frame);
    if (!ui_frame_begin_blank(ui, US_WIDTH, US_HEIGHT)) {
        return (ui_screen_settings_action_t)255u;
    }
    const ui_screen_settings_action_t action = ui_screen_settings_frame(&ui->settings_screen, false);
    memcpy(out, ui_frame_end(ui, false), (uint64_t)US_WIDTH * US_HEIGHT * 4u);
    return action;
}

/* A click, and the picture of the frame after, with no button on it and the pointer still over the same place. */
static ui_screen_settings_action_t us_settings_click(us_rig_t *rig, int32_t x, int32_t y, uint8_t *out) {
    input_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.mouse_dx = (int64_t)x - rig->ui->widget.input.pointer_x;
    frame.mouse_dy = (int64_t)y - rig->ui->widget.input.pointer_y;
    (void)us_settings_frame(rig, &frame, out);
    memset(&frame, 0, sizeof(frame));
    us_key_set(&frame.pressed, BTN_LEFT);
    us_key_set(&frame.down, BTN_LEFT);
    (void)us_settings_frame(rig, &frame, out);
    memset(&frame, 0, sizeof(frame));
    us_key_set(&frame.released, BTN_LEFT);
    const ui_screen_settings_action_t action = us_settings_frame(rig, &frame, out);
    memset(&frame, 0, sizeof(frame));
    (void)us_settings_frame(rig, &frame, out);
    return action;
}

TEST(the_coordinates_box_changes_that_setting_and_nothing_else_and_shows_it) {
    /* At 320 by 240 the scale is 1: row 6 starts at 60 across and 24 + 6 * 24 down, is 20 tall, and the box starts 50 along it. */
    enum : int32_t { BOX_X = 114 + 4, BOX_Y = 168 + 10 };
    us_rig_t *const rig = test_alloc(t, sizeof(us_rig_t));
    REQUIRE(us_rig_start(t, rig));
    settings_values_t values;
    settings_persist_defaults(&values);
    CHECK_EQ_U64(values.show_coordinates, 1u);
    settings_values_t expected = values;
    ui_screen_settings_open(&rig->ui->settings_screen, &rig->ui->widget, &values, 0u, MC_THREADS_WORKERS, false);
    const uint64_t bytes = (uint64_t)US_WIDTH * US_HEIGHT * 4u;
    uint8_t *const on = test_alloc(t, bytes);
    uint8_t *const off = test_alloc(t, bytes);
    uint8_t *const on_again = test_alloc(t, bytes);
    const input_frame_t nothing = {0};
    (void)us_settings_frame(rig, &nothing, on); /* so that the widgets know the canvas */

    /* Hovering shows, so what is compared is the box over the box, with the pointer there each time. */
    input_frame_t over;
    memset(&over, 0, sizeof(over));
    over.mouse_dx = (int64_t)BOX_X - rig->ui->widget.input.pointer_x;
    over.mouse_dy = (int64_t)BOX_Y - rig->ui->widget.input.pointer_y;
    CHECK_EQ_U64(us_settings_frame(rig, &over, on), UI_SCREEN_SETTINGS_NONE);
    CHECK_EQ_U64(us_settings_click(rig, BOX_X, BOX_Y, off), UI_SCREEN_SETTINGS_CHANGED);
    expected.show_coordinates = 0u;
    CHECK_MEM_EQ(&values, &expected, sizeof(expected));
    CHECK(memcmp(on, off, bytes) != 0);
    CHECK_EQ_U64(us_settings_click(rig, BOX_X, BOX_Y, on_again), UI_SCREEN_SETTINGS_CHANGED);
    expected.show_coordinates = 1u;
    CHECK_MEM_EQ(&values, &expected, sizeof(expected));
    CHECK_MEM_EQ(on, on_again, bytes);

    /* It is the box's own business: VSync, on its left, and Effects..., on its right, have not moved under the pointer. */
    CHECK_EQ_U64(us_settings_click(rig, 60 + 4, BOX_Y, off), UI_SCREEN_SETTINGS_CHANGED);
    CHECK_EQ_U64(values.vsync, 0u);
    CHECK_EQ_U64(values.show_coordinates, 1u);
    (void)us_settings_click(rig, 260 - 4, BOX_Y, off);
    CHECK_EQ_U64(rig->ui->settings_screen.page, UI_SCREEN_SETTINGS_PAGE_EFFECTS);
    us_rig_stop(rig);
}

/* ---- Replays and golden pictures -------------------------------------------------------------------------------------------- */

static void us_golden_compare(test_context_t *t, const char *path, const uint8_t *picture) {
    const uint64_t capacity = display_png_bytes_needed(US_WIDTH, US_HEIGHT);
    uint8_t *const ours = test_alloc(t, capacity);
    uint8_t *const file = test_alloc(t, capacity);
    uint64_t written = 0u;
    REQUIRE(display_png_encode(picture, US_WIDTH * 4u, US_WIDTH, US_HEIGHT, ours, capacity, &written));
    if (getenv("MC_GOLDEN_WRITE") != nullptr) {
        REQUIRE(file_write_atomic(path, ours, written) == FILE_OK);
        printf("  wrote %s\n", path);
        return;
    }
    uint64_t length = 0u;
    if (!CHECK_EQ_U64(file_read_whole(path, file, capacity, &length), FILE_OK)) {
        printf("  cannot read %s: run with MC_GOLDEN_WRITE=1, then look at what it wrote\n", path);
        return;
    }
    /* The writer stores its pixels whole, so the same pixels make the same bytes, and any other pixels do not. */
    CHECK_EQ_U64(length, written);
    uint64_t wrong = 0u;
    for (uint64_t i = 0u; i < written && i < length; i++) {
        wrong += ours[i] != file[i] ? 1u : 0u;
    }
    printf("  %s: %" PRIu64 " bytes differ\n", path, wrong);
    CHECK_EQ_U64(wrong, 0u);
}

/* A script's ticks through exactly what game_scene_tick and game_scene_draw call, with every screenshot held to its picture.
 * tick_furnace, if not nullptr, burns once a tick. */
static void us_replay(test_context_t *t, us_rig_t *rig, const char *name, smelting_furnace_t *tick_furnace) {
    char path[US_FIXTURE_PATH_BYTES];
    (void)snprintf(path, sizeof(path), "tests/run/ui/screens/fixtures/%s.script", name);
    char *const text = test_alloc(t, US_SCRIPT_BYTES);
    uint64_t length = 0u;
    REQUIRE(file_read_whole(path, text, US_SCRIPT_BYTES, &length) == FILE_OK);
    display_script_event_t *const events = test_alloc(t, US_EVENTS * sizeof(display_script_event_t));
    display_script_t script;
    display_script_error_t error;
    const bool parsed = display_script_parse(text, length, MC_GAME_TICK_HZ, events, US_EVENTS, &script, &error);
    if (!parsed) {
        printf("  %s:%" PRIu32 ": %s\n", path, error.line, error.message);
    }
    REQUIRE(parsed);
    display_script_state_t *const state = test_alloc(t, sizeof(display_script_state_t));
    uint32_t next = 0u;
    for (uint32_t tick = 0u; tick <= script.last_tick; tick++) {
        display_script_state_begin_tick(state, tick);
        const uint32_t first = next;
        while (next < script.event_count && events[next].tick == tick) {
            display_script_state_apply(state, &events[next]);
            next++;
        }
        const display_headless_tick_t info = {.events = next > first ? events + first : nullptr,
                                              .state = state,
                                              .event_count = next - first,
                                              .tick = tick};
        input_frame_t frame;
        game_input_from_script(&info, &frame);
        (void)ui_screen_input(rig->ui, &frame, US_WIDTH, US_HEIGHT);
        if (tick_furnace != nullptr) {
            (void)smelting_tick(&rig->item->furnaces, tick_furnace);
        }
        ui_screen_sim_step(rig->ui, &rig->sim, true);
        const uint8_t *const out = ui_screen_over(rig->ui, rig->picture, US_WIDTH, US_HEIGHT);
        for (uint32_t e = first; e < next; e++) {
            if (events[e].kind == DISPLAY_SCRIPT_SCREENSHOT) {
                char shot[DISPLAY_SCRIPT_NAME_BYTES];
                display_script_screenshot_name(&script, &events[e], shot);
                (void)snprintf(path, sizeof(path), "tests/run/ui/screens/fixtures/%s", shot);
                us_golden_compare(t, path, out);
            }
        }
    }
}

TEST(the_inventory_replay_crafts_by_hand_wears_a_helmet_and_looks_as_it_should) {
    us_rig_t rig;
    REQUIRE(us_rig_start(t, &rig));
    inventory_t *const inventory = &rig.item->inventory;
    *inventory_slot(inventory, 0u) = item_stack_make(ITEM_ID_OAK_LOG, 10u);
    *inventory_slot(inventory, 1u) = item_stack_make(ITEM_ID_IRON_PICKAXE, 1u);
    inventory_slot(inventory, 1u)->wear = 100u;
    *inventory_slot(inventory, 9u) = item_stack_make(ITEM_ID_IRON_HELMET, 1u);
    *inventory_slot(inventory, 10u) = item_stack_make(ITEM_ID_COAL, 64u);
    *inventory_slot(inventory, 20u) = item_stack_make(ITEM_ID_BREAD, 5u);
    inventory->added_count = 81u;
    us_replay(t, &rig, "inventory", nullptr);
    CHECK_EQ_U64(ui_screen_sim_kind(rig.ui), UI_SCREEN_NONE);
    CHECK_EQ_U64(inventory_slot(inventory, 0u)->item, ITEM_ID_OAK_LOG);
    CHECK_EQ_U64(inventory_slot(inventory, 0u)->count, 9u);
    CHECK_EQ_U64(inventory_slot(inventory, 3u)->item, ITEM_ID_OAK_PLANKS);
    CHECK_EQ_U64(inventory_slot(inventory, 3u)->count, 4u);
    CHECK(item_stack_is_empty(inventory_slot(inventory, 9u)));
    CHECK_EQ_U64(equipment_armor_worn(&rig.item->equipment, 0u)->item, ITEM_ID_IRON_HELMET);
    for (uint32_t c = 0u; c < 4u; c++) {
        CHECK(item_stack_is_empty(crafting_grid_cell(&rig.item->crafting_hands, c)));
    }
    CHECK(item_stack_is_empty(&rig.ui->borrowed_screens->cursor));
    us_rig_stop(&rig);
}

TEST(the_crafting_replay_makes_a_pickaxe_at_a_table_and_looks_as_it_should) {
    us_rig_t rig;
    REQUIRE(us_rig_start(t, &rig));
    inventory_t *const inventory = &rig.item->inventory;
    *inventory_slot(inventory, 0u) = item_stack_make(ITEM_ID_OAK_PLANKS, 3u);
    *inventory_slot(inventory, 1u) = item_stack_make(ITEM_ID_STICK, 2u);
    us_open(&rig, UI_SCREEN_CRAFTING);
    us_replay(t, &rig, "crafting", nullptr);
    CHECK_EQ_U64(ui_screen_sim_kind(rig.ui), UI_SCREEN_NONE);
    CHECK_EQ_U64(inventory_slot(inventory, 8u)->item, ITEM_ID_WOOD_PICKAXE);
    CHECK_EQ_U64(rig.item->crafting_table.crafted_count, 1u);
    CHECK_EQ_U64(inventory_items_held(inventory), 1u);
    us_rig_stop(&rig);
}

TEST(the_furnace_replay_burns_coal_into_glass_and_looks_as_it_should) {
    us_rig_t rig;
    REQUIRE(us_rig_start(t, &rig));
    inventory_t *const inventory = &rig.item->inventory;
    *inventory_slot(inventory, 0u) = item_stack_make(ITEM_ID_SAND, 2u);
    *inventory_slot(inventory, 1u) = item_stack_make(ITEM_ID_COAL, 1u);
    us_open(&rig, UI_SCREEN_FURNACE);
    smelting_furnace_t *const furnace = smelting_at(&rig.item->furnaces, US_CELL[0], US_CELL[1], US_CELL[2]);
    REQUIRE(furnace != nullptr);
    us_replay(t, &rig, "furnace", furnace);
    CHECK_EQ_U64(ui_screen_sim_kind(rig.ui), UI_SCREEN_NONE);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_GLASS), 2u);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_SAND), 0u);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_COAL), 0u);
    CHECK(item_stack_is_empty(&furnace->slots[SMELTING_SLOT_OUTPUT]));
    us_rig_stop(&rig);
}

TEST(the_chest_replay_moves_stacks_each_way_and_looks_as_it_should) {
    us_rig_t rig;
    REQUIRE(us_rig_start(t, &rig));
    inventory_t *const inventory = &rig.item->inventory;
    *inventory_slot(inventory, 0u) = item_stack_make(ITEM_ID_OAK_LOG, 64u);
    us_open(&rig, UI_SCREEN_CHEST);
    container_chest_t *const chest = container_at(&rig.item->chests, US_CELL[0], US_CELL[1], US_CELL[2]);
    REQUIRE(chest != nullptr);
    chest->slots[0] = item_stack_make(ITEM_ID_IRON_PICKAXE, 1u);
    chest->slots[4] = item_stack_make(ITEM_ID_COAL, 5u);
    chest->slots[13] = item_stack_make(ITEM_ID_DIAMOND, 3u);
    us_replay(t, &rig, "chest", nullptr);
    CHECK_EQ_U64(ui_screen_sim_kind(rig.ui), UI_SCREEN_NONE);
    /* The logs went in by shift to the first free slot; half the diamonds rounded up and the coal came out. */
    CHECK_EQ_U64(chest->slots[1].item, ITEM_ID_OAK_LOG);
    CHECK_EQ_U64(chest->slots[1].count, 64u);
    CHECK_EQ_U64(chest->slots[13].count, 1u);
    CHECK(item_stack_is_empty(&chest->slots[4]));
    CHECK_EQ_U64(chest->slots[0].item, ITEM_ID_IRON_PICKAXE);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_DIAMOND), 2u);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_COAL), 5u);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_OAK_LOG), 0u);
    us_rig_stop(&rig);
}

/* ---- What a click does ------------------------------------------------------------------------------------------------------ */

static uint64_t us_rng_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* Everything in reach of a screen of this kind. */
static uint32_t us_all_stacks(us_rig_t *rig, ui_screen_kind_t kind, const item_stack_t **out) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < INVENTORY_SLOTS; i++) {
        out[count++] = inventory_slot_read(&rig->item->inventory, i);
    }
    for (uint32_t i = 0u; i < EQUIPMENT_ARMOR_SLOTS; i++) {
        out[count++] = equipment_armor_worn(&rig->item->equipment, i);
    }
    for (uint32_t i = 0u; i < RECIPE_CELLS; i++) {
        if (kind == UI_SCREEN_INVENTORY && i < 4u) {
            out[count++] = &rig->item->crafting_hands.cells[i];
        } else if (kind == UI_SCREEN_CRAFTING) {
            out[count++] = &rig->item->crafting_table.cells[i];
        }
    }
    if (kind == UI_SCREEN_FURNACE) {
        const smelting_furnace_t *const furnace = smelting_at(&rig->item->furnaces, US_CELL[0], US_CELL[1], US_CELL[2]);
        for (uint32_t i = 0u; i < SMELTING_SLOTS; i++) {
            out[count++] = &furnace->slots[i];
        }
    }
    if (kind == UI_SCREEN_CHEST) {
        container_chest_t *const chest = container_at(&rig->item->chests, US_CELL[0], US_CELL[1], US_CELL[2]);
        for (uint32_t i = 0u; i < CONTAINER_CHEST_SLOTS; i++) {
            out[count++] = container_slot(chest, i);
        }
    }
    out[count++] = &rig->ui->borrowed_screens->cursor;
    return count;
}

TEST(random_clicks_on_every_screen_lose_nothing_and_keep_the_books_and_every_stack_good) {
    static const ui_screen_kind_t kinds[] = {UI_SCREEN_INVENTORY, UI_SCREEN_CRAFTING, UI_SCREEN_FURNACE,
                                             UI_SCREEN_CHEST};
    static const uint16_t items[] = {ITEM_ID_OAK_LOG,     ITEM_ID_COAL,       ITEM_ID_SAND,  ITEM_ID_IRON_PICKAXE,
                                     ITEM_ID_IRON_HELMET, ITEM_ID_IRON_BOOTS, ITEM_ID_BREAD, ITEM_ID_OAK_PLANKS};
    for (uint32_t k = 0u; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        us_rig_t rig;
        REQUIRE(us_rig_start(t, &rig));
        inventory_t *const inventory = &rig.item->inventory;
        uint64_t state = 0x9E3779B97F4A7C15ull + k;
        uint32_t *const want = test_alloc(t, (uint64_t)ITEM_COUNT * sizeof(uint32_t));
        for (uint32_t i = 0u; i < INVENTORY_SLOTS; i += 2u) {
            const uint16_t item = items[us_rng_next(&state) % (sizeof(items) / sizeof(items[0]))];
            const uint32_t count = 1u + (uint32_t)(us_rng_next(&state) % item_stack_max(item));
            *inventory_slot(inventory, i) = item_stack_make(item, count);
            if (item_defs[item].durability != 0u && us_rng_next(&state) % 2u == 0u) {
                inventory_slot(inventory, i)->wear = (uint16_t)(us_rng_next(&state) % 50u);
            }
            want[item] += count;
            inventory->added_count += count;
        }
        us_open(&rig, kinds[k]);
        ui_screen_place_t places[UI_SCREEN_PLACES_MAX];
        const uint32_t count = us_places_of(kinds[k], places);
        const ui_rect_t window = ui_screen_window(US_WIDTH, US_HEIGHT);
        const item_stack_t *stacks[US_ALL_STACKS];
        const uint32_t stack_count = us_all_stacks(&rig, kinds[k], stacks);
        uint32_t *const have = test_alloc(t, (uint64_t)ITEM_COUNT * sizeof(uint32_t));
        for (uint32_t step = 0u; step < 20000u; step++) {
            const ui_screen_place_t *const place = &places[us_rng_next(&state) % count];
            /* What a grid makes is new, so its output is the one place this cannot count. It has tests of its own. */
            if (place->zone == UI_SCREEN_ZONE_OUTPUT) {
                continue;
            }
            const bool secondary = us_rng_next(&state) % 2u == 0u;
            const bool shift = us_rng_next(&state) % 4u == 0u;
            us_click_at(&rig, window.x + place->x + 9, window.y + place->y + 9, secondary ? BTN_RIGHT : BTN_LEFT,
                        shift);
            if (step % 64u != 0u) {
                continue;
            }
            memset(have, 0, (uint64_t)ITEM_COUNT * sizeof(uint32_t));
            bool good = true;
            for (uint32_t s = 0u; s < stack_count; s++) {
                good = good && item_stack_is_valid(stacks[s]);
                have[stacks[s]->item] += stacks[s]->count;
            }
            CHECK(good);
            for (uint32_t item = 1u; item < (uint32_t)ITEM_COUNT; item++) {
                CHECK_EQ_U64(have[item], want[item]);
            }
            CHECK_EQ_U64(inventory->added_count - inventory->removed_count, inventory_items_held(inventory));
        }
        /* Closing brings home all a screen had out, unless a chest or furnace is what holds it. */
        ui_screen_sim_close(rig.ui, &rig.sim);
        CHECK(item_stack_is_empty(&rig.ui->borrowed_screens->cursor));
        CHECK_EQ_U64(inventory->added_count - inventory->removed_count, inventory_items_held(inventory));
        CHECK_EQ_U64(rig.spilled_items, 0u);
        us_rig_stop(&rig);
    }
}

TEST(closing_with_no_room_lets_fall_what_it_cannot_put_back) {
    us_rig_t rig;
    REQUIRE(us_rig_start(t, &rig));
    inventory_t *const inventory = &rig.item->inventory;
    *inventory_slot(inventory, 0u) = item_stack_make(ITEM_ID_COAL, 3u);
    *inventory_slot(inventory, 1u) = item_stack_make(ITEM_ID_DIAMOND, 2u);
    us_open(&rig, UI_SCREEN_INVENTORY);
    const ui_rect_t window = ui_screen_window(US_WIDTH, US_HEIGHT);
    /* The coal on the pointer, the diamonds in the grid, and then no room anywhere. */
    us_click_at(&rig, window.x + 8 + 18 + 9, window.y + 142 + 9, BTN_LEFT, false);
    us_click_at(&rig, window.x + 88 + 9, window.y + 26 + 9, BTN_LEFT, false);
    us_click_at(&rig, window.x + 8 + 9, window.y + 142 + 9, BTN_LEFT, false);
    CHECK_EQ_U64(rig.ui->borrowed_screens->cursor.item, ITEM_ID_COAL);
    CHECK_EQ_U64(crafting_grid_cell(&rig.item->crafting_hands, 0u)->item, ITEM_ID_DIAMOND);
    for (uint32_t i = 0u; i < INVENTORY_SLOTS; i++) {
        *inventory_slot(inventory, i) = item_stack_make(ITEM_ID_OAK_LOG, 64u);
    }
    ui_screen_sim_close(rig.ui, &rig.sim);
    CHECK_EQ_U64(rig.spilled_items, 5u);
    CHECK_EQ_U64(rig.spilled_last.item, ITEM_ID_COAL);
    CHECK(item_stack_is_empty(&rig.ui->borrowed_screens->cursor));
    CHECK(item_stack_is_empty(crafting_grid_cell(&rig.item->crafting_hands, 0u)));
    CHECK_EQ_U64(ui_screen_sim_kind(rig.ui), UI_SCREEN_NONE);
    us_rig_stop(&rig);
}

TEST(a_click_outside_throws_out_all_or_one_and_a_click_on_the_window_nothing) {
    us_rig_t rig;
    REQUIRE(us_rig_start(t, &rig));
    inventory_t *const inventory = &rig.item->inventory;
    *inventory_slot(inventory, 0u) = item_stack_make(ITEM_ID_COAL, 5u);
    us_open(&rig, UI_SCREEN_INVENTORY);
    const ui_rect_t window = ui_screen_window(US_WIDTH, US_HEIGHT);
    us_click_at(&rig, window.x + 8 + 9, window.y + 142 + 9, BTN_LEFT, false);
    us_click_at(&rig, window.x + 60, window.y + 4, BTN_LEFT, false);
    CHECK_EQ_U64(rig.ui->borrowed_screens->cursor.count, 5u);
    us_click_at(&rig, 2, 2, BTN_RIGHT, false);
    CHECK_EQ_U64(rig.spilled_items, 1u);
    CHECK_EQ_U64(rig.spilled_last.count, 1u);
    CHECK_EQ_U64(rig.ui->borrowed_screens->cursor.count, 4u);
    us_click_at(&rig, 2, 2, BTN_LEFT, false);
    CHECK_EQ_U64(rig.spilled_items, 5u);
    CHECK_EQ_U64(rig.spilled_last.item, ITEM_ID_COAL);
    CHECK(item_stack_is_empty(&rig.ui->borrowed_screens->cursor));
    us_click_at(&rig, 2, 2, BTN_LEFT, false);
    CHECK_EQ_U64(rig.spilled_items, 5u);
    us_rig_stop(&rig);
}

TEST(a_full_ring_counts_what_it_loses_and_keeps_the_order_of_the_rest) {
    us_rig_t rig;
    REQUIRE(us_rig_start(t, &rig));
    ui_screens_t *const screens = rig.ui->borrowed_screens;
    for (uint32_t i = 0u; i < UI_SCREEN_RING + 5u; i++) {
        ui_screens_send(screens,
                        (ui_screen_action_t){.op = UI_SCREEN_OP_CLICK, .zone = 2u, .slot = (uint8_t)i, .flags = 0u});
    }
    CHECK_EQ_U64(screens->sent_count, UI_SCREEN_RING);
    CHECK_EQ_U64(screens->lost_count, 5u);
    ui_screen_action_t action;
    for (uint32_t i = 0u; i < UI_SCREEN_RING; i++) {
        REQUIRE(ui_screens_next(screens, &action));
        CHECK_EQ_U64(action.slot, i);
    }
    CHECK(!ui_screens_next(screens, &action));
    us_rig_stop(&rig);
}

TEST(what_is_shown_is_a_copy_that_changes_only_when_the_sim_says) {
    us_rig_t rig;
    REQUIRE(us_rig_start(t, &rig));
    inventory_t *const inventory = &rig.item->inventory;
    *inventory_slot(inventory, 0u) = item_stack_make(ITEM_ID_COAL, 5u);
    us_open(&rig, UI_SCREEN_INVENTORY);
    const ui_screens_t *const screens = rig.ui->borrowed_screens;
    CHECK_EQ_U64(screens->borrowed_shown->kind, UI_SCREEN_INVENTORY);
    CHECK_EQ_U64(screens->borrowed_shown->player[0].count, 5u);
    inventory_slot(inventory, 0u)->count = 3u;
    ui_screen_sim_step(rig.ui, &rig.sim, false);
    (void)ui_screen_over(rig.ui, rig.picture, US_WIDTH, US_HEIGHT);
    CHECK_EQ_U64(screens->borrowed_shown->player[0].count, 5u);
    ui_screen_sim_step(rig.ui, &rig.sim, true);
    (void)ui_screen_over(rig.ui, rig.picture, US_WIDTH, US_HEIGHT);
    CHECK_EQ_U64(screens->borrowed_shown->player[0].count, 3u);
    us_rig_stop(&rig);
}

TEST(with_nothing_up_a_picture_goes_through_untouched_and_a_body_with_none_opens_nothing) {
    us_rig_t rig;
    REQUIRE(us_rig_start(t, &rig));
    CHECK(ui_screen_over(rig.ui, rig.picture, US_WIDTH, US_HEIGHT) == rig.picture);
    rig.sim.may_open = 0u;
    us_open(&rig, UI_SCREEN_INVENTORY);
    CHECK_EQ_U64(ui_screen_sim_kind(rig.ui), UI_SCREEN_NONE);
    CHECK(ui_screen_over(rig.ui, rig.picture, US_WIDTH, US_HEIGHT) == rig.picture);
    us_rig_stop(&rig);
}

TEST(a_picture_is_left_alone_and_a_screen_draws_nothing_outside_its_canvas_at_any_scale) {
    us_rig_t rig;
    REQUIRE(us_rig_start(t, &rig));
    us_open(&rig, UI_SCREEN_CHEST);
    const uint64_t bytes = (uint64_t)US_WIDTH * US_HEIGHT * 4u;
    uint8_t *const before = test_alloc(t, bytes);
    memcpy(before, rig.picture, bytes);
    const uint8_t *const out = ui_screen_over(rig.ui, rig.picture, US_WIDTH, US_HEIGHT);
    CHECK(out != rig.picture);
    CHECK_MEM_EQ(rig.picture, before, bytes);
    CHECK(memcmp(out, before, bytes) != 0);
    /* Twice over the same picture is one window: nothing builds up, frame on frame. */
    uint8_t *const first = test_alloc(t, bytes);
    memcpy(first, out, bytes);
    CHECK_MEM_EQ(ui_screen_over(rig.ui, rig.picture, US_WIDTH, US_HEIGHT), first, bytes);
    us_rig_stop(&rig);
}

TEST(no_heap_and_no_table_memory_in_200_frames_of_a_screen_after_the_first) {
    us_rig_t rig;
    REQUIRE(us_rig_start(t, &rig));
    *inventory_slot(&rig.item->inventory, 0u) = item_stack_make(ITEM_ID_COAL, 5u);
    us_open(&rig, UI_SCREEN_INVENTORY);
    heap_counters_t before;
    heap_counters_read(&before);
    const uint64_t tables = rig.ui->arena_tables.used;
    const ui_rect_t window = ui_screen_window(US_WIDTH, US_HEIGHT);
    for (uint32_t i = 0u; i < 200u; i++) {
        us_click_at(&rig, window.x + 8 + 9, window.y + 142 + 9, BTN_LEFT, false);
        ui_screen_sim_step(rig.ui, &rig.sim, true);
    }
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(rig.ui->arena_tables.used, tables);
    printf("  the table arena holds %" PRIu64 " bytes\n", tables);
    us_rig_stop(&rig);
}

/* ---- A real game ------------------------------------------------------------------------------------------------------------- */

typedef struct us_game_t {
    gpu_bridge_t *bridge;
    job_system_t *job;
    world_t *world;
    terrain_t *terrain;
    render_t *render;
    texture_t *texture;
    game_t *game;
    physics_t *physics;
    player_t *player;
    entity_t *entity;
    item_t *item;
    ui_t *ui;
    thread_topology_t *topology;
    display_script_state_t *state;
    display_headless_scene_t scene;
    display_buffer_t target;
    uint32_t tick;
    uint8_t _pad0[4];
} us_game_t;
static_assert(sizeof(us_game_t) == 112u + sizeof(display_headless_scene_t) + sizeof(display_buffer_t) + 8u);

typedef struct us_block_screen_t {
    uint16_t block;
    uint8_t kind;
    uint8_t _pad0[1];
} us_block_screen_t;
static_assert(sizeof(us_block_screen_t) == 4);

static bool us_game_start(test_context_t *t, us_game_t *g) {
    memset(g, 0, sizeof(*g));
    g->bridge = test_world_aligned(t, sizeof(gpu_bridge_t));
    g->job = test_world_aligned(t, sizeof(job_system_t));
    g->world = test_world_aligned(t, sizeof(world_t));
    g->terrain = test_world_aligned(t, sizeof(terrain_t));
    g->render = test_world_aligned(t, sizeof(render_t));
    g->texture = test_world_aligned(t, sizeof(texture_t));
    g->game = test_world_aligned(t, sizeof(game_t));
    g->physics = test_world_aligned(t, sizeof(physics_t));
    g->player = test_world_aligned(t, sizeof(player_t));
    g->entity = test_world_aligned(t, sizeof(entity_t));
    g->item = test_world_aligned(t, sizeof(item_t));
    g->ui = test_world_aligned(t, sizeof(ui_t));
    g->topology = test_alloc(t, sizeof(thread_topology_t));
    g->state = test_alloc(t, sizeof(display_script_state_t));
    if (gpu_bridge_init(g->bridge, gpu_bridge_limits_default()) != GPU_STATUS_OK ||
        !thread_topology_read(g->topology) || !job_init(g->job, g->topology) || !texture_init(g->texture) ||
        !world_init(g->world, world_limits_default()) || !terrain_init(g->terrain, TERRAIN_SEED_DEFAULT) ||
        !physics_init(g->physics) || !entity_init(g->entity, g->physics) ||
        !render_init(g->render, &g->bridge->device_memory, g->job, render_limits_default()) || !item_init(g->item) ||
        !ui_init(g->ui, g->texture)) {
        return false;
    }
    player_init(g->player, g->physics);
    game_init(g->game, g->world, g->terrain, g->render, g->job, g->texture, g->topology);
    game_player_set(g->game, g->player);
    game_entity_set(g->game, g->entity);
    game_item_set(g->game, g->item);
    game_ui_set(g->game, g->ui);
    static const game_config_t small = {.render_distance = 4u, .width = US_WIDTH, .height = US_HEIGHT, ._pad0 = {0}};
    if (!game_open(g->game, &small) || !game_settle(g->game)) {
        return false;
    }
    game_headless_scene(g->game, &g->scene);
    const uint64_t bytes = post_xrgb_bytes(US_WIDTH, US_HEIGHT);
    g->target = (display_buffer_t){.owned_map = test_world_aligned(t, bytes),
                                   .map_bytes = bytes,
                                   .pitch = US_WIDTH * 4u,
                                   .width = US_WIDTH,
                                   .height = US_HEIGHT};
    return true;
}

static void us_game_stop(test_context_t *t, us_game_t *g) {
    game_close(g->game);
    game_deinit(g->game);
    render_deinit(g->render);
    CHECK(gpu_bridge_exit_check(g->bridge));
    ui_deinit(g->ui);
    item_deinit(g->item);
    entity_deinit(g->entity);
    player_deinit(g->player);
    physics_deinit(g->physics);
    terrain_deinit(g->terrain);
    world_deinit(g->world);
    texture_deinit(g->texture);
    job_deinit(g->job);
    gpu_bridge_deinit(g->bridge);
}

/* One tick of the game's own scene, as display_headless_run would give it. */
static void us_game_tick(test_context_t *t, us_game_t *g, const display_script_event_t *events, uint32_t count) {
    display_script_state_begin_tick(g->state, g->tick);
    for (uint32_t i = 0u; i < count; i++) {
        display_script_state_apply(g->state, &events[i]);
    }
    const display_headless_tick_t info = {
        .events = count != 0u ? events : nullptr, .state = g->state, .event_count = count, .tick = g->tick};
    CHECK(g->scene.tick(g->scene.borrowed_context, &info));
    CHECK(g->scene.draw(g->scene.borrowed_context, &info, &g->target));
    g->tick++;
}

static void us_game_ticks(test_context_t *t, us_game_t *g, uint32_t count) {
    for (uint32_t i = 0u; i < count; i++) {
        us_game_tick(t, g, nullptr, 0u);
    }
}

static void us_game_key(test_context_t *t, us_game_t *g, uint32_t code, bool down) {
    const display_script_event_t event = {
        .kind = DISPLAY_SCRIPT_KEY, .code = (uint16_t)code, .pressed = down ? 1u : 0u};
    us_game_tick(t, g, &event, 1u);
}

/* The pointer goes to a slot's middle, with one tick for it to get there. */
static void us_game_point_at(test_context_t *t, us_game_t *g, int32_t window_x, int32_t window_y) {
    const ui_rect_t window = ui_screen_window(US_WIDTH, US_HEIGHT);
    const display_script_event_t event = {
        .kind = DISPLAY_SCRIPT_MOVE,
        .dx = window.x + window_x + 9 - g->ui->widget.input.pointer_x,
        .dy = window.y + window_y + 9 - g->ui->widget.input.pointer_y,
    };
    us_game_tick(t, g, &event, 1u);
}

static void us_game_click(test_context_t *t, us_game_t *g) {
    us_game_key(t, g, BTN_LEFT, true);
    us_game_key(t, g, BTN_LEFT, false);
}

TEST(in_a_real_game_e_and_the_right_button_bring_up_each_screen_and_the_hands_and_feet_stop) {
    us_game_t *const g = test_alloc(t, sizeof(us_game_t));
    REQUIRE(us_game_start(t, g));
    item_t *const item = g->item;
    /* Something placeable in hand, which a screen must keep the hand from placing. */
    REQUIRE(inventory_add(&item->inventory, ITEM_ID_OAK_LOG, 5u) == 5u);

    /* Looks straight down at what it stands on, and has come to rest. */
    const display_script_event_t look = {.kind = DISPLAY_SCRIPT_MOVE, .dx = 0, .dy = 500};
    us_game_tick(t, g, &look, 1u);
    us_game_ticks(t, g, 60u);
    const raycast_hit_t *const hit = &g->player->target.hit;
    REQUIRE(hit->result == RAYCAST_RESULT_HIT);
    const int32_t cell[3] = {hit->cell[0], hit->cell[1], hit->cell[2]};

    /* E brings up the inventory, and the keys stop moving the body. */
    us_game_key(t, g, KEY_E, true);
    us_game_key(t, g, KEY_E, false);
    CHECK_EQ_U64(ui_screen_sim_kind(g->ui), UI_SCREEN_INVENTORY);
    const float64_t before[3] = {g->player->movement.position[0], g->player->movement.position[1],
                                 g->player->movement.position[2]};
    us_game_key(t, g, KEY_W, true);
    us_game_ticks(t, g, 20u);
    CHECK_MEM_EQ(g->player->movement.position, before, sizeof(before));
    us_game_key(t, g, KEY_W, false);
    us_game_key(t, g, KEY_ESC, true);
    us_game_key(t, g, KEY_ESC, false);
    CHECK_EQ_U64(ui_screen_sim_kind(g->ui), UI_SCREEN_NONE);

    static const us_block_screen_t blocks[] = {
        {.block = BLOCK_ID_CHEST, .kind = UI_SCREEN_CHEST},
        {.block = BLOCK_ID_FURNACE, .kind = UI_SCREEN_FURNACE},
        {.block = BLOCK_ID_CRAFTING_TABLE, .kind = UI_SCREEN_CRAFTING},
    };
    const uint64_t bytes = post_xrgb_bytes(US_WIDTH, US_HEIGHT);
    uint8_t *const shown = test_alloc(t, bytes);
    for (uint32_t b = 0u; b < sizeof(blocks) / sizeof(blocks[0]); b++) {
        REQUIRE(world_edit_block_set(&g->world->edit, cell[0], cell[1], cell[2], blocks[b].block) ==
                WORLD_EDIT_STATUS_CHANGED);
        us_game_ticks(t, g, 2u);
        memcpy(shown, g->target.owned_map, bytes);
        CHECK_EQ_U64(hit->block, blocks[b].block);
        /* The button goes down, and it stays down through the screen: no placing, and nothing comes back when it goes. */
        us_game_key(t, g, BTN_RIGHT, true);
        CHECK_EQ_U64(ui_screen_sim_kind(g->ui), blocks[b].kind);
        us_game_ticks(t, g, 3u);
        CHECK(memcmp(shown, g->target.owned_map, bytes) != 0);
        CHECK_EQ_U64(inventory_count(&item->inventory, ITEM_ID_OAK_LOG), 5u);
        CHECK_EQ_U64(g->player->interaction.placed_count, 0u);
        us_game_key(t, g, KEY_E, true);
        us_game_key(t, g, KEY_E, false);
        CHECK_EQ_U64(ui_screen_sim_kind(g->ui), UI_SCREEN_NONE);
        us_game_ticks(t, g, 5u);
        CHECK_EQ_U64(ui_screen_sim_kind(g->ui), UI_SCREEN_NONE);
        CHECK_EQ_U64(inventory_count(&item->inventory, ITEM_ID_OAK_LOG), 5u);
        CHECK_EQ_U64(g->player->interaction.placed_count, 0u);
        us_game_key(t, g, BTN_RIGHT, false);
        us_game_ticks(t, g, 1u);
    }
    CHECK_EQ_U64(g->ui->borrowed_screens->opened_count, 4u);

    /* Into a real chest by the mouse, and the chest breaks with the screen up and logs on the pointer. */
    REQUIRE(world_edit_block_set(&g->world->edit, cell[0], cell[1], cell[2], BLOCK_ID_CHEST) ==
            WORLD_EDIT_STATUS_CHANGED);
    us_game_ticks(t, g, 2u);
    us_game_key(t, g, BTN_RIGHT, true);
    us_game_key(t, g, BTN_RIGHT, false);
    REQUIRE(ui_screen_sim_kind(g->ui) == UI_SCREEN_CHEST);
    us_game_point_at(t, g, 8, 142);
    us_game_click(t, g);
    us_game_point_at(t, g, 8 + 18, 18);
    us_game_key(t, g, BTN_RIGHT, true);
    us_game_key(t, g, BTN_RIGHT, false);
    const container_chest_t *const chest = container_at(&item->chests, cell[0], cell[1], cell[2]);
    REQUIRE(chest != nullptr);
    CHECK_EQ_U64(chest->slots[1].item, ITEM_ID_OAK_LOG);
    CHECK_EQ_U64(chest->slots[1].count, 1u);
    CHECK_EQ_U64(g->ui->borrowed_screens->cursor.count, 4u);
    REQUIRE(world_edit_block_set(&g->world->edit, cell[0], cell[1], cell[2], BLOCK_ID_AIR) ==
            WORLD_EDIT_STATUS_CHANGED);
    us_game_ticks(t, g, 1u);
    CHECK_EQ_U64(ui_screen_sim_kind(g->ui), UI_SCREEN_NONE);
    CHECK(item_stack_is_empty(&g->ui->borrowed_screens->cursor));
    us_game_ticks(t, g, 1u);
    CHECK(container_at(&item->chests, cell[0], cell[1], cell[2]) == nullptr);
    CHECK_EQ_U64(inventory_count(&item->inventory, ITEM_ID_OAK_LOG), 4u);
    game_report(g->game);
    us_game_stop(t, g);
}

/* A click from main, across to the real sim thread, to a real inventory, and back to what main draws. */
TEST(with_the_real_threads_a_click_reaches_the_sim_and_comes_back_to_be_drawn) {
    us_game_t *const g = test_alloc(t, sizeof(us_game_t));
    REQUIRE(us_game_start(t, g));
    REQUIRE(inventory_add(&g->item->inventory, ITEM_ID_COAL, 7u) == 7u);
    REQUIRE(game_loop_start(g->game, nullptr));
    render_fly_input_t none;
    memset(&none, 0, sizeof(none));
    const ui_screens_t *const screens = g->ui->borrowed_screens;
    input_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    us_key_set(&frame.pressed, KEY_E);
    CHECK(!game_screen_input(g->game, &frame));
    const int64_t give_up = time_now_ns() + 30000000000;
    while (time_now_ns() < give_up && screens->borrowed_shown->kind != UI_SCREEN_INVENTORY) {
        game_walk_send(g->game, &none, false, false, time_now_ns());
        CHECK(game_loop_frame(g->game, &none, 0.0f32, &g->target));
        time_sleep_ns(2000000);
    }
    CHECK_EQ_U64(screens->borrowed_shown->kind, UI_SCREEN_INVENTORY);

    const ui_rect_t window = ui_screen_window(US_WIDTH, US_HEIGHT);
    memset(&frame, 0, sizeof(frame));
    frame.mouse_dx = (int64_t)window.x + 8 + 9 - g->ui->widget.input.pointer_x;
    frame.mouse_dy = (int64_t)window.y + 142 + 9 - g->ui->widget.input.pointer_y;
    us_key_set(&frame.pressed, BTN_LEFT);
    us_key_set(&frame.down, BTN_LEFT);
    CHECK(game_screen_input(g->game, &frame));
    const int64_t clicked_ns = time_now_ns();
    while (time_now_ns() < give_up && screens->borrowed_shown->cursor.count == 0u) {
        game_walk_send(g->game, &none, false, false, time_now_ns());
        CHECK(game_loop_frame(g->game, &none, 0.0f32, &g->target));
        time_sleep_ns(1000000);
    }
    const int64_t took_ns = time_now_ns() - clicked_ns;
    printf("  a click was on the screen after %.1f ms\n", (libc_double_t)((float64_t)took_ns * 1e-6f64));
    CHECK_EQ_U64(screens->borrowed_shown->cursor.item, ITEM_ID_COAL);
    CHECK_EQ_U64(screens->borrowed_shown->cursor.count, 7u);
    CHECK_EQ_U64(screens->borrowed_shown->player[0].count, 0u);

    memset(&frame, 0, sizeof(frame));
    us_key_set(&frame.pressed, KEY_ESC);
    CHECK(game_screen_input(g->game, &frame));
    while (time_now_ns() < give_up && screens->borrowed_shown->kind != UI_SCREEN_NONE) {
        game_walk_send(g->game, &none, false, false, time_now_ns());
        CHECK(game_loop_frame(g->game, &none, 0.0f32, &g->target));
        time_sleep_ns(1000000);
    }
    CHECK(game_loop_stop(g->game));
    CHECK_EQ_U64(screens->lost_count, 0u);
    CHECK_EQ_U64(inventory_count(&g->item->inventory, ITEM_ID_COAL), 7u);
    us_game_stop(t, g);
}

/* ---- The built program ------------------------------------------------------------------------------------------------------ */

TEST(the_program_opens_and_closes_the_inventory_by_script_and_shows_it) {
    REQUIRE(t->arg_count >= 1);
    char dir[64] = "/tmp/mc_ui_screens_XXXXXX";
    REQUIRE(mkdtemp(dir) != nullptr);
    char script[128], open_png[128], closed_png[128];
    (void)snprintf(script, sizeof(script), "%s/script", dir);
    (void)snprintf(open_png, sizeof(open_png), "%s/open.png", dir);
    (void)snprintf(closed_png, sizeof(closed_png), "%s/closed.png", dir);
    static const char text[] = "1 screenshot before.png\n"
                               "2 key e down\n"
                               "3 key e up\n"
                               "5 screenshot open.png\n"
                               "6 key escape down\n"
                               "7 key escape up\n"
                               "9 screenshot closed.png\n"
                               "10 end\n";
    REQUIRE(file_write_atomic(script, text, sizeof(text) - 1u) == FILE_OK);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--test-world",
                          (char *)"--walk",
                          (char *)"--render-distance",
                          (char *)"3",
                          (char *)"--script",
                          script,
                          (char *)"--screenshot-dir",
                          dir,
                          (char *)"--width",
                          (char *)"320",
                          (char *)"--height",
                          (char *)"240",
                          nullptr};
    test_process_t run;
    test_process_run(t, argv, US_PROGRAM_TIMEOUT_MS, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, "screens came up 1 times");
    const uint64_t capacity = display_png_bytes_needed(320u, 240u);
    uint8_t *const open_bytes = test_alloc(t, capacity);
    uint8_t *const closed_bytes = test_alloc(t, capacity);
    uint64_t open_length = 0u, closed_length = 0u;
    REQUIRE(file_read_whole(open_png, open_bytes, capacity, &open_length) == FILE_OK);
    REQUIRE(file_read_whole(closed_png, closed_bytes, capacity, &closed_length) == FILE_OK);
    CHECK_EQ_U64(open_length, closed_length);
    CHECK(memcmp(open_bytes, closed_bytes, open_length) != 0);
    char before_png[128];
    (void)snprintf(before_png, sizeof(before_png), "%s/before.png", dir);
    (void)unlink(script);
    (void)unlink(open_png);
    (void)unlink(closed_png);
    (void)unlink(before_png);
    (void)rmdir(dir);
    CHECK_EQ_I64(access(dir, F_OK), -1);
}

TEST_MAIN()
