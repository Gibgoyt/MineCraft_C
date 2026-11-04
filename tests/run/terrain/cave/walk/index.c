/* ctest run_terrain_cave_walk. See README.md. */
#include "harness/index.h"

#include "terrain/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    WALK_TEST_PATH_BYTES = 256u,
    WALK_TEST_BENCH_TIMEOUT_MS = 120000u,
    WALK_TEST_GAME_TIMEOUT_MS = 120000u,
};

/* How far the game's body may end from the tool's. The game goes on for some ticks after the keys come up, and the body slows down
 * over those, so they are never the same place to the bit. */
static const float64_t WALK_TEST_ACROSS_MOST = 1.0f64;
static const float64_t WALK_TEST_UP_MOST = 0.75f64;

/* Three numbers after a marker, as "x, y, z". */
static void walk_test_read_place(test_context_t *t, const char *text, const char *marker, float64_t *place) {
    const char *const at = strstr(text, marker);
    REQUIRE(at != nullptr);
    char *rest = nullptr;
    place[0] = strtod(at + strlen(marker), &rest);
    REQUIRE(rest[0] == ',');
    place[1] = strtod(rest + 1, &rest);
    REQUIRE(rest[0] == ',');
    place[2] = strtod(rest + 1, &rest);
}

static void walk_test_remove_dir(const char *path) {
    DIR *const handle = opendir(path);
    if (handle != nullptr) {
        char file[WALK_TEST_PATH_BYTES * 2u];
        for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
            if (entry->d_name[0] != '.') {
                snprintf(file, sizeof(file), "%s/%s", path, entry->d_name);
                unlink(file);
            }
        }
        closedir(handle);
    }
    rmdir(path);
}

TEST(a_body_walks_from_where_a_game_starts_into_a_cave_and_the_deep_levels_and_the_game_agrees) {
    char dir[WALK_TEST_PATH_BYTES];
    snprintf(dir, sizeof(dir), "/tmp/mc_c_cave_walk_XXXXXX");
    REQUIRE(mkdtemp(dir) != nullptr);

    /* The tool: real blocks, and the real body's own moves. Its exit status says both walks got there. */
    char *const bench_argv[] = {t->borrowed_args[1], (char *)"--route-only", dir, nullptr};
    test_process_t bench;
    test_process_run(t, bench_argv, WALK_TEST_BENCH_TIMEOUT_MS, &bench);
    CHECK(bench.started != 0u);
    CHECK(bench.timed_out == 0u);
    CHECK_EQ_I64((int64_t)bench.exit_code, 0);
    CHECK_CONTAINS(bench.output, "cave: the body walked it in ");
    CHECK_CONTAINS(bench.output, "deep: the body walked it in ");
    float64_t bench_end[3];
    const char *const walked = strstr(bench.output, "cave: the body walked it in ");
    REQUIRE(walked != nullptr);
    walk_test_read_place(t, walked, "and ended at ", bench_end);

    /* The game: its threads, its mobs and its renderer, with only the keys and the mouse to go on. */
    char script[WALK_TEST_PATH_BYTES * 2u];
    snprintf(script, sizeof(script), "%s/%" PRIu64 "_cave.script", dir, (uint64_t)TERRAIN_SEED_DEFAULT);
    char *const game_argv[] = {t->borrowed_args[0],
                               (char *)"--headless",
                               (char *)"--test-world",
                               (char *)"--walk",
                               (char *)"--render-distance",
                               (char *)"8",
                               (char *)"--script",
                               script,
                               (char *)"--screenshot-dir",
                               dir,
                               (char *)"--width",
                               (char *)"160",
                               (char *)"--height",
                               (char *)"90",
                               nullptr};
    test_process_t game;
    test_process_run(t, game_argv, WALK_TEST_GAME_TIMEOUT_MS, &game);
    CHECK(game.started != 0u);
    CHECK(game.timed_out == 0u);
    CHECK_EQ_I64((int64_t)game.exit_code, 0);
    CHECK_CONTAINS(game.output, "hurt 0 times, died 0");
    float64_t game_end[3];
    walk_test_read_place(t, game.output, "the player is at ", game_end);
    printf("the tool's body ended at %.3f, %.3f, %.3f, and the game's at %.3f, %.3f, %.3f\n",
           (libc_double_t)bench_end[0], (libc_double_t)bench_end[1], (libc_double_t)bench_end[2],
           (libc_double_t)game_end[0], (libc_double_t)game_end[1], (libc_double_t)game_end[2]);
    CHECK(fabs(game_end[0] - bench_end[0]) <= WALK_TEST_ACROSS_MOST);
    CHECK(fabs(game_end[1] - bench_end[1]) <= WALK_TEST_ACROSS_MOST);
    CHECK(fabs(game_end[2] - bench_end[2]) <= WALK_TEST_UP_MOST);
    walk_test_remove_dir(dir);
}

TEST_MAIN()
