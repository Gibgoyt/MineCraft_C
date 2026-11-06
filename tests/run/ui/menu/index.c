/* ctest run_ui_menu. See README.md. */
#include "harness/index.h"

/* Both fake kernels. Their helpers share names, so the one for input is renamed coming in. */
#include "run/display/mode/fixtures/fake_card.h"
#define fake_close_fd evdev_close_fd
#define fake_ioctl_call evdev_ioctl_call
#define fake_read_fd evdev_read_fd
#define fake_record evdev_record
#define fake_ops evdev_ops
#include "run/input/fixtures/fake_evdev.h"
#undef fake_close_fd
#undef fake_ioctl_call
#undef fake_read_fd
#undef fake_record
#undef fake_ops
#include "run/input/fixtures/replay.h"

#include "climate/index.h"
#include "display/headless/png/index.h"
#include "entity/index.h"
#include "file/index.h"
#include "game/index.h"
#include "game/session/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "item/index.h"
#include "job/index.h"
#include "physics/index.h"
#include "player/index.h"
#include "render/index.h"
#include "simd/reference/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    SHEET_WIDTH = 640u, /* scale 2 */
    SHEET_HEIGHT = 480u,
    PATH_BYTES = 256u,
};

/* The layout at 640 by 480, worked out by hand from the numbers at the top of run/src/ui/menu/index.c: the middle of each thing. */
enum : int32_t {
    AT_MIDDLE_X = 320,
    AT_COLUMN_0_Y = 200, /* 3/8 of the height is 180, and a button is 40 tall */
    AT_COLUMN_1_Y = 248,
    AT_COLUMN_2_Y = 296,
    AT_ROW_X = 300,
    AT_ROW_0_Y = 78, /* the list starts at 48, 8 in, and a row is 44 tall */
    AT_ROW_STEP = 48,
    AT_PLAY_X = 188,
    AT_CREATE_NEW_X = 452,
    AT_BUTTONS_Y = 396,
    AT_BACK_Y = 444,
    AT_NAME_Y = 122,
    AT_SEED_Y = 202,
    AT_CREATE_X = 218,
    AT_CANCEL_X = 422,
    AT_CREATE_Y = 280,
};

/* ---- A ui on memory of its own, and input frames made here ------------------------------------------------------------ */

typedef struct menu_rig_t {
    texture_t *texture; /* 0 */
    ui_t *ui;           /* 8 */
    char *saves;        /* 16 */
    char *directory;    /* 24: what holds saves, and is removed at the end */
    uint8_t *picture;   /* 32: SHEET_WIDTH by SHEET_HEIGHT, for the pause screen to go over */
    int32_t pointer_x;  /* 40 */
    int32_t pointer_y;  /* 44 */
} menu_rig_t;
static_assert(sizeof(menu_rig_t) == 48);

