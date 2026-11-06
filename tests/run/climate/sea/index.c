/* ctest run_climate_sea. See README.md. */
#include "harness/index.h"

#include "climate/sea/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    SEA_WINDOW = 272u,
    SEA_SMALL = 64u,
    SEA_GUARD = 4u,
    SEA_GUARD_WORD = 0xDEADBEEFu,
};

/* Arrays for one grid, each with guard words either side. */
typedef struct sea_grid_t {
    uint8_t *water;                /* 0 */
    uint8_t *kind;                 /* 8 */
    uint32_t *labels;              /* 16 */
    uint32_t *queue;               /* 24 */
    uint32_t *distance;            /* 32 */
    uint32_t *nearest;             /* 40 */
    climate_sea_scratch_t scratch; /* 48 */
    uint32_t side;                 /* 64 */
    uint8_t _pad0[4];              /* 68 */
} sea_grid_t;
static_assert(sizeof(sea_grid_t) == 72);

static uint32_t *sea_guarded_words(test_context_t *t, uint64_t count) {
    uint32_t *const words = test_alloc(t, (count + 2u * SEA_GUARD) * sizeof(uint32_t));
    for (uint32_t g = 0u; g < SEA_GUARD; g++) {
        words[g] = SEA_GUARD_WORD;
        words[SEA_GUARD + count + g] = SEA_GUARD_WORD;
    }
    return words + SEA_GUARD;
}

static bool sea_guards_hold(const uint32_t *words, uint64_t count) {
    bool ok = true;
    for (uint32_t g = 0u; g < SEA_GUARD; g++) {
        ok = ok && words[(int64_t)g - (int64_t)SEA_GUARD] == SEA_GUARD_WORD && words[count + g] == SEA_GUARD_WORD;
    }
    return ok;
}

static sea_grid_t sea_grid(test_context_t *t, uint32_t side) {
    const uint64_t cells = (uint64_t)side * side;
    sea_grid_t grid = {
        .water = test_alloc(t, cells),
        .kind = test_alloc(t, cells),
        .labels = sea_guarded_words(t, cells),
        .queue = sea_guarded_words(t, cells),
        .distance = sea_guarded_words(t, cells),
        .nearest = sea_guarded_words(t, cells),
        .scratch = {.words = sea_guarded_words(t, 5u * (uint64_t)side + 1u),
                    .crossings = test_alloc(t, ((uint64_t)side + 1u) * sizeof(float64_t))},
        .side = side,
        ._pad0 = {0},
    };
    return grid;
}

static void sea_square(sea_grid_t *grid, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h) {
    for (uint32_t y = y0; y < y0 + h; y++) {
        for (uint32_t x = x0; x < x0 + w; x++) {
            grid->water[y * grid->side + x] = 1u;
        }
    }
}

/* ---- Components -------------------------------------------------------------------------------------------------- */

TEST(components_are_labelled_as_the_worked_example_says) {
    /* The phase 03 file's 6 by 6 grid, '#' land and '.' water, the top line y = 5. */
    static const char *const ROWS[6] = {"..####", "..#..#", ".##..#", ".#####", "..##.#", "...#.."};
    sea_grid_t grid = sea_grid(t, 6u);
    for (uint32_t r = 0u; r < 6u; r++) {
        const uint32_t y = 5u - r;
        for (uint32_t x = 0u; x < 6u; x++) {
            grid.water[y * 6u + x] = ROWS[r][x] == '.' ? CLIMATE_SEA_WATER_SHALLOW : CLIMATE_SEA_WATER_NONE;
        }
    }
    grid.water[5u * 6u + 0u] = CLIMATE_SEA_WATER_DEEP; /* A reaches the sea floor at (0, 5) */
    const uint32_t count = climate_sea_classify(grid.water, 6u, grid.labels, grid.queue, grid.kind);
    CHECK_EQ_U64(count, 3u);
    uint32_t sizes[4] = {0u, 0u, 0u, 0u};
    for (uint32_t i = 0u; i < 36u; i++) {
        sizes[grid.labels[i] < 4u ? grid.labels[i] : 0u]++;
    }
    /* A: all of column 0 and five more; the file says 12, and counting its own picture gives 11 (Issues found). */
    CHECK_EQ_U64(sizes[1], 11u);
    CHECK_EQ_U64(sizes[2], 3u);
    CHECK_EQ_U64(sizes[3], 4u);
    CHECK_EQ_U64(grid.labels[0 * 6u + 0u], 1u);
    CHECK_EQ_U64(grid.labels[0 * 6u + 4u], 2u);
    CHECK_EQ_U64(grid.labels[3 * 6u + 3u], 3u);
    /* (4, 1) is B's and (3, 3) is C's: a diagonal does not join them. */
    CHECK_EQ_U64(grid.labels[1 * 6u + 4u], 2u);
    CHECK_EQ_U64(grid.kind[0], CLIMATE_SEA_KIND_SEA);
    CHECK_EQ_U64(grid.kind[4], CLIMATE_SEA_KIND_LAKE_SMALL); /* B touches the edge, which counts for nothing */
    CHECK_EQ_U64(grid.kind[3 * 6u + 3u], CLIMATE_SEA_KIND_LAKE_SMALL);
    CHECK_EQ_U64(grid.kind[2 * 6u + 2u], CLIMATE_SEA_KIND_LAND);
    CHECK(sea_guards_hold(grid.labels, 36u) && sea_guards_hold(grid.queue, 36u));
}

