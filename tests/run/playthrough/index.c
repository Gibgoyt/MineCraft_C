/* ctest run_playthrough. See README.md. */
#include "harness/index.h"

#include "file/index.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    PT_PATH_BYTES = 512u,
    PT_DAY_MS = 240000u,
    PT_NIGHT_MS = 800000u,
};
enum : uint64_t {
    PT_FILE_BYTES = 1ull << 20,
};

static char *pt_temp(test_context_t *t) {
    char *const directory = test_alloc(t, PT_PATH_BYTES);
    snprintf(directory, PT_PATH_BYTES, "/tmp/mc_playthrough_XXXXXX");
    return mkdtemp(directory);
}

static void pt_temp_remove(test_context_t *t, char *directory) {
    if (directory == nullptr || strncmp(directory, "/tmp/mc_playthrough_", 20u) != 0) {
        return;
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", directory, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static char *pt_join(test_context_t *t, const char *left, const char *right) {
    char *const path = test_alloc(t, PT_PATH_BYTES);
    snprintf(path, PT_PATH_BYTES, "%s/%s", left, right);
    return path;
}

static uint64_t pt_count(const char *text, const char *needle) {
    uint64_t count = 0u;
    for (const char *at = strstr(text, needle); at != nullptr; at = strstr(at + 1, needle)) {
        count++;
    }
    return count;
}

/* The whole number that comes just before needle, in its first place, as the report's lines say them. UINT64_MAX with no such place. */
static uint64_t pt_number_before(const char *text, const char *needle) {
    const char *const at = strstr(text, needle);
    if (at == nullptr || at == text || at[-1] != ' ') {
        return UINT64_MAX;
    }
    const char *start = at - 1;
    while (start > text && start[-1] >= '0' && start[-1] <= '9') {
        start--;
    }
    return start == at - 1 ? UINT64_MAX : strtoull(start, nullptr, 10);
}

/* titles is how many times the script comes back to the title screen, the last on the way to Quit Game. */
static void pt_run(test_context_t *t, const char *script, const char *root, uint32_t timeout_ms, uint64_t titles,
                   test_process_t *process) {
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--saves",
                          pt_join(t, root, "saves"),
                          (char *)"--settings",
                          pt_join(t, root, "settings.yaml"),
                          (char *)"--script",
                          (char *)script,
                          (char *)"--screenshot-dir",
                          pt_join(t, root, "shots"),
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          (char *)"--render-distance",
                          (char *)"8",
                          nullptr};
    test_process_run(t, argv, timeout_ms, process);
    REQUIRE(process->started != 0u);
    CHECK(process->timed_out == 0u);
    if (!CHECK_EQ_I64(process->exit_code, 0)) {
        printf("%s\n", process->output);
    }
    CHECK_CONTAINS(process->output, " 0 bytes live");
    /* Saved on the way to the title, opened from what was saved, and out by Quit Game. */
    CHECK_EQ_U64(pt_count(process->output, "session: back on the title screen"), titles);
    CHECK_EQ_U64(pt_count(process->output, "session: quit"), 1u);
    CHECK_CONTAINS(process->output, "game: the player goes on from");
    CHECK_CONTAINS(process->output, " was read");
    CHECK_EQ_U64(pt_count(process->output, " failed;"), pt_count(process->output, " 0 failed;"));
}