static void *aligned_alloc_64(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static char *temp_directory(test_context_t *t) {
    char *const directory = test_alloc(t, PATH_BYTES);
    snprintf(directory, PATH_BYTES, "/tmp/mc_ui_menu_XXXXXX");
    return mkdtemp(directory);
}

static void temp_directory_remove(test_context_t *t, char *directory) {
    if (directory == nullptr || strncmp(directory, "/tmp/mc_ui_menu_", 16u) != 0) {
        return;
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", directory, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static char *path_join(test_context_t *t, const char *left, const char *right) {
    char *const path = test_alloc(t, PATH_BYTES);
    snprintf(path, PATH_BYTES, "%s/%s", left, right);
    return path;
}

static uint32_t rig_action(menu_rig_t *rig, const input_frame_t *frame, bool escape) {
    ui_t *const ui = rig->ui;
    ui_widget_input_feed(&ui->widget, frame);
    if (!ui_frame_begin_blank(ui, SHEET_WIDTH, SHEET_HEIGHT)) {
        return 255u;
    }
    const ui_menu_action_t action = ui_menu_frame(&ui->menu, escape);
    (void)ui_frame_end(ui, true);
    return action;
}

static bool rig_begin(test_context_t *t, menu_rig_t *rig) {
    memset(rig, 0, sizeof(*rig));
    rig->texture = aligned_alloc_64(t, sizeof(texture_t));
    rig->ui = aligned_alloc_64(t, sizeof(ui_t));
    rig->directory = temp_directory(t);
    if (!CHECK(rig->directory != nullptr) || !CHECK(texture_init(rig->texture))) {
        return false;
    }
    if (!CHECK(ui_init(rig->ui, rig->texture))) {
        texture_deinit(rig->texture);
        return false;
    }
    rig->saves = path_join(t, rig->directory, "saves");
    ui_menu_saves_set(&rig->ui->menu, rig->saves, 777u);
    ui_menu_open(&rig->ui->menu, UI_MENU_SCREEN_MAIN);
    /* A first frame, so that the widgets know the canvas and the pointer is in its middle. */
    const input_frame_t nothing = {0};
    (void)rig_action(rig, &nothing, false);
    rig->pointer_x = (int32_t)(SHEET_WIDTH / 2u);
    rig->pointer_y = (int32_t)(SHEET_HEIGHT / 2u);
    return true;
}

static void rig_end(test_context_t *t, menu_rig_t *rig) {
    ui_deinit(rig->ui);
    texture_deinit(rig->texture);
    temp_directory_remove(t, rig->directory);
}

static void keys_set(input_keys_t *keys, uint32_t code) {
    keys->words[code / 64u] |= (uint64_t)1 << (code % 64u);
}

/* Press and release both, each in a frame of its own, with the pointer moved there in a frame before them. What the last frame
 * returned is the click's. */
static uint32_t rig_click(menu_rig_t *rig, int32_t x, int32_t y) {
    input_frame_t frame = {0};
    frame.mouse_dx = x - rig->pointer_x;
    frame.mouse_dy = y - rig->pointer_y;
    (void)rig_action(rig, &frame, false);
    rig->pointer_x = x;
    rig->pointer_y = y;
    memset(&frame, 0, sizeof(frame));
    keys_set(&frame.pressed, BTN_LEFT);
    keys_set(&frame.down, BTN_LEFT);
    (void)rig_action(rig, &frame, false);
    memset(&frame, 0, sizeof(frame));
    keys_set(&frame.released, BTN_LEFT);
    return rig_action(rig, &frame, false);
}

static uint32_t rig_key(menu_rig_t *rig, uint32_t code) {
    input_frame_t frame = {0};
    keys_set(&frame.pressed, code);
    keys_set(&frame.down, code);
    const uint32_t action = rig_action(rig, &frame, false);
    memset(&frame, 0, sizeof(frame));
    keys_set(&frame.released, code);
    (void)rig_action(rig, &frame, false);
    return action;
}

static uint32_t key_of(char c) {
    static const uint16_t letters[26] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
                                         KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
                                         KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
    static const uint16_t digits[10] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
    if (c >= 'a' && c <= 'z') {
        return letters[c - 'a'];
    }
    if (c >= '0' && c <= '9') {
        return digits[c - '0'];
    }
    return c == '-' ? KEY_MINUS : KEY_SPACE;
}

static void rig_type(menu_rig_t *rig, const char *text) {
    for (const char *c = text; *c != '\0'; c++) {
        (void)rig_key(rig, key_of(*c));
    }
}

static uint32_t rig_escape(menu_rig_t *rig) {
    const input_frame_t nothing = {0};
    return rig_action(rig, &nothing, true);
}

static uint32_t rig_wheel(menu_rig_t *rig, int64_t detents) {
    input_frame_t frame = {0};
    frame.wheel = detents;
    return rig_action(rig, &frame, false);
}

static uint64_t canvas_hash(const ui_t *ui) {
    const uint32_t *const pixels = (const uint32_t *)(const void *)ui->canvas.borrowed_pixels;
    uint64_t hash = 14695981039346656037ull;
    for (uint64_t i = 0u; i < (uint64_t)ui->canvas.width * ui->canvas.height; i++) {
        for (uint32_t b = 0u; b < 3u; b++) {
            hash ^= (pixels[i] >> (b * 8u)) & 0xFFu;
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

/* If UI_MENU_PNG_DIR names a directory, each screen goes there, to look at. */
static void canvas_write_png(test_context_t *t, const ui_t *ui, const char *name) {
    const char *const directory = getenv("UI_MENU_PNG_DIR");
    if (directory == nullptr) {
        return;
    }
    const uint64_t capacity = display_png_bytes_needed(ui->canvas.width, ui->canvas.height);
    uint8_t *const png = test_alloc(t, capacity);
    uint64_t bytes = 0u;
    REQUIRE(display_png_encode(ui->canvas.borrowed_pixels, (uint32_t)ui->canvas.pitch, ui->canvas.width,
                               ui->canvas.height, png, capacity, &bytes));
    REQUIRE(file_write_atomic(path_join(t, directory, name), png, bytes) == FILE_OK);
}

/* ---- What was typed, and what it turns into ---------------------------------------------------------------------------- */

TEST(a_seed_is_a_number_or_a_hash_or_what_blank_means) {
    CHECK_EQ_U64(ui_menu_catalog_seed_from_text("", 7u), 7u);
    CHECK_EQ_U64(ui_menu_catalog_seed_from_text("   ", 7u), 7u);
    CHECK_EQ_U64(ui_menu_catalog_seed_from_text("0", 7u), 0u);
    CHECK_EQ_U64(ui_menu_catalog_seed_from_text("12345", 7u), 12345u);
    CHECK_EQ_U64(ui_menu_catalog_seed_from_text("  12345 ", 7u), 12345u);
    CHECK_EQ_U64(ui_menu_catalog_seed_from_text("18446744073709551615", 7u), UINT64_MAX);
    CHECK_EQ_U64(ui_menu_catalog_seed_from_text("-1", 7u), UINT64_MAX);
    CHECK_EQ_U64(ui_menu_catalog_seed_from_text("-9223372036854775808", 7u), (uint64_t)1 << 63);
    /* FNV-1a's own published vectors: of "a", and of "foobar". */
    CHECK_EQ_U64(ui_menu_catalog_seed_from_text("a", 7u), 0xAF63DC4C8601EC8Cull);
    CHECK_EQ_U64(ui_menu_catalog_seed_from_text("foobar", 7u), 0x85944171F73967E8ull);
    CHECK_EQ_U64(ui_menu_catalog_seed_from_text(" foobar  ", 7u), 0x85944171F73967E8ull);
    /* One past what fits is not that number wrapped round, and a lone minus is not 0. */
    CHECK(ui_menu_catalog_seed_from_text("18446744073709551616", 7u) != 0u);
    CHECK(ui_menu_catalog_seed_from_text("-9223372036854775809", 7u) != (((uint64_t)1 << 63) - 1u));
    CHECK(ui_menu_catalog_seed_from_text("-", 7u) != 0u);
    CHECK(ui_menu_catalog_seed_from_text("-", 7u) != 7u);
    CHECK(ui_menu_catalog_seed_from_text("12a", 7u) != 12u);
}

TEST(a_name_loses_its_spaces_and_a_directory_cannot_leave_saves) {
    char name[UI_MENU_NAME_BYTES];
    CHECK(ui_menu_catalog_name_clean("  My World  ", name));
    CHECK_STR_EQ(name, "My World");
    CHECK(!ui_menu_catalog_name_clean("    ", name));
    CHECK_STR_EQ(name, "");
    CHECK(!ui_menu_catalog_name_clean("", name));
    CHECK(ui_menu_catalog_name_clean("0123456789012345678901234567890123456789", name));
    CHECK_STR_EQ(name, "0123456789012345678901234567890");

    char directory[UI_MENU_DIRECTORY_BYTES];
    ui_menu_catalog_directory_name("My World-2_b", directory);
    CHECK_STR_EQ(directory, "My_World-2_b");
    ui_menu_catalog_directory_name("../../etc/passwd", directory);
    CHECK_STR_EQ(directory, "______etc_passwd");
    ui_menu_catalog_directory_name(".hidden", directory);
    CHECK_STR_EQ(directory, "_hidden");
    ui_menu_catalog_directory_name("", directory);
    CHECK_STR_EQ(directory, "world");
}

TEST(a_world_meta_is_exactly_what_is_written_or_it_is_nothing) {
    static const char good[] = "minecraft_c world 1\nseed 42\nname My World\n";
    const uint64_t length = sizeof(good) - 1u;
    ui_menu_world_t world;
    memset(&world, 0, sizeof(world));
    REQUIRE(ui_menu_catalog_meta_parse(good, length, &world));
    CHECK_EQ_U64(world.seed, 42u);
    CHECK_STR_EQ(world.name, "My World");
    /* Cut anywhere short, it does not parse. */
    for (uint64_t cut = 0u; cut < length; cut++) {
        CHECK(!ui_menu_catalog_meta_parse(good, cut, &world));
    }
    static const char *const bad[] = {
        "",
        "\n\n\n",
        "minecraft_c world 2\nseed 42\nname My World\n",
        "minecraft_c world 1\nseed\nname My World\n",
        "minecraft_c world 1\nseed \nname My World\n",
        "minecraft_c world 1\nseed -1\nname My World\n",
        "minecraft_c world 1\nseed 4x\nname My World\n",
        "minecraft_c world 1\nseed 18446744073709551616\nname My World\n",
        "minecraft_c world 1\nseed 42\nname \n",
        "minecraft_c world 1\nseed 42\nname My\tWorld\n",
        "minecraft_c world 1\nseed 42\nname 01234567890123456789012345678901\n",
        "minecraft_c world 1\nseed 42\nname My World\nmore\n",
        "minecraft_c world 1\nname My World\nseed 42\n",
    };
    for (uint64_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(!ui_menu_catalog_meta_parse(bad[i], strlen(bad[i]), &world));
    }
    /* The largest of each. */
    static const char largest[] =
        "minecraft_c world 1\nseed 18446744073709551615\nname 0123456789012345678901234567890\n";
    REQUIRE(ui_menu_catalog_meta_parse(largest, sizeof(largest) - 1u, &world));
    CHECK_EQ_U64(world.seed, UINT64_MAX);
    CHECK_STR_EQ(world.name, "0123456789012345678901234567890");
}

/* ---- Worlds on disk ---------------------------------------------------------------------------------------------------- */

static void write_text(test_context_t *t, const char *directory, const char *name, const char *text) {
    REQUIRE(file_make_dirs(directory) == FILE_OK);
    REQUIRE(file_write_atomic(path_join(t, directory, name), text, strlen(text)) == FILE_OK);
}

TEST(worlds_are_made_and_found_again_in_order) {
    char *const root = temp_directory(t);
    REQUIRE(root != nullptr);
    const char *const saves = path_join(t, root, "saves/deeper");
    ui_menu_world_t worlds[8];
    uint32_t count = 99u;
    uint32_t skipped = 99u;

    /* Nothing there yet is no worlds, and no error. */
    CHECK_EQ_U64(ui_menu_catalog_read(saves, worlds, 8u, &count, &skipped), FILE_OK);
    CHECK_EQ_U64(count, 0u);
    CHECK_EQ_U64(skipped, 0u);

    ui_menu_world_t made;
    CHECK_EQ_U64(ui_menu_catalog_create(saves, "  zebra ", 1u, &made), FILE_OK);
    CHECK_STR_EQ(made.name, "zebra");
    CHECK_STR_EQ(made.directory, "zebra");
    CHECK_EQ_U64(made.seed, 1u);
    CHECK_EQ_U64(ui_menu_catalog_create(saves, "Apple", UINT64_MAX, &made), FILE_OK);
    CHECK_EQ_U64(ui_menu_catalog_create(saves, "mango", 3u, &made), FILE_OK);
    /* Taken: the same name twice is two worlds, in two directories. */
    CHECK_EQ_U64(ui_menu_catalog_create(saves, "mango", 4u, &made), FILE_OK);
    CHECK_STR_EQ(made.directory, "mango-2");
    CHECK_EQ_U64(ui_menu_catalog_create(saves, "mango", 5u, &made), FILE_OK);
    CHECK_STR_EQ(made.directory, "mango-3");
    /* A name made of nothing a directory may hold still lands inside saves. */
    CHECK_EQ_U64(ui_menu_catalog_create(saves, "../..", 6u, &made), FILE_OK);
    CHECK_STR_EQ(made.directory, "_____");
    uint64_t size = 0u;
    CHECK_EQ_U64(file_path_size(path_join(t, path_join(t, saves, "_____"), "world.meta"), &size), FILE_OK);
    /* And a name with nothing in it is refused, making nothing. */
    CHECK(ui_menu_catalog_create(saves, "   ", 7u, &made) != FILE_OK);

    /* What is written is what the three lines say. */
    char meta[UI_MENU_META_BYTES];
    uint64_t bytes = 0u;
    REQUIRE(file_read_whole(path_join(t, path_join(t, saves, "Apple"), "world.meta"), meta, sizeof(meta), &bytes) ==
            FILE_OK);
    static const char apple[] = "minecraft_c world 1\nseed 18446744073709551615\nname Apple\n";
    REQUIRE(bytes == sizeof(apple) - 1u);
    CHECK_MEM_EQ(meta, apple, bytes);

    /* Not worlds, which are no one's business: a loose file, a directory without a meta, a hidden one. */
    write_text(t, saves, "loose.txt", "x");
    REQUIRE(file_make_dirs(path_join(t, saves, "just_a_directory")) == FILE_OK);
    write_text(t, path_join(t, saves, ".hidden"), "world.meta", "minecraft_c world 1\nseed 1\nname Hidden\n");
    /* Worlds that went wrong, which are: one is wrong within, and one's name is too long to keep. */
    write_text(t, path_join(t, saves, "broken"), "world.meta", "not one of ours");
    write_text(t, path_join(t, saves, "0123456789012345678901234567890123456789012345678"), "world.meta",
               "minecraft_c world 1\nseed 1\nname Long\n");

    CHECK_EQ_U64(ui_menu_catalog_read(saves, worlds, 8u, &count, &skipped), FILE_OK);
    REQUIRE(count == 6u);
    CHECK_EQ_U64(skipped, 2u);
    /* By name, whatever its case, and then by directory. */
    CHECK_STR_EQ(worlds[0].name, "../..");
    CHECK_STR_EQ(worlds[1].name, "Apple");
    CHECK_EQ_U64(worlds[1].seed, UINT64_MAX);
    CHECK_STR_EQ(worlds[1].directory, "Apple");
    CHECK_STR_EQ(worlds[2].directory, "mango");
    CHECK_EQ_U64(worlds[2].seed, 3u);
    CHECK_STR_EQ(worlds[3].directory, "mango-2");
    CHECK_EQ_U64(worlds[3].seed, 4u);
    CHECK_STR_EQ(worlds[4].directory, "mango-3");
    CHECK_STR_EQ(worlds[5].name, "zebra");

    /* More than there is room for are counted, not written past. */
    worlds[2].seed = 0xABCDu;
    CHECK_EQ_U64(ui_menu_catalog_read(saves, worlds, 2u, &count, &skipped), FILE_OK);
    CHECK_EQ_U64(count, 2u);
    CHECK_EQ_U64(skipped, 6u);
    CHECK_EQ_U64(worlds[2].seed, 0xABCDu);

    /* A file where saves should be is a failure, for both. */
    const char *const file_there = path_join(t, saves, "loose.txt");
    CHECK(ui_menu_catalog_read(file_there, worlds, 8u, &count, &skipped) != FILE_OK);
    CHECK_EQ_U64(count, 0u);
    CHECK(ui_menu_catalog_create(file_there, "x", 1u, &made) != FILE_OK);
    temp_directory_remove(t, root);
}

/* ---- The screens ------------------------------------------------------------------------------------------------------- */

TEST(the_main_screen_goes_where_its_buttons_say) {
    menu_rig_t rig;
    REQUIRE(rig_begin(t, &rig));
    ui_menu_t *const menu = &rig.ui->menu;
    CHECK_EQ_U64(menu->screen, UI_MENU_SCREEN_MAIN);
    CHECK_EQ_U64(rig_escape(&rig), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->screen, UI_MENU_SCREEN_MAIN);
    /* Settings does nothing until there is a settings screen, and then asks for it. */
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, AT_COLUMN_1_Y), UI_MENU_ACTION_NONE);
    ui_menu_settings_enable(menu);
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, AT_COLUMN_1_Y), UI_MENU_ACTION_SETTINGS);
    /* Between the buttons there is nothing. */
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, 224), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, AT_COLUMN_2_Y), UI_MENU_ACTION_QUIT_GAME);
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, AT_COLUMN_0_Y), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->screen, UI_MENU_SCREEN_SELECT);
    /* The loading screen is only something to look at. */
    ui_menu_open(menu, UI_MENU_SCREEN_LOADING);
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, AT_COLUMN_0_Y), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(rig_escape(&rig), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->screen, UI_MENU_SCREEN_LOADING);
    rig_end(t, &rig);
}