/* The kind of the water at (x, y) on a 272 grid holding one enclosed square of w by h, away from every edge. */
static uint8_t sea_square_kind(test_context_t *t, uint32_t w, uint32_t h) {
    sea_grid_t grid = sea_grid(t, SEA_WINDOW);
    sea_square(&grid, 40u, 40u, w, h);
    (void)climate_sea_classify(grid.water, SEA_WINDOW, grid.labels, grid.queue, grid.kind);
    return grid.kind[41u * SEA_WINDOW + 41u];
}

/* Issues found 17: the rule in place of "touching the window's edge". */
TEST(deep_water_makes_its_whole_body_sea_and_the_window_edge_does_not) {
    sea_grid_t grid = sea_grid(t, SEA_WINDOW);
    sea_square(&grid, 0u, 0u, 5u, SEA_WINDOW); /* a shallow band along the west edge: 1360 cells, under 8192 */
    sea_square(&grid, 100u, 100u, 5u, 5u);     /* 25 shallow cells, one of them deep */
    grid.water[104u * SEA_WINDOW + 104u] = CLIMATE_SEA_WATER_DEEP;
    sea_square(&grid, 200u, 200u, 5u, 5u); /* 25 shallow cells */
    (void)climate_sea_classify(grid.water, SEA_WINDOW, grid.labels, grid.queue, grid.kind);
    CHECK_EQ_U64(grid.kind[100u * SEA_WINDOW + 2u], CLIMATE_SEA_KIND_LAKE_LARGE);
    CHECK_EQ_U64(grid.kind[100u * SEA_WINDOW + 100u], CLIMATE_SEA_KIND_SEA);
    CHECK_EQ_U64(grid.kind[202u * SEA_WINDOW + 202u], CLIMATE_SEA_KIND_LAKE_SMALL);
    CHECK_EQ_U64(grid.kind[100u * SEA_WINDOW + 50u], CLIMATE_SEA_KIND_LAND);
}

TEST(a_big_enclosed_water_is_sea_and_a_small_one_a_lake) {
    CHECK_EQ_U64(sea_square_kind(t, 91u, 91u), CLIMATE_SEA_KIND_SEA);        /* 8281 >= 8192 */
    CHECK_EQ_U64(sea_square_kind(t, 90u, 90u), CLIMATE_SEA_KIND_LAKE_LARGE); /* 8100 */
    CHECK_EQ_U64(sea_square_kind(t, 22u, 22u), CLIMATE_SEA_KIND_LAKE_SMALL); /* 484 < 489 */
    CHECK_EQ_U64(sea_square_kind(t, 23u, 22u), CLIMATE_SEA_KIND_LAKE_LARGE); /* 506 */
    /* The thresholds themselves: 8192 is sea and 8191 not; 489 is large and 488 not. */
    CHECK_EQ_U64(sea_square_kind(t, 128u, 64u), CLIMATE_SEA_KIND_SEA);       /* 8192 */
    CHECK_EQ_U64(sea_square_kind(t, 163u, 3u), CLIMATE_SEA_KIND_LAKE_LARGE); /* 489 */
    CHECK_EQ_U64(sea_square_kind(t, 122u, 4u), CLIMATE_SEA_KIND_LAKE_SMALL); /* 488 */
    sea_grid_t grid = sea_grid(t, SEA_WINDOW);
    sea_square(&grid, 40u, 40u, 91u, 90u); /* 8190 */
    sea_square(&grid, 131u, 40u, 1u, 1u);  /* 8191, one cell joined on at the east */
    (void)climate_sea_classify(grid.water, SEA_WINDOW, grid.labels, grid.queue, grid.kind);
    CHECK_EQ_U64(grid.kind[41u * SEA_WINDOW + 41u], CLIMATE_SEA_KIND_LAKE_LARGE);
    CHECK_EQ_U64(grid.kind[40u * SEA_WINDOW + 131u], CLIMATE_SEA_KIND_LAKE_LARGE);
}