TEST(a_first_day_mines_dirt_picks_it_up_crafts_changes_the_distance_and_comes_back) {
    REQUIRE(t->arg_count >= 1);
    char *const root = pt_temp(t);
    REQUIRE(root != nullptr);
    test_process_t process;
    pt_run(t, "tests/run/playthrough/fixtures/day.script", root, PT_DAY_MS, 2u, &process);
    const char *const output = process.output;

    /* A mangrove's trunk, on seed 36: the one line a person would look for. */
    CHECK_CONTAINS(output, "the last mined was mangrove_log");
    /* Mud, a log left over and the pickaxe, as the bench took what the planks and sticks were, once on the way out and once again from the save. */
    CHECK_EQ_U64(pt_count(output, "the inventory holds 3 items in 3 stacks"), 2u);
    CHECK_EQ_U64(pt_count(output, "hotbar slot 4 is in hand, with 1 of wood_pickaxe"), 2u);
    /* A table and a bench, the bench two blocks for one placing. */
    CHECK_EQ_U64(pt_count(output, "the player mined 5 blocks and placed 2"), 2u);
    CHECK_CONTAINS(output, "render distance 2");
    CHECK_CONTAINS(output, "render distance 8");

    uint8_t *const settings = test_alloc(t, PT_FILE_BYTES);
    uint64_t bytes = 0u;
    REQUIRE(file_read_whole(pt_join(t, root, "settings.yaml"), settings, PT_FILE_BYTES - 1u, &bytes) == FILE_OK);
    settings[bytes] = 0u;
    CHECK_CONTAINS((const char *)settings, "render_distance: 8");

    /* What a person sees of it is the picture, so each is at least there. A person reads them with MC_PLAYTHROUGH_KEEP. */
    static const char *const shots[] = {"spawn.png",      "picked_up.png",        "logs.png",       "crafted.png",
                                        "table.png",      "table_no_pickaxe.png", "bench_made.png", "bench_placed.png",
                                        "bench_open.png", "pickaxe.png",          "settings.png",   "at_2.png",
                                        "at_8.png",       "back_on_title.png",    "reloaded.png"};
    for (uint64_t i = 0u; i < sizeof(shots) / sizeof(shots[0]); i++) {
        uint64_t size = 0u;
        CHECK_EQ_U64(file_path_size(pt_join(t, pt_join(t, root, "shots"), shots[i]), &size), FILE_OK);
        CHECK(size > 1000u);
    }
    if (getenv("MC_PLAYTHROUGH_KEEP") != nullptr) {
        printf("  kept in %s\n", root);
    } else {
        pt_temp_remove(t, root);
    }
}

/* Ahmed's words: "creating stairs with wood on a crafting table is not possible", and then "all wood working is done on this bench". Seed 36's
 * tree is a mangrove, whose planks had no stairs. So the pattern makes nothing on the table, a bench is made there, and makes four on it. */
TEST(mangrove_planks_make_stairs_on_a_bench_that_is_placed_broken_and_all_of_it_saved) {
    REQUIRE(t->arg_count >= 1);
    char *const root = pt_temp(t);
    REQUIRE(root != nullptr);
    test_process_t process;
    pt_run(t, "tests/run/playthrough/fixtures/stairs.script", root, PT_DAY_MS, 2u, &process);
    const char *const output = process.output;

    /* Four made and in slot 4, two put down and one back: three in hand, once on the way out and once again from the save. */
    CHECK_EQ_U64(pt_count(output, "hotbar slot 4 is in hand, with 3 of mangrove_stairs"), 2u);
    /* Five for the planks, a sixth for the stairs and a seventh for the bench; the table, the bench and two stairs placed. */
    CHECK_EQ_U64(pt_count(output, "the player mined 7 blocks and placed 4"), 2u);
    /* Mining one half is one block in the books, and the half is the one aimed at. */
    CHECK_EQ_U64(pt_count(output, "the last mined was woodworking_bench_east"), 2u);
    /* Mud, a plank, three stairs, two sticks, and the one bench that came down and was picked up. */
    CHECK_EQ_U64(pt_count(output, "the inventory holds 8 items in 5 stacks"), 2u);

    static const char *const shots[] = {"table_no_stairs.png",    "bench_made.png",     "bench_placed.png",
                                        "bench_open.png",         "stairs_ready.png",   "stairs_in_slot.png",
                                        "stairs_placed.png",      "stairs_mined.png",   "bench_broken.png",
                                        "bench_in_inventory.png", "stairs_reloaded.png"};
    for (uint64_t i = 0u; i < sizeof(shots) / sizeof(shots[0]); i++) {
        uint64_t size = 0u;
        CHECK_EQ_U64(file_path_size(pt_join(t, pt_join(t, root, "shots"), shots[i]), &size), FILE_OK);
        CHECK(size > 1000u);
    }
    if (getenv("MC_PLAYTHROUGH_KEEP") != nullptr) {
        printf("  kept in %s\n", root);
    } else {
        pt_temp_remove(t, root);
    }
}