TEST(an_empty_list_can_only_make_a_world_or_go_back) {
    menu_rig_t rig;
    REQUIRE(rig_begin(t, &rig));
    ui_menu_t *const menu = &rig.ui->menu;
    ui_menu_open(menu, UI_MENU_SCREEN_SELECT);
    CHECK_EQ_U64(menu->world_count, 0u);
    CHECK_STR_EQ(menu->message, "");
    /* Play Selected is disabled, and Enter has nothing to play either. */
    CHECK_EQ_U64(rig_click(&rig, AT_PLAY_X, AT_BUTTONS_Y), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(rig_key(&rig, KEY_ENTER), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->screen, UI_MENU_SCREEN_SELECT);
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, AT_BACK_Y), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->screen, UI_MENU_SCREEN_MAIN);
    ui_menu_open(menu, UI_MENU_SCREEN_SELECT);
    CHECK_EQ_U64(rig_escape(&rig), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->screen, UI_MENU_SCREEN_MAIN);
    ui_menu_open(menu, UI_MENU_SCREEN_SELECT);
    CHECK_EQ_U64(rig_click(&rig, AT_CREATE_NEW_X, AT_BUTTONS_Y), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->screen, UI_MENU_SCREEN_CREATE);
    rig_end(t, &rig);
}

static uint64_t worlds_on_disk(const menu_rig_t *rig) {
    ui_menu_world_t worlds[4];
    uint32_t count = 0u;
    uint32_t skipped = 0u;
    if (ui_menu_catalog_read(rig->saves, worlds, 4u, &count, &skipped) != FILE_OK) {
        return UINT64_MAX;
    }
    return count;
}

TEST(a_world_is_made_from_what_is_typed) {
    menu_rig_t rig;
    REQUIRE(rig_begin(t, &rig));
    ui_menu_t *const menu = &rig.ui->menu;

    /* Typing goes to the name from the start, and the seed takes it after a click. */
    ui_menu_open(menu, UI_MENU_SCREEN_CREATE);
    rig_type(&rig, "first world");
    CHECK_STR_EQ(menu->name_text, "first world");
    CHECK_STR_EQ(menu->seed_text, "");
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, AT_SEED_Y), UI_MENU_ACTION_NONE);
    rig_type(&rig, "-12");
    CHECK_STR_EQ(menu->seed_text, "-12");
    CHECK_STR_EQ(menu->name_text, "first world");
    CHECK_EQ_U64(rig_key(&rig, KEY_BACKSPACE), UI_MENU_ACTION_NONE);
    CHECK_STR_EQ(menu->seed_text, "-1");
    CHECK_EQ_U64(worlds_on_disk(&rig), 0u);
    CHECK_EQ_U64(rig_click(&rig, AT_CREATE_X, AT_CREATE_Y), UI_MENU_ACTION_PLAY);
    CHECK_STR_EQ(menu->chosen.name, "first world");
    CHECK_STR_EQ(menu->chosen.directory, "first_world");
    CHECK_EQ_U64(menu->chosen.seed, UINT64_MAX);
    CHECK_EQ_U64(worlds_on_disk(&rig), 1u);

    /* Nothing is left in the fields from the last one. A blank seed is what the session said, and Enter does as Create does. */
    ui_menu_open(menu, UI_MENU_SCREEN_CREATE);
    CHECK_STR_EQ(menu->name_text, "");
    rig_type(&rig, "second");
    CHECK_EQ_U64(rig_key(&rig, KEY_ENTER), UI_MENU_ACTION_PLAY);
    CHECK_EQ_U64(menu->chosen.seed, 777u);
    CHECK_EQ_U64(worlds_on_disk(&rig), 2u);

    /* A word for a seed. */
    ui_menu_open(menu, UI_MENU_SCREEN_CREATE);
    rig_type(&rig, "third");
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, AT_SEED_Y), UI_MENU_ACTION_NONE);
    rig_type(&rig, "foobar");
    CHECK_EQ_U64(rig_key(&rig, KEY_ENTER), UI_MENU_ACTION_PLAY);
    CHECK_EQ_U64(menu->chosen.seed, 0x85944171F73967E8ull);

    /* The name's field keeps what fits. */
    ui_menu_open(menu, UI_MENU_SCREEN_CREATE);
    rig_type(&rig, "abcdefghijabcdefghijabcdefghijabcdefghij");
    CHECK_STR_EQ(menu->name_text, "abcdefghijabcdefghijabcdefghija");

    /* No name, no world: Create does nothing, Enter leaves the typing in the name, and the name's click works too. */
    ui_menu_open(menu, UI_MENU_SCREEN_CREATE);
    rig_type(&rig, "   ");
    const uint64_t worlds_before = worlds_on_disk(&rig);
    CHECK_EQ_U64(rig_click(&rig, AT_CREATE_X, AT_CREATE_Y), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, AT_NAME_Y), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(rig_key(&rig, KEY_ENTER), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->screen, UI_MENU_SCREEN_CREATE);
    rig_type(&rig, "x");
    CHECK_STR_EQ(menu->name_text, "   x");
    CHECK_EQ_U64(worlds_on_disk(&rig), worlds_before);

    /* Cancel and Esc make nothing, and go back to the list, which has the new ones. */
    CHECK_EQ_U64(rig_click(&rig, AT_CANCEL_X, AT_CREATE_Y), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->screen, UI_MENU_SCREEN_SELECT);
    CHECK_EQ_U64(menu->world_count, worlds_before);
    ui_menu_open(menu, UI_MENU_SCREEN_CREATE);
    rig_type(&rig, "not made");
    CHECK_EQ_U64(rig_escape(&rig), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->screen, UI_MENU_SCREEN_SELECT);
    CHECK_EQ_U64(worlds_on_disk(&rig), worlds_before);
    rig_end(t, &rig);
}