TEST(a_channel_one_cell_wide_joins_a_lake_to_the_sea_and_a_diagonal_does_not) {
    sea_grid_t grid = sea_grid(t, SEA_WINDOW);
    sea_square(&grid, 0u, 0u, 4u, SEA_WINDOW); /* the sea along the west edge, deep at one cell */
    grid.water[10u * SEA_WINDOW + 1u] = CLIMATE_SEA_WATER_DEEP;
    sea_square(&grid, 60u, 40u, 30u, 30u);   /* a lake of 900 cells */
    sea_square(&grid, 4u, 55u, 56u, 1u);     /* a channel one cell wide, from the sea to it */
    sea_square(&grid, 150u, 150u, 30u, 30u); /* another, which touches a third only at a corner */
    sea_square(&grid, 180u, 180u, 30u, 30u);
    (void)climate_sea_classify(grid.water, SEA_WINDOW, grid.labels, grid.queue, grid.kind);
    CHECK_EQ_U64(grid.kind[50u * SEA_WINDOW + 70u], CLIMATE_SEA_KIND_SEA);
    CHECK_EQ_U64(grid.kind[160u * SEA_WINDOW + 160u], CLIMATE_SEA_KIND_LAKE_LARGE);
    CHECK_EQ_U64(grid.kind[190u * SEA_WINDOW + 190u], CLIMATE_SEA_KIND_LAKE_LARGE);
    CHECK(grid.labels[160u * SEA_WINDOW + 160u] != grid.labels[190u * SEA_WINDOW + 190u]);
}

/* ---- Distance ---------------------------------------------------------------------------------------------------- */

static uint32_t sea_brute_distance(const uint32_t *marks, uint32_t mark_count, uint32_t side, uint32_t at) {
    uint32_t best = CLIMATE_SEA_FAR;
    const int64_t x = at % side, y = at / side;
    for (uint32_t m = 0u; m < mark_count; m++) {
        const int64_t dx = (int64_t)(marks[m] % side) - x, dy = (int64_t)(marks[m] / side) - y;
        const uint32_t d = (uint32_t)(dx * dx + dy * dy);
        best = d < best ? d : best;
    }
    return best;
}

/* Every cell's distance against a search over every marked cell; every nearest at exactly that distance, and marked. */
static void sea_check_against_brute(test_context_t *t, sea_grid_t *grid, const char *what) {
    const uint32_t side = grid->side;
    const uint32_t cells = side * side;
    uint32_t *const marks = test_alloc(t, (uint64_t)cells * sizeof(uint32_t));
    uint32_t mark_count = 0u;
    for (uint32_t i = 0u; i < cells; i++) {
        if (grid->water[i] != 0u) {
            marks[mark_count++] = i;
        }
    }
    climate_sea_distance(grid->water, side, grid->distance, grid->nearest, &grid->scratch);
    uint64_t wrong = 0u;
    for (uint32_t i = 0u; i < cells; i++) {
        const uint32_t expected = sea_brute_distance(marks, mark_count, side, i);
        bool ok = grid->distance[i] == expected;
        if (expected == CLIMATE_SEA_FAR) {
            ok = ok && grid->nearest[i] == CLIMATE_SEA_FAR;
        } else {
            const uint32_t n = grid->nearest[i];
            const int64_t dx = (int64_t)(n % side) - (int64_t)(i % side);
            const int64_t dy = (int64_t)(n / side) - (int64_t)(i / side);
            ok = ok && n < cells && grid->water[n] != 0u && (uint32_t)(dx * dx + dy * dy) == expected;
        }
        wrong += ok ? 0u : 1u;
    }
    printf("%s: %u marked, %" PRIu64 " wrong\n", what, mark_count, wrong);
    CHECK_EQ_U64(wrong, 0u);
    CHECK(sea_guards_hold(grid->distance, cells) && sea_guards_hold(grid->nearest, cells));
    CHECK(sea_guards_hold(grid->scratch.words, 5u * (uint64_t)side + 1u));
}