/* What the stairs do not reach: a bench closed with planks on its grid and on the pointer, a bench that stands through a save, and its west half. */
TEST(a_bench_gives_its_grid_back_stands_through_a_save_and_goes_by_its_west_half) {
    REQUIRE(t->arg_count >= 1);
    char *const root = pt_temp(t);
    REQUIRE(root != nullptr);
    test_process_t process;
    pt_run(t, "tests/run/playthrough/fixtures/bench.script", root, PT_DAY_MS, 3u, &process);
    const char *const output = process.output;

    /* Saved with it standing. Mud, seven planks and two sticks: the two on the grid and the five on the pointer came back. */
    CHECK_EQ_U64(pt_count(output, "the player mined 5 blocks and placed 2"), 1u);
    CHECK_EQ_U64(pt_count(output, "the inventory holds 10 items in 3 stacks"), 1u);
    /* From that save its west half is still there to be mined, once in the books, and the one bench it gives is picked up. So it is after the second. */
    CHECK_EQ_U64(pt_count(output, "the player mined 6 blocks and placed 2"), 2u);
    CHECK_EQ_U64(pt_count(output, "the last mined was woodworking_bench_west"), 2u);
    CHECK_EQ_U64(pt_count(output, "the inventory holds 11 items in 4 stacks"), 2u);

    static const char *const shots[] = {"bench_placed.png", "grid_two.png", "grid_back.png",   "reloaded.png",
                                        "reopened.png",     "west_aim.png", "west_broken.png", "gone_reloaded.png"};
    for (uint64_t i = 0u; i < sizeof(shots) / sizeof(shots[0]); i++) {
        uint64_t size = 0u;
        CHECK_EQ_U64(file_path_size(pt_join(t, pt_join(t, root, "shots"), shots[i]), &size), FILE_OK);
        CHECK(size > 1000u);
    }
    if (getenv("MC_PLAYTHROUGH_KEEP") != nullptr) {
        printf("  kept in %s\n", root);
    } else {
        pt_temp_remove(t, root);
    }
}

TEST(a_night_in_the_open_meets_zombies_skeletons_and_creepers_and_is_saved_with_them) {
    REQUIRE(t->arg_count >= 1);
    char *const root = pt_temp(t);
    REQUIRE(root != nullptr);
    test_process_t process;
    pt_run(t, "tests/run/playthrough/fixtures/night.script", root, PT_NIGHT_MS, 2u, &process);
    const char *const output = process.output;

    CHECK(pt_number_before(output, "mobs came") >= 10u);
    /* A zombie hurts and dies; a body dies and comes back, through the swing that is also a press. */
    const uint64_t blows = pt_number_before(output, "blows,");
    CHECK(blows != UINT64_MAX && blows >= 1u);
    CHECK(pt_number_before(output, "died and left") != UINT64_MAX);
    const char *const back = strstr(output, "came back ");
    REQUIRE(back != nullptr);
    CHECK(strtoull(back + strlen("came back "), nullptr, 10) >= 1u);
    /* A creeper bursts and leaves a hole. */
    CHECK(pt_number_before(output, "blasts broke") != UINT64_MAX && pt_number_before(output, "blasts broke") >= 1u);
    /* A skeleton shoots. */
    CHECK(pt_number_before(output, "arrows,") != UINT64_MAX && pt_number_before(output, "arrows,") >= 1u);
    if (getenv("MC_PLAYTHROUGH_KEEP") != nullptr) {
        printf("  kept in %s\n", root);
    } else {
        pt_temp_remove(t, root);
    }
}

TEST_MAIN()