TEST(what_goes_wrong_on_disk_is_said_on_the_screen) {
    menu_rig_t rig;
    REQUIRE(rig_begin(t, &rig));
    ui_menu_t *const menu = &rig.ui->menu;
    /* A file where the saves directory should be. */
    write_text(t, rig.directory, "saves", "in the way");
    ui_menu_open(menu, UI_MENU_SCREEN_SELECT);
    CHECK_EQ_U64(menu->world_count, 0u);
    CHECK_CONTAINS(menu->message, "Could not read the worlds");
    CHECK_EQ_U64(rig_escape(&rig), UI_MENU_ACTION_NONE);
    CHECK_STR_EQ(menu->message, "");

    ui_menu_open(menu, UI_MENU_SCREEN_CREATE);
    rig_type(&rig, "doomed");
    CHECK_EQ_U64(rig_key(&rig, KEY_ENTER), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->screen, UI_MENU_SCREEN_CREATE);
    CHECK_CONTAINS(menu->message, "Could not make the world");
    CHECK_STR_EQ(menu->name_text, "doomed");
    REQUIRE(file_remove(rig.saves) == FILE_OK);

    /* A broken world is counted, and the others still listed. */
    ui_menu_world_t made;
    REQUIRE(ui_menu_catalog_create(rig.saves, "good", 1u, &made) == FILE_OK);
    write_text(t, path_join(t, rig.saves, "bad"), "world.meta", "broken");
    ui_menu_open(menu, UI_MENU_SCREEN_SELECT);
    CHECK_EQ_U64(menu->world_count, 1u);
    CHECK_STR_EQ(menu->message, "1 worlds could not be listed");
    rig_end(t, &rig);
}

TEST(a_long_list_scrolls_and_a_row_means_the_world_on_it) {
    menu_rig_t rig;
    REQUIRE(rig_begin(t, &rig));
    ui_menu_t *const menu = &rig.ui->menu;
    for (uint32_t i = 0u; i < 20u; i++) {
        char name[16];
        snprintf(name, sizeof(name), "w%02" PRIu32, i);
        ui_menu_world_t made;
        REQUIRE(ui_menu_catalog_create(rig.saves, name, 1000u + i, &made) == FILE_OK);
    }
    ui_menu_open(menu, UI_MENU_SCREEN_SELECT);
    REQUIRE(menu->world_count == 20u);
    CHECK_EQ_I64(menu->selected, -1);

    /* Six rows fit. The wheel stops at both ends. */
    CHECK_EQ_U64(rig_wheel(&rig, 3), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->first_row, 0u);
    CHECK_EQ_U64(rig_wheel(&rig, -5), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->first_row, 5u);
    CHECK_EQ_U64(rig_wheel(&rig, -1000), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->first_row, 14u);
    CHECK_EQ_U64(rig_wheel(&rig, 2), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->first_row, 12u);

    /* The third row now is the fifteenth world. */
    CHECK_EQ_U64(rig_click(&rig, AT_ROW_X, AT_ROW_0_Y + 2 * AT_ROW_STEP), UI_MENU_ACTION_NONE);
    CHECK_EQ_I64(menu->selected, 14);
    /* It stays chosen when it scrolls away. */
    CHECK_EQ_U64(rig_wheel(&rig, 1000), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->first_row, 0u);
    CHECK_EQ_I64(menu->selected, 14);
    CHECK_EQ_U64(rig_click(&rig, AT_PLAY_X, AT_BUTTONS_Y), UI_MENU_ACTION_PLAY);
    CHECK_STR_EQ(menu->chosen.name, "w14");
    CHECK_EQ_U64(menu->chosen.seed, 1014u);
    CHECK_EQ_U64(rig_click(&rig, AT_ROW_X, AT_ROW_0_Y), UI_MENU_ACTION_NONE);
    CHECK_EQ_I64(menu->selected, 0);
    CHECK_EQ_U64(rig_key(&rig, KEY_ENTER), UI_MENU_ACTION_PLAY);
    CHECK_STR_EQ(menu->chosen.name, "w00");
    /* Looking again starts again. */
    ui_menu_open(menu, UI_MENU_SCREEN_SELECT);
    CHECK_EQ_I64(menu->selected, -1);
    rig_end(t, &rig);
}

TEST(more_worlds_than_fit_are_counted_and_nothing_breaks) {
    menu_rig_t rig;
    REQUIRE(rig_begin(t, &rig));
    ui_menu_t *const menu = &rig.ui->menu;
    for (uint32_t i = 0u; i < UI_MENU_WORLDS_MAX + 3u; i++) {
        char name[16];
        snprintf(name, sizeof(name), "w%" PRIu32, i);
        ui_menu_world_t made;
        REQUIRE(ui_menu_catalog_create(rig.saves, name, i, &made) == FILE_OK);
    }
    ui_menu_open(menu, UI_MENU_SCREEN_SELECT);
    CHECK_EQ_U64(menu->world_count, UI_MENU_WORLDS_MAX);
    CHECK_EQ_U64(menu->worlds_skipped, 3u);
    CHECK_STR_EQ(menu->message, "3 worlds could not be listed");
    CHECK_EQ_U64(rig_wheel(&rig, -100000), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(menu->first_row, UI_MENU_WORLDS_MAX - 6u);
    /* The last row's id is its place in the list: the sixth row is the last world. */
    CHECK_EQ_U64(rig_click(&rig, AT_ROW_X, AT_ROW_0_Y + 5 * AT_ROW_STEP), UI_MENU_ACTION_NONE);
    CHECK_EQ_I64(menu->selected, (int64_t)UI_MENU_WORLDS_MAX - 1);
    rig_end(t, &rig);
}

/* A plain picture to go behind the pause screen. */
static void picture_fill(uint8_t *picture, uint32_t color) {
    uint32_t *const pixels = (uint32_t *)(void *)picture;
    for (uint64_t i = 0u; i < (uint64_t)SHEET_WIDTH * SHEET_HEIGHT; i++) {
        pixels[i] = color;
    }
}

static uint32_t rig_pause_action(menu_rig_t *rig, const input_frame_t *frame, bool escape, const uint8_t *picture) {
    ui_t *const ui = rig->ui;
    ui_widget_input_feed(&ui->widget, frame);
    if (!ui_frame_begin_over(ui, picture, SHEET_WIDTH, SHEET_HEIGHT)) {
        return 255u;
    }
    const ui_menu_action_t action = ui_menu_frame(&ui->menu, escape);
    (void)ui_frame_end(ui, false);
    return action;
}

TEST(the_pause_screen_goes_over_the_world_and_leaves_it_alone) {
    menu_rig_t rig;
    REQUIRE(rig_begin(t, &rig));
    ui_menu_t *const menu = &rig.ui->menu;
    ui_menu_open(menu, UI_MENU_SCREEN_PAUSE);
    uint8_t *const picture = test_alloc(t, (uint64_t)SHEET_WIDTH * SHEET_HEIGHT * 4u);
    picture_fill(picture, 0xFF80C040u);
    const input_frame_t nothing = {0};
    CHECK_EQ_U64(rig_pause_action(&rig, &nothing, false, picture), UI_MENU_ACTION_NONE);
    /* A corner, away from everything: the world, shaded once, by the reference blend. */
    uint32_t expected = 0xFF80C040u;
    const uint32_t shade = 0x90000000u;
    simd_reference_blend_over(&expected, &shade, 1u);
    const uint32_t *const canvas = (const uint32_t *)(const void *)rig.ui->canvas.borrowed_pixels;
    CHECK_EQ_U64(canvas[0] & 0xFFFFFFu, expected & 0xFFFFFFu);
    /* The picture itself is as it was, so it can go behind the next frame too, and the shade is not blended twice. */
    CHECK_EQ_U64(((const uint32_t *)(const void *)picture)[0], 0xFF80C040u);
    CHECK_EQ_U64(rig_pause_action(&rig, &nothing, false, picture), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(canvas[0] & 0xFFFFFFu, expected & 0xFFFFFFu);

    CHECK_EQ_U64(rig_pause_action(&rig, &nothing, true, picture), UI_MENU_ACTION_RESUME);
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, AT_COLUMN_0_Y), UI_MENU_ACTION_RESUME);
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, AT_COLUMN_1_Y), UI_MENU_ACTION_NONE);
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, AT_COLUMN_2_Y), UI_MENU_ACTION_QUIT_TO_TITLE);
    ui_menu_settings_enable(menu);
    CHECK_EQ_U64(rig_click(&rig, AT_MIDDLE_X, AT_COLUMN_1_Y), UI_MENU_ACTION_SETTINGS);
    rig_end(t, &rig);
}