/* splitmix64, the same every run. */
static uint64_t sea_next(uint64_t *state) {
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

TEST(distance_matches_a_brute_force_search_on_synthetic_coasts) {
    const uint32_t s = SEA_SMALL;
    {
        sea_grid_t grid = sea_grid(t, s);
        sea_square(&grid, 0u, 0u, 20u, s);
        sea_check_against_brute(t, &grid, "a straight north-south coast");
    }
    {
        sea_grid_t grid = sea_grid(t, s);
        for (uint32_t y = 0u; y < s; y++) {
            for (uint32_t x = 0u; x < s; x++) {
                grid.water[y * s + x] = x + y < 50u ? 1u : 0u;
            }
        }
        sea_check_against_brute(t, &grid, "a diagonal coast");
    }
    {
        sea_grid_t grid = sea_grid(t, s);
        sea_square(&grid, 0u, 0u, s, 10u);
        sea_square(&grid, 0u, 0u, 10u, s);
        sea_square(&grid, 20u, 20u, 30u, 4u);
        sea_check_against_brute(t, &grid, "an L-shaped bay and a strip");
    }
    {
        sea_grid_t grid = sea_grid(t, s);
        for (uint32_t i = 0u; i < s * s; i++) {
            grid.water[i] = ((i % s) + (i / s)) % 2u == 0u ? 1u : 0u;
        }
        sea_check_against_brute(t, &grid, "a checkerboard");
    }
    {
        sea_grid_t grid = sea_grid(t, s);
        memset(grid.water, 1, (uint64_t)s * s);
        sea_check_against_brute(t, &grid, "all sea");
    }
    {
        sea_grid_t grid = sea_grid(t, s);
        sea_check_against_brute(t, &grid, "no sea");
    }
    {
        sea_grid_t grid = sea_grid(t, SEA_WINDOW);
        uint64_t state = 12345u;
        for (uint32_t m = 0u; m < 200u; m++) {
            grid.water[sea_next(&state) % ((uint64_t)SEA_WINDOW * SEA_WINDOW)] = 1u;
        }
        sea_check_against_brute(t, &grid, "200 random sea cells on the whole window");
    }
    {
        sea_grid_t grid = sea_grid(t, SEA_WINDOW);
        const int64_t middle = SEA_WINDOW / 2u;
        for (uint32_t y = 0u; y < SEA_WINDOW; y++) {
            for (uint32_t x = 0u; x < SEA_WINDOW; x++) {
                const int64_t dx = (int64_t)x - middle, dy = (int64_t)y - middle;
                grid.water[y * SEA_WINDOW + x] = dx * dx + dy * dy <= 40 * 40 ? 1u : 0u;
            }
        }
        sea_check_against_brute(t, &grid, "a round island of radius 40 on the whole window");
    }
}

TEST(the_worked_example_row_is_exact) {
    sea_grid_t grid = sea_grid(t, 8u);
    grid.water[2] = 1u;
    grid.water[6] = 1u;
    climate_sea_distance(grid.water, 8u, grid.distance, grid.nearest, &grid.scratch);
    static const uint32_t DISTANCE[8] = {4u, 1u, 0u, 1u, 4u, 1u, 0u, 1u};
    static const uint32_t NEAREST[8] = {2u, 2u, 2u, 2u, 2u, 6u, 6u, 6u};
    for (uint32_t q = 0u; q < 8u; q++) {
        CHECK_EQ_U64(grid.distance[q], DISTANCE[q]);
        CHECK_EQ_U64(grid.nearest[q], NEAREST[q]);
    }
}

TEST(no_marked_cell_gives_far_everywhere) {
    sea_grid_t grid = sea_grid(t, SEA_WINDOW);
    climate_sea_distance(grid.water, SEA_WINDOW, grid.distance, grid.nearest, &grid.scratch);
    uint64_t far = 0u;
    for (uint32_t i = 0u; i < SEA_WINDOW * SEA_WINDOW; i++) {
        far += grid.distance[i] == CLIMATE_SEA_FAR && grid.nearest[i] == CLIMATE_SEA_FAR ? 1u : 0u;
    }
    CHECK_EQ_U64(far, (uint64_t)SEA_WINDOW * SEA_WINDOW);
    CHECK(sea_guards_hold(grid.distance, (uint64_t)SEA_WINDOW * SEA_WINDOW));
    CHECK(sea_guards_hold(grid.nearest, (uint64_t)SEA_WINDOW * SEA_WINDOW));
    CHECK(sea_guards_hold(grid.scratch.words, 5u * SEA_WINDOW + 1u));
}

TEST_MAIN()