TEST(no_heap_and_no_table_memory_in_200_frames_of_every_screen) {
    menu_rig_t rig;
    REQUIRE(rig_begin(t, &rig));
    ui_menu_t *const menu = &rig.ui->menu;
    ui_menu_world_t made;
    REQUIRE(ui_menu_catalog_create(rig.saves, "one", 1u, &made) == FILE_OK);
    const input_frame_t nothing = {0};
    static const ui_menu_screen_t screens[] = {UI_MENU_SCREEN_MAIN, UI_MENU_SCREEN_SELECT, UI_MENU_SCREEN_CREATE,
                                               UI_MENU_SCREEN_PAUSE, UI_MENU_SCREEN_LOADING};
    for (uint32_t s = 0u; s < 5u; s++) {
        ui_menu_open(menu, screens[s]);
        (void)rig_action(&rig, &nothing, false);
        heap_counters_t before;
        heap_counters_read(&before);
        const uint64_t tables_before = rig.ui->arena_tables.used;
        for (uint32_t frame = 0u; frame < 200u; frame++) {
            CHECK_EQ_U64(rig_action(&rig, &nothing, false), UI_MENU_ACTION_NONE);
        }
        heap_counters_t after;
        heap_counters_read(&after);
        CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
        CHECK_EQ_U64(after.release_calls, before.release_calls);
        CHECK_EQ_U64(rig.ui->arena_tables.used, tables_before);
    }
    rig_end(t, &rig);
}

/* Set from pictures that were looked at; see README.md. A change here is a change a person would see. */
static const uint64_t HASH_MAIN = 0xe700da279c885173ull;
static const uint64_t HASH_SELECT = 0x6113131b3fecd245ull;
static const uint64_t HASH_SELECT_EMPTY = 0x7e99c9e8bf68fad4ull;
static const uint64_t HASH_CREATE = 0xf5a88ca9fc5f5c9eull;
static const uint64_t HASH_PAUSE = 0x8bd42dacec6f522dull;
static const uint64_t HASH_LOADING = 0xe29ef157ec16ef85ull;

static void sheet_check(test_context_t *t, menu_rig_t *rig, const char *name, uint64_t expected) {
    canvas_write_png(t, rig->ui, name);
    const uint64_t hash = canvas_hash(rig->ui);
    printf("%s: 0x%016" PRIx64 "\n", name, hash);
    CHECK_EQ_U64(hash, expected);
}

TEST(every_screen_is_the_picture_that_was_looked_at) {
    menu_rig_t rig;
    REQUIRE(rig_begin(t, &rig));
    ui_menu_t *const menu = &rig.ui->menu;
    const input_frame_t nothing = {0};
    CHECK_EQ_U64(rig_action(&rig, &nothing, false), UI_MENU_ACTION_NONE);
    sheet_check(t, &rig, "main.png", HASH_MAIN);

    ui_menu_open(menu, UI_MENU_SCREEN_SELECT);
    (void)rig_action(&rig, &nothing, false);
    sheet_check(t, &rig, "select_empty.png", HASH_SELECT_EMPTY);

    for (uint32_t i = 0u; i < 9u; i++) {
        char name[32];
        snprintf(name, sizeof(name), "World number %" PRIu32, i);
        ui_menu_world_t made;
        REQUIRE(ui_menu_catalog_create(rig.saves, name, i * 1000000007ull, &made) == FILE_OK);
    }
    ui_menu_world_t longest;
    REQUIRE(ui_menu_catalog_create(rig.saves, "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", UINT64_MAX / 2u, &longest) == FILE_OK);
    ui_menu_open(menu, UI_MENU_SCREEN_SELECT);
    (void)rig_click(&rig, AT_ROW_X, AT_ROW_0_Y + AT_ROW_STEP);
    (void)rig_wheel(&rig, -1);
    (void)rig_action(&rig, &nothing, false);
    sheet_check(t, &rig, "select.png", HASH_SELECT);

    ui_menu_open(menu, UI_MENU_SCREEN_CREATE);
    rig_type(&rig, "my new world");
    (void)rig_action(&rig, &nothing, false);
    sheet_check(t, &rig, "create.png", HASH_CREATE);

    ui_menu_open(menu, UI_MENU_SCREEN_PAUSE);
    uint8_t *const picture = test_alloc(t, (uint64_t)SHEET_WIDTH * SHEET_HEIGHT * 4u);
    picture_fill(picture, 0xFF6090D0u);
    CHECK_EQ_U64(rig_pause_action(&rig, &nothing, false, picture), UI_MENU_ACTION_NONE);
    sheet_check(t, &rig, "pause.png", HASH_PAUSE);

    menu->chosen = longest;
    ui_menu_open(menu, UI_MENU_SCREEN_LOADING);
    (void)rig_action(&rig, &nothing, false);
    sheet_check(t, &rig, "loading.png", HASH_LOADING);
    rig_end(t, &rig);
}

/* Everything must stay on the canvas at the smallest size it is made for, and at the sizes people have. */
TEST(the_screens_stay_on_the_canvas_at_every_size) {
    menu_rig_t rig;
    REQUIRE(rig_begin(t, &rig));
    ui_menu_t *const menu = &rig.ui->menu;
    ui_menu_world_t made;
    REQUIRE(ui_menu_catalog_create(rig.saves, "one", 1u, &made) == FILE_OK);
    static const uint32_t sizes[][2] = {{320u, 240u},   {640u, 360u},   {1280u, 720u},
                                        {1920u, 1080u}, {2560u, 1080u}, {3840u, 2160u}};
    static const ui_menu_screen_t screens[] = {UI_MENU_SCREEN_MAIN, UI_MENU_SCREEN_SELECT, UI_MENU_SCREEN_CREATE,
                                               UI_MENU_SCREEN_PAUSE, UI_MENU_SCREEN_LOADING};
    const input_frame_t nothing = {0};
    for (uint32_t size = 0u; size < sizeof(sizes) / sizeof(sizes[0]); size++) {
        const uint32_t width = sizes[size][0];
        const uint32_t height = sizes[size][1];
        for (uint32_t s = 0u; s < 5u; s++) {
            ui_menu_open(menu, screens[s]);
            ui_widget_input_feed(&rig.ui->widget, &nothing);
            REQUIRE(ui_frame_begin_blank(rig.ui, width, height));
            CHECK_EQ_U64(ui_menu_frame(menu, false), UI_MENU_ACTION_NONE);
            (void)ui_frame_end(rig.ui, true);
            /* Every pixel was painted but on the pause screen, which has nothing under it here. */
            if (screens[s] != UI_MENU_SCREEN_PAUSE) {
                const uint32_t *const pixels = (const uint32_t *)(const void *)rig.ui->canvas.borrowed_pixels;
                uint64_t black = 0u;
                for (uint64_t i = 0u; i < (uint64_t)width * height; i++) {
                    black += (pixels[i] & 0xFFFFFFu) == 0u ? 1u : 0u;
                }
                CHECK(black < (uint64_t)width * height / 8u);
            }
            char name[48];
            snprintf(name, sizeof(name), "size_%" PRIu32 "x%" PRIu32 "_%" PRIu32 ".png", width, height, s);
            canvas_write_png(t, rig.ui, name);
        }
    }
    rig_end(t, &rig);
}

/* ---- The built program ------------------------------------------------------------------------------------------------- */

static uint8_t *file_slurp(test_context_t *t, const char *path, uint64_t *out_bytes) {
    const uint64_t capacity = 16ull << 20;
    uint8_t *const bytes = test_alloc(t, capacity);
    *out_bytes = 0u;
    return file_read_whole(path, bytes, capacity, out_bytes) == FILE_OK ? bytes : nullptr;
}

static uint64_t count_of(const char *text, const char *needle) {
    uint64_t count = 0u;
    for (const char *at = strstr(text, needle); at != nullptr; at = strstr(at + 1, needle)) {
        count++;
    }
    return count;
}

TEST(the_program_goes_all_the_way_round_with_a_script) {
    REQUIRE(t->arg_count >= 1);
    char *const root = temp_directory(t);
    REQUIRE(root != nullptr);
    char *const saves = path_join(t, root, "saves");
    char *const shots = path_join(t, root, "shots");
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--saves",
                          saves,
                          (char *)"--script",
                          (char *)"tests/run/ui/menu/fixtures/flow.script",
                          (char *)"--screenshot-dir",
                          shots,
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          (char *)"--render-distance",
                          (char *)"4",
                          nullptr};
    test_process_t process;
    test_process_run(t, argv, 240000u, &process);
    REQUIRE(process.started != 0u);
    CHECK(process.timed_out == 0u);
    if (!CHECK_EQ_I64(process.exit_code, 0)) {
        printf("%s\n", process.output);
    }
    /* Made once, opened twice, from the same place and with the same seed. */
    CHECK_EQ_U64(count_of(process.output, "session: world 'flow' is open, from flow, with seed 123"), 2u);
    CHECK_EQ_U64(count_of(process.output, "session: back on the title screen"), 2u);
    CHECK_EQ_U64(count_of(process.output, "session: quit"), 1u);
    CHECK_CONTAINS(process.output, " 0 bytes live");

    uint64_t bytes = 0u;
    const uint8_t *const meta = file_slurp(t, path_join(t, path_join(t, saves, "flow"), "world.meta"), &bytes);
    REQUIRE(meta != nullptr);
    static const char expected_meta[] = "minecraft_c world 1\nseed 123\nname flow\n";
    REQUIRE(bytes == sizeof(expected_meta) - 1u);
    CHECK_MEM_EQ(meta, expected_meta, bytes);

    /* The same seed, opened again: the same world, the same place. Since Stage 16.2 a world opened again goes on from where it was left, ticks later than
     * the first picture by a pause and a few steps, so it is the same picture but for the light having moved on a little. A new place, or a new look,
     * would change a large part of it. */
    uint64_t a_bytes = 0u;
    uint64_t b_bytes = 0u;
    const uint8_t *const world_a = file_slurp(t, path_join(t, shots, "world_a.png"), &a_bytes);
    const uint8_t *const world_b = file_slurp(t, path_join(t, shots, "world_b.png"), &b_bytes);
    REQUIRE(world_a != nullptr && world_b != nullptr && a_bytes == b_bytes);
    uint64_t far_off = 0u;
    for (uint64_t i = 0u; i < a_bytes; i++) {
        far_off += world_a[i] > world_b[i] + 16 || world_b[i] > world_a[i] + 16 ? 1u : 0u;
    }
    printf("  a world and that world opened again: %" PRIu64 " of %" PRIu64 " bytes far off\n", far_off, a_bytes);
    CHECK(far_off < a_bytes / 1000u);
    /* A screen is not a world, and the two title screens are one picture but for nothing. */
    uint64_t main_bytes = 0u;
    uint64_t title_bytes = 0u;
    const uint8_t *const main_png = file_slurp(t, path_join(t, shots, "main.png"), &main_bytes);
    const uint8_t *const title_png = file_slurp(t, path_join(t, shots, "title_again.png"), &title_bytes);
    REQUIRE(main_png != nullptr && title_png != nullptr && main_bytes == a_bytes && title_bytes == a_bytes);
    CHECK(memcmp(main_png, world_a, a_bytes) != 0);
    uint64_t paused_bytes = 0u;
    const uint8_t *const paused = file_slurp(t, path_join(t, shots, "paused.png"), &paused_bytes);
    REQUIRE(paused != nullptr && paused_bytes == a_bytes);
    CHECK(memcmp(paused, world_a, a_bytes) != 0);

    /* If UI_MENU_PNG_DIR names a directory, the program's pictures stay to be looked at. */
    const char *const keep = getenv("UI_MENU_PNG_DIR");
    if (keep != nullptr) {
        static const char *const names[] = {"main.png",    "select_empty.png", "create.png",      "world_a.png",
                                            "paused.png",  "resumed.png",      "title_again.png", "select_chosen.png",
                                            "world_b.png", "after_quit.png"};
        for (uint32_t i = 0u; i < sizeof(names) / sizeof(names[0]); i++) {
            uint64_t size = 0u;
            const uint8_t *const png = file_slurp(t, path_join(t, shots, names[i]), &size);
            REQUIRE(png != nullptr);
            char kept[64];
            snprintf(kept, sizeof(kept), "program_%s", names[i]);
            REQUIRE(file_write_atomic(path_join(t, keep, kept), png, size) == FILE_OK);
        }
    }
    temp_directory_remove(t, root);
}

TEST(another_seed_is_another_world) {
    REQUIRE(t->arg_count >= 1);
    char *const root = temp_directory(t);
    REQUIRE(root != nullptr);
    char *const saves = path_join(t, root, "saves");
    char *const shots = path_join(t, root, "shots");
    /* A world with the first seed's name in a second run with a second seed: only the seed in world.meta is different. */
    ui_menu_world_t made;
    REQUIRE(ui_menu_catalog_create(saves, "flow", 123u, &made) == FILE_OK);
    ui_menu_world_t other;
    REQUIRE(ui_menu_catalog_create(saves, "other", 987654321u, &other) == FILE_OK);
    char *const script = path_join(t, root, "two.script");
    static const char text[] = "1 move 0 -40\n"
                               "+1 button left down\n+1 button left up\n"
                               "+1 move -20 -122\n"
                               "+1 button left down\n+1 button left up\n"
                               "+1 move -112 318\n"
                               "+1 button left down\n+1 button left up\n"
                               "+4 screenshot first.png\n"
                               "+1 key escape down\n+1 key escape up\n"
                               "+1 move 132 -100\n"
                               "+1 button left down\n+1 button left up\n"
                               "+1 move 0 -96\n"
                               "+1 button left down\n+1 button left up\n"
                               "+1 move -20 -74\n" /* 300, 126: the second row */
                               "+1 button left down\n+1 button left up\n"
                               "+1 move -112 270\n"
                               "+1 button left down\n+1 button left up\n"
                               "+4 screenshot second.png\n"
                               "+1 end\n";
    REQUIRE(file_write_atomic(script, text, sizeof(text) - 1u) == FILE_OK);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--saves",
                          saves,
                          (char *)"--script",
                          script,
                          (char *)"--screenshot-dir",
                          shots,
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          (char *)"--render-distance",
                          (char *)"4",
                          nullptr};
    test_process_t process;
    test_process_run(t, argv, 240000u, &process);
    REQUIRE(process.started != 0u);
    if (!CHECK_EQ_I64(process.exit_code, 0)) {
        printf("%s\n", process.output);
    }
    CHECK_CONTAINS(process.output, "session: world 'flow' is open, from flow, with seed 123");
    CHECK_CONTAINS(process.output, "session: world 'other' is open, from other, with seed 987654321");
    uint64_t a_bytes = 0u;
    uint64_t b_bytes = 0u;
    const uint8_t *const first = file_slurp(t, path_join(t, shots, "first.png"), &a_bytes);
    const uint8_t *const second = file_slurp(t, path_join(t, shots, "second.png"), &b_bytes);
    REQUIRE(first != nullptr && second != nullptr && a_bytes == b_bytes);
    CHECK(memcmp(first, second, a_bytes) != 0);
    CHECK_CONTAINS(process.output, " 0 bytes live");
    temp_directory_remove(t, root);
}

TEST(the_program_refuses_what_makes_no_sense_and_a_display_that_is_not_its_own) {
    REQUIRE(t->arg_count >= 1);
    char *const game = t->borrowed_args[0];
    test_process_t process;
    char *const saves_alone[] = {game, (char *)"--saves", (char *)"/nowhere", nullptr};
    test_process_run(t, saves_alone, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 2);
    CHECK_CONTAINS(process.output, "--saves only means something with --menu");
    char *const with_world[] = {game, (char *)"--menu", (char *)"--test-world", nullptr};
    test_process_run(t, with_world, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 2);
    CHECK_CONTAINS(process.output, "--menu does not go with");
    char *const with_report[] = {game, (char *)"--menu", (char *)"--report", nullptr};
    test_process_run(t, with_report, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 2);
    /* Without --headless, with stdin from /dev/null and whatever session this is run in: refused before the card is opened. */
    char *const kms[] = {game, (char *)"--menu", (char *)"--saves", (char *)"/nowhere", nullptr};
    test_process_run(t, kms, 120000u, &process);
    CHECK_EQ_I64(process.exit_code, 6);
    CHECK_CONTAINS(process.output, "refused:");
    CHECK_CONTAINS(process.output, "The card was not opened");
    CHECK_CONTAINS(process.output, " 0 bytes live");
    /* And with no flags and no terminal it is the check of the runtime, and no display at all. */
    char *const no_flags[] = {game, nullptr};
    test_process_run(t, no_flags, 120000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
    CHECK_CONTAINS(process.output, "core runtime");
    CHECK(strstr(process.output, "refused") == nullptr);
}

/* ---- All of it on fake kernels, with the real threads and a real guardian ------------------------------------------------ */

/* The real screens at 320 by 200, scale 1. By hand again. */
enum : int32_t {
    SMALL_MIDDLE_X = 160,
    SMALL_MIDDLE_Y = 100,
    SMALL_SINGLEPLAYER_Y = 85, /* 3/8 of the height is 75, and a button is 20 tall */
    SMALL_QUIT_Y = 133,
    SMALL_RESUME_Y = 85,
    SMALL_TITLE_Y = 133,
    SMALL_CREATE_NEW_X = 226, /* the list is 260 wide from 30: half of it is 128 */
    SMALL_PLAY_X = 94,
    SMALL_BUTTONS_Y = 158, /* 200 - 56 + 4 is 148 */
    SMALL_ROW_X = 150,
    SMALL_ROW_0_Y = 39, /* the list starts at 24, 4 in, and a row is 22 tall */
};

typedef enum step_kind_t : uint32_t {
    STEP_WAIT,         /* a number of frames */
    STEP_MOVE,         /* the pointer to x, y */
    STEP_CLICK,        /* the left button down and, in a later frame, up */
    STEP_KEY,          /* code down and, in a later frame, up */
    STEP_NOTE_MENU,    /* what back holds is a menu: lit, and not a world */
    STEP_WAIT_PICTURE, /* until a world's picture has been shown */
    STEP_WAIT_TICKS,   /* until value more ticks have gone by */
    STEP_FROZEN,       /* value milliseconds with no tick going by, and the frame not the picture */
} step_kind_t;

typedef struct step_t {
    int32_t x;        /* 0 */
    int32_t y;        /* 4 */
    uint32_t value;   /* 8 */
    step_kind_t kind; /* 12 */
} step_t;
static_assert(sizeof(step_t) == 16);

#define WAIT(frames) {.kind = STEP_WAIT, .value = (frames)}
#define MOVE(at_x, at_y) {.kind = STEP_MOVE, .x = (at_x), .y = (at_y)}
#define CLICK {.kind = STEP_CLICK}
#define KEY(code) {.kind = STEP_KEY, .value = (code)}

static const step_t round_trip[] = {
    WAIT(3),
    {.kind = STEP_NOTE_MENU},
    MOVE(SMALL_MIDDLE_X, SMALL_SINGLEPLAYER_Y),
    CLICK,
    WAIT(2),
    MOVE(SMALL_CREATE_NEW_X, SMALL_BUTTONS_Y),
    CLICK,
    WAIT(2),
    KEY(KEY_A),
    WAIT(2),
    KEY(KEY_ENTER), /* the loading screen, and then a frame that opens the world */
    {.kind = STEP_WAIT_PICTURE},
    {.kind = STEP_WAIT_TICKS, .value = 2u},
    KEY(KEY_ESC),
    WAIT(5),
    {.kind = STEP_FROZEN, .value = 300u},
    MOVE(SMALL_MIDDLE_X, SMALL_RESUME_Y),
    CLICK,
    {.kind = STEP_WAIT_TICKS, .value = 3u},
    KEY(KEY_ESC),
    WAIT(3),
    KEY(KEY_ESC), /* and Esc resumes too */
    {.kind = STEP_WAIT_TICKS, .value = 2u},
    KEY(KEY_ESC),
    WAIT(3),
    MOVE(SMALL_MIDDLE_X, SMALL_TITLE_Y),
    CLICK, /* the world goes, in that frame, and the next is the title screen */
    WAIT(3),
    {.kind = STEP_NOTE_MENU},
    MOVE(SMALL_MIDDLE_X, SMALL_SINGLEPLAYER_Y),
    CLICK,
    WAIT(2),
    MOVE(SMALL_ROW_X, SMALL_ROW_0_Y),
    CLICK,
    WAIT(2),
    MOVE(SMALL_PLAY_X, SMALL_BUTTONS_Y),
    CLICK, /* the same world again */
    {.kind = STEP_WAIT_PICTURE},
    {.kind = STEP_WAIT_TICKS, .value = 2u},
    KEY(KEY_ESC),
    WAIT(3),
    MOVE(SMALL_MIDDLE_X, SMALL_TITLE_Y),
    CLICK,
    WAIT(3),
    MOVE(SMALL_MIDDLE_X, SMALL_QUIT_Y),
    CLICK, /* and that is the end */
};

typedef struct round_run_t {
    game_session_t *borrowed_session;  /* 0 */
    fake_device_t *borrowed_mouse;     /* 8 */
    fake_device_t *borrowed_keyboard;  /* 16 */
    const display_t *borrowed_display; /* 24 */
    int64_t started_ns;                /* 32 */
    int64_t step_started_ns;           /* 40 */
    uint64_t ticks_at_step;            /* 48 */
    uint64_t frames;                   /* 56 */
    uint64_t frozen_ticks_moved;       /* 64 */
    uint64_t frames_of_frozen;         /* 72 */
    uint64_t menu_notes_lit;           /* 80 */
    uint64_t menu_notes;               /* 88 */
    uint64_t pauses_shown_over_world;  /* 96 */
    uint32_t step;                     /* 104 */
    uint32_t wait;                     /* 108 */
    int32_t pointer_x;                 /* 112 */
    int32_t pointer_y;                 /* 116 */
    uint32_t phase;                    /* 120: of a click or a key: 0 not begun, 1 down */
    uint32_t guardians_seen;           /* 124 */
    uint8_t timed_out;                 /* 128 */
    uint8_t step_begun;                /* 129 */
    uint8_t guardian_missing;          /* 130 */
    uint8_t pet_during_open;           /* 131: the watchdog heard from it, in the frame that opened the first world */
    uint8_t climate_percent_max;       /* 132: the most the loading screen's climate line said, after a world opened */
    uint8_t _pad0[3];                  /* 133 */
    int64_t open_ns;                   /* 136: how long that frame took */
    uint64_t climate_frames; /* 144: loading screens drawn after a world opened, as its climate was worked out */
} round_run_t;
static_assert(sizeof(round_run_t) == 152);

static void round_send(fake_device_t *device, uint16_t type, uint16_t code, int32_t value) {
    const struct input_event events[] = {REPLAY_EVENT(type, code, value), REPLAY_SYN};
    fake_replay(device, events, REPLAY_COUNT(events));
}

/* Says whether this step is over, doing a little more of it each frame. */
static bool round_step(round_run_t *run, const step_t *step, const display_buffer_t *back) {
    game_t *const game = run->borrowed_session->borrowed_game;
    switch (step->kind) {
    case STEP_WAIT:
        if (run->step_begun == 0u) {
            run->wait = step->value;
        }
        return run->wait-- == 0u;
    case STEP_MOVE:
        round_send(run->borrowed_mouse, EV_REL, REL_X, step->x - run->pointer_x);
        round_send(run->borrowed_mouse, EV_REL, REL_Y, step->y - run->pointer_y);
        run->pointer_x = step->x;
        run->pointer_y = step->y;
        return true;
    case STEP_CLICK:
    case STEP_KEY: {
        fake_device_t *const device = step->kind == STEP_CLICK ? run->borrowed_mouse : run->borrowed_keyboard;
        const uint16_t code = step->kind == STEP_CLICK ? (uint16_t)BTN_LEFT : (uint16_t)step->value;
        round_send(device, EV_KEY, code, run->phase == 0u ? 1 : 0);
        run->phase = run->phase == 0u ? 1u : 0u;
        return run->phase == 0u;
    }
    case STEP_NOTE_MENU: {
        run->menu_notes++;
        const uint32_t *const pixels = (const uint32_t *)(const void *)back->owned_map;
        run->menu_notes_lit += (pixels[0] & 0xFFFFFFu) != 0u ? 1u : 0u;
        return true;
    }
    case STEP_WAIT_PICTURE:
        return game->open != 0u && game->pictures_shown > 0u;
    case STEP_WAIT_TICKS:
        if (run->step_begun == 0u) {
            run->ticks_at_step = game->tick_count;
        }
        return game->tick_count >= run->ticks_at_step + step->value;
    case STEP_FROZEN: {
        if (run->step_begun == 0u) {
            run->ticks_at_step = game->tick_count;
            run->step_started_ns = time_now_ns();
            run->pauses_shown_over_world +=
                game->borrowed_last_picture != nullptr &&
                        memcmp(back->owned_map, game->borrowed_last_picture, back->pitch) != 0
                    ? 1u
                    : 0u;
        }
        run->frames_of_frozen++;
        const bool over = time_now_ns() - run->step_started_ns > (int64_t)step->value * 1000000ll;
        if (over) {
            run->frozen_ticks_moved = game->tick_count - run->ticks_at_step;
        }
        return over;
    }
    }
    return true;
}

static bool round_source(void *context, display_buffer_t *back) {
    round_run_t *const run = context;
    run->frames++;
    run->guardians_seen += run->borrowed_display->guard.guardian_pid != 0 ? 1u : 0u;
    if (run->borrowed_display->guard.guardian_pid == 0) {
        run->guardian_missing = 1u;
    }
    const game_t *const game = run->borrowed_session->borrowed_game;
    const bool was_open = game->open != 0u;
    const int64_t before_ns = time_now_ns();
    if (!game_session_source(run->borrowed_session, back)) {
        return false;
    }
    /* Plan 004 phase 03: the world is open and its climate still being worked out, so that frame was the loading screen. */
    if (was_open && game->open != 0u && game->threads_running == 0u &&
        run->borrowed_session->state == GAME_SESSION_PLAYING) {
        run->climate_frames++;
        const uint8_t percent = run->borrowed_session->borrowed_ui->menu.loading_percent;
        run->climate_percent_max = percent > run->climate_percent_max ? percent : run->climate_percent_max;
    }
    if (!was_open && game->open != 0u && run->open_ns == 0) {
        run->open_ns = time_now_ns() - before_ns;
        run->pet_during_open = run->borrowed_display->guard.last_pet_ns > before_ns ? 1u : 0u;
    }
    if (time_now_ns() - run->started_ns > 200ll * TIME_NS_PER_SECOND) {
        run->timed_out = 1u;
        return false;
    }
    if (run->step < REPLAY_COUNT(round_trip)) {
        const bool over = round_step(run, &round_trip[run->step], back);
        run->step_begun = (uint8_t)(over ? 0u : 1u);
        if (over) {
            run->step++;
        }
    }
    time_sleep_ns(1000000);
    return true;
}

TEST(the_whole_round_runs_through_the_display_loop_on_the_real_threads) {
    /* The fixtures' other helpers, which only their own tests need. */
    (void)evdev_record;
    (void)fake_caps_touchpad;
    (void)fake_caps_power_button;
    (void)fake_find_call;
    gpu_bridge_t *const bridge = aligned_alloc_64(t, sizeof(gpu_bridge_t));
    job_system_t *const job = aligned_alloc_64(t, sizeof(job_system_t));
    world_t *const world = aligned_alloc_64(t, sizeof(world_t));
    terrain_t *const terrain = aligned_alloc_64(t, sizeof(terrain_t));
    render_t *const render = aligned_alloc_64(t, sizeof(render_t));
    texture_t *const texture = aligned_alloc_64(t, sizeof(texture_t));
    game_t *const game = aligned_alloc_64(t, sizeof(game_t));
    physics_t *const physics = aligned_alloc_64(t, sizeof(physics_t));
    player_t *const player = aligned_alloc_64(t, sizeof(player_t));
    entity_t *const entity = aligned_alloc_64(t, sizeof(entity_t));
    item_t *const item = aligned_alloc_64(t, sizeof(item_t));
    ui_t *const ui = aligned_alloc_64(t, sizeof(ui_t));
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    REQUIRE(gpu_bridge_init(bridge, gpu_bridge_limits_default()) == GPU_STATUS_OK);
    REQUIRE(thread_topology_read(topology));
    REQUIRE(job_init(job, topology));
    REQUIRE(texture_init(texture));
    REQUIRE(world_init(world, world_limits_default()));
    REQUIRE(terrain_init(terrain, TERRAIN_SEED_DEFAULT));
    REQUIRE(physics_init(physics));
    player_init(player, physics);
    REQUIRE(entity_init(entity, physics));
    REQUIRE(item_init(item));
    REQUIRE(render_init(render, &bridge->device_memory, job, render_limits_default()));
    REQUIRE(ui_init(ui, texture));
    game_init(game, world, terrain, render, job, texture, topology);
    game_player_set(game, player);
    game_entity_set(game, entity);
    game_item_set(game, item);
    /* Each world has its climate cells, as the program's --menu does: worked out over the loading screen's frames. */
    climate_t *const climate = aligned_alloc_64(t, sizeof(climate_t));
    REQUIRE(climate_init(climate));
    game_climate_set(game, climate);

    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    display_t *const display = aligned_alloc_64(t, sizeof(display_t));
    const display_config_t config = fake_config();
    REQUIRE(display_init(display, &config, &fake_ops, card) == DISPLAY_STATUS_OK);
    const display_buffer_t *const buffer = &display->flip.buffers[0];
    REQUIRE(buffer->width == 320u && buffer->height == 200u);
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const mouse = fake_add(fake, "event0", "Mouse", fake_caps_mouse, true);
    fake_device_t *const keyboard = fake_add(fake, "event1", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = aligned_alloc_64(t, sizeof(input_t));
    REQUIRE(input_init(input, FAKE_DIRECTORY, &evdev_ops, fake));
    char *const root = temp_directory(t);
    REQUIRE(root != nullptr);
    const game_config_t small = {.render_distance = 4u,
                                 .width = buffer->width,
                                 .height = buffer->height,
                                 .features_off = SHADE_FEATURE_DAY_CYCLE | SHADE_FEATURE_AO | SHADE_FEATURE_TAA,
                                 .show_date = 0u};
    game_session_t session;
    game_session_init(&session, game, ui, terrain, input, &small, path_join(t, root, "saves"), 4242u);
    round_run_t run = {.borrowed_session = &session,
                       .borrowed_mouse = mouse,
                       .borrowed_keyboard = keyboard,
                       .borrowed_display = display,
                       .started_ns = time_now_ns(),
                       .pointer_x = SMALL_MIDDLE_X,
                       .pointer_y = SMALL_MIDDLE_Y};
    const display_status_t status = game_session_run(&session, display, round_source, &run);
    printf("frames %" PRIu64 ", step %" PRIu32 " of %" PRIu32 ", %" PRIu64 " flips, a world opened in %" PRId64 " ms\n",
           run.frames, run.step, REPLAY_COUNT(round_trip), (uint64_t)card->flips, run.open_ns / (int64_t)1000000);
    printf("climate: %" PRIu64 " loading frames after the worlds opened, the line at most %" PRIu32 "%%\n",
           run.climate_frames, (uint32_t)run.climate_percent_max);
    /* The threads started only once the climate round the start was built, with the loading screen up meanwhile. */
    CHECK(run.climate_frames >= 1u);
    CHECK(!climate_is_open(climate));
    /* This seed's dry land is some way off, so that frame was long enough for the watchdog to want hearing from: half a second is
     * twice the pace at which the guard writes to it. */
    CHECK(run.open_ns > 0);
    CHECK(run.pet_during_open != 0u || run.open_ns < 500000000ll);
    CHECK(game->open_alive == nullptr);

    CHECK_EQ_U64(status, DISPLAY_STATUS_OK);
    CHECK_EQ_U64(run.timed_out, 0u);
    CHECK_EQ_U64(session.failed, 0u);
    CHECK_EQ_U64(session.state, GAME_SESSION_DONE);
    CHECK_EQ_U64(run.step, REPLAY_COUNT(round_trip));
    CHECK_EQ_U64(session.worlds_opened, 2u);
    CHECK_EQ_U64(session.pauses, 4u);
    CHECK_EQ_U64(run.guardian_missing, 0u);
    CHECK_EQ_U64(run.menu_notes, 2u);
    CHECK_EQ_U64(run.menu_notes_lit, 2u);
    CHECK_EQ_U64(run.pauses_shown_over_world, 1u);
    /* No time passed in the world through 300 ms of pause, and many frames were shown of it. */
    CHECK_EQ_U64(run.frozen_ticks_moved, 0u);
    CHECK(run.frames_of_frozen > 10u);
    /* Both worlds were really the chosen seed, and everything has gone. */
    CHECK_EQ_U64(terrain->seed, 4242u);
    CHECK_EQ_U64(game->open, 0u);
    CHECK_EQ_U64(game->threads_running, 0u);
    CHECK_EQ_I64(display->guard.guardian_pid, 0);
    CHECK_EQ_I64(card->kd_mode, KD_TEXT);
    CHECK(card->flips > 100u);

    game_session_deinit(&session);
    input_deinit(input);
    display_deinit(display);
    game_deinit(game);
    climate_deinit(climate);
    ui_deinit(ui);
    render_deinit(render);
    item_deinit(item);
    entity_deinit(entity);
    player_deinit(player);
    physics_deinit(physics);
    CHECK(gpu_bridge_exit_check(bridge));
    terrain_deinit(terrain);
    world_deinit(world);
    texture_deinit(texture);
    job_deinit(job);
    gpu_bridge_deinit(bridge);
    check_nothing_left(t, card);
    temp_directory_remove(t, root);
}

TEST_MAIN()
