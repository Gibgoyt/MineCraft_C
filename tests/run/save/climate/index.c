/* ctest run_save_climate. See README.md. The phase 07 file's tests of run/src/save/climate/: a store read back byte for
 * byte, bad files refused and left alone, a world reopened mid-week giving the same bytes, a crash after the file rerunning
 * the same census, and a world from before this phase starting from spin-up. */
#include "harness/index.h"

#include "climate/weather/index.h"
#include "file/index.h"
#include "save/climate/index.h"
#include "terrain/rng/index.h"

#include "run/game/census/fixtures/weather.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint64_t SC_HASH_START = 0xCBF29CE484222325ull;

/* A tile of random valid state at week. */
static void sc_random_tile(climate_week_store_t *store, terrain_rng_t *rng, int32_t tile_x, int32_t tile_y,
                           uint64_t week) {
    climate_week_tile_t *const tile =
        climate_week_store_add(store, tile_x, tile_y, terrain_rng_next_below(rng, 2u) == 0u ? 1u : 3u, nullptr);
    tile->week = (uint32_t)week;
    for (uint32_t w = 0u; w < 4u; w++) {
        tile->generated[w] = terrain_rng_next_u64(rng);
    }
    for (uint32_t c = 0u; c < CLIMATE_TILE_CELL_COUNT; c++) {
        climate_week_t *const cell = &tile->cells[c];
        cell->t_mean = -30.0f32 + 60.0f32 * terrain_rng_next_f32(rng);
        cell->t_min = cell->t_mean - 5.0f32;
        cell->t_max = cell->t_mean + 5.0f32;
        cell->rain_mm = 100.0f32 * terrain_rng_next_f32(rng);
        cell->snow_mm = 500.0f32 * terrain_rng_next_f32(rng);
        cell->soil_mm = 150.0f32 * terrain_rng_next_f32(rng);
        cell->runoff_mm = 50.0f32 * terrain_rng_next_f32(rng);
        cell->wind[0] = -10.0f32 + 20.0f32 * terrain_rng_next_f32(rng);
        cell->wind[1] = -10.0f32 + 20.0f32 * terrain_rng_next_f32(rng);
        cell->ice_cm = 40.0f32 * terrain_rng_next_f32(rng);
        cell->flood = 3.0f32 * terrain_rng_next_f32(rng);
        for (uint32_t d = 0u; d < CLIMATE_WEEK_DAYS; d++) {
            cell->day_kind[d] = (uint8_t)terrain_rng_next_below(rng, CLIMATE_DAY_KIND_COUNT);
        }
    }
}

static bool sc_write_plain(const char *path, const uint8_t *data, uint64_t bytes) {
    file_t file;
    if (file_open(&file, path, FILE_MODE_WRITE) != FILE_OK) {
        return false;
    }
    const bool ok = file_write_all(&file, data, bytes) == FILE_OK;
    return file_close(&file) == FILE_OK && ok;
}

TEST(a_store_reads_back_byte_for_byte) {
    gw_rig_t rig;
    REQUIRE(gw_start(t, &rig, 4u));
    char *const directory = gw_directory(t);
    REQUIRE(directory != nullptr);
    climate_week_store_t *const store = climate_week_store(rig.climate);
    terrain_rng_t rng = terrain_rng_start(21u);
    for (uint32_t i = 0u; i < 50u; i++) {
        /* Not in order: the file sorts them. */
        sc_random_tile(store, &rng, (int32_t)(i * 7u % 11u) - 5, (int32_t)(i / 11u) - 2, 30u);
    }
    store->week = 30u;
    const uint64_t before = gw_digest(t, rig.climate);
    uint32_t *const sorted = test_alloc(t, 64u * sizeof(uint32_t));
    uint32_t *const scratch = test_alloc(t, 64u * sizeof(uint32_t));
    REQUIRE(save_climate_write(directory, GW_SEED, store, sorted, scratch) == FILE_OK);
    char path[FILE_PATH_BYTES];
    REQUIRE(save_climate_path(directory, 30u, path));
    uint64_t bytes = 0u;
    const uint8_t *const file = gw_file(t, path, &bytes);
    REQUIRE(file != nullptr);
    CHECK_EQ_U64(bytes, sizeof(save_climate_header_t) + 50u * sizeof(climate_week_tile_t));
    /* Every padding byte zero: the header's, each tile's, each cell's. */
    const save_climate_header_t *const header = (const save_climate_header_t *)(const void *)file;
    uint32_t nonzero = 0u;
    for (uint32_t i = 0u; i < sizeof(header->_pad0); i++) {
        nonzero += header->_pad0[i] != 0u ? 1u : 0u;
    }
    for (uint32_t i = 0u; i < 50u; i++) {
        const climate_week_tile_t *const tile =
            (const climate_week_tile_t *)(const void *)(file + sizeof(save_climate_header_t) +
                                                        (uint64_t)i * sizeof(climate_week_tile_t));
        for (uint32_t b = 0u; b < sizeof(tile->_pad0); b++) {
            nonzero += tile->_pad0[b] != 0u ? 1u : 0u;
        }
        for (uint32_t c = 0u; c < CLIMATE_TILE_CELL_COUNT; c++) {
            for (uint32_t b = 0u; b < sizeof(tile->cells[c]._pad0); b++) {
                nonzero += tile->cells[c]._pad0[b] != 0u ? 1u : 0u;
            }
        }
    }
    climate_week_store_empty(store);
    uint64_t week = 0u;
    const save_climate_status_t status = save_climate_open(directory, GW_SEED, 40u, store, &week);
    const uint64_t after = gw_digest(t, rig.climate);
    printf("50 tiles: %" PRIu64 " bytes on disk, status %u at week %" PRIu64 ", %u tiles back, padding not zero %u; "
           "digest %016" PRIx64 " and %016" PRIx64 "\n",
           bytes, (uint32_t)status, week, store->tile_count, nonzero, before, after);
    CHECK_EQ_U64(status, SAVE_CLIMATE_LOADED);
    CHECK_EQ_U64(week, 30u);
    CHECK_EQ_U64(store->tile_count, 50u);
    CHECK_EQ_U64(nonzero, 0u);
    CHECK(before == after);
    /* Written again from what was read: the same bytes. */
    REQUIRE(save_climate_write(directory, GW_SEED, store, sorted, scratch) == FILE_OK);
    uint64_t again_bytes = 0u;
    const uint8_t *const again = gw_file(t, path, &again_bytes);
    REQUIRE(again != nullptr && again_bytes == bytes);
    CHECK_MEM_EQ(again, file, bytes);
    gw_stop(&rig);
    gw_remove(t, directory);
}

/* Rewrites a file's header checksum and tiles' hash after a change of the test's, so only the value itself is wrong. */
static void sc_rehash(uint8_t *file, uint64_t bytes) {
    save_climate_header_t *const header = (save_climate_header_t *)(void *)file;
    header->tiles_hash = save_climate_hash(SC_HASH_START, file + sizeof(*header), bytes - sizeof(*header));
    header->checksum = save_climate_hash(SC_HASH_START, header, offsetof(save_climate_header_t, checksum));
}

/* Opens what is on disk and checks it is refused, the store left empty, and the file as it was. */
static bool sc_refused(test_context_t *t, const char *directory, const char *path, uint64_t seed,
                       climate_week_store_t *store, const uint8_t *expected, uint64_t bytes, uint8_t *now) {
    /* now holds bytes + 1: one buffer for every call, as a test calls this tens of thousands of times. */
    (void)t;
    uint64_t week = 0u;
    const save_climate_status_t status = save_climate_open(directory, seed, 30u, store, &week);
    uint64_t now_bytes = 0u;
    const bool read = file_read_whole(path, now, bytes + 1u, &now_bytes) == FILE_OK;
    return status == SAVE_CLIMATE_REFUSED && store->tile_count == 0u && week == 0u && read && now_bytes == bytes &&
           memcmp(now, expected, bytes) == 0;
}

TEST(a_bad_file_is_refused_and_left_alone) {
    gw_rig_t rig;
    REQUIRE(gw_start(t, &rig, 4u));
    char *const directory = gw_directory(t);
    REQUIRE(directory != nullptr);
    climate_week_store_t *const store = climate_week_store(rig.climate);
    terrain_rng_t rng = terrain_rng_start(31u);
    sc_random_tile(store, &rng, 0, 0, 30u);
    sc_random_tile(store, &rng, 1, 0, 30u);
    store->week = 30u;
    uint32_t sorted[4], scratch[4];
    REQUIRE(save_climate_write(directory, GW_SEED, store, sorted, scratch) == FILE_OK);
    char path[FILE_PATH_BYTES];
    REQUIRE(save_climate_path(directory, 30u, path));
    uint64_t bytes = 0u;
    const uint8_t *const good = gw_file(t, path, &bytes);
    REQUIRE(good != nullptr);
    uint8_t *const bad = test_alloc(t, bytes);
    uint8_t *const now = test_alloc(t, bytes + 1u);
    /* A bit flipped at every byte. */
    uint32_t refused = 0u;
    for (uint64_t i = 0u; i < bytes; i++) {
        memcpy(bad, good, bytes);
        bad[i] ^= (uint8_t)(1u << (i % 8u));
        REQUIRE(sc_write_plain(path, bad, bytes));
        refused += sc_refused(t, directory, path, GW_SEED, store, bad, bytes, now) ? 1u : 0u;
    }
    printf("a bit flipped at each of %" PRIu64 " bytes: %u refused and left as they were\n", bytes, refused);
    CHECK_EQ_U64(refused, bytes);
    /* Another seed. */
    REQUIRE(sc_write_plain(path, good, bytes));
    CHECK(sc_refused(t, directory, path, GW_SEED + 1u, store, good, bytes, now));
    /* Another version, with every hash good. */
    memcpy(bad, good, bytes);
    ((save_climate_header_t *)(void *)bad)->version = SAVE_CLIMATE_VERSION + 1u;
    sc_rehash(bad, bytes);
    REQUIRE(sc_write_plain(path, bad, bytes));
    CHECK(sc_refused(t, directory, path, GW_SEED, store, bad, bytes, now));
    /* Soil of 151, and a NaN, each with good hashes. */
    climate_week_tile_t *const first = (climate_week_tile_t *)(void *)(bad + sizeof(save_climate_header_t));
    memcpy(bad, good, bytes);
    first->cells[17].soil_mm = 151.0f32;
    sc_rehash(bad, bytes);
    REQUIRE(sc_write_plain(path, bad, bytes));
    CHECK(sc_refused(t, directory, path, GW_SEED, store, bad, bytes, now));
    memcpy(bad, good, bytes);
    first->cells[200].snow_mm = nanf("");
    sc_rehash(bad, bytes);
    REQUIRE(sc_write_plain(path, bad, bytes));
    CHECK(sc_refused(t, directory, path, GW_SEED, store, bad, bytes, now));
    /* And the census then starts from spin-up, and says so on its screen. */
    gw_open(&rig, directory, 30u);
    CHECK_EQ_U64(rig.census->refused, 1u);
    CHECK_EQ_U64(store->tile_count, 0u);
    CHECK_EQ_U64(store->week, 30u);
    gw_close(&rig);
    /* The good file is believed. */
    REQUIRE(sc_write_plain(path, good, bytes));
    uint64_t week = 0u;
    CHECK_EQ_U64(save_climate_open(directory, GW_SEED, 30u, store, &week), SAVE_CLIMATE_LOADED);
    gw_stop(&rig);
    gw_remove(t, directory);
}

TEST(mid_week_save_and_load_give_the_same_bytes) {
    /* Uninterrupted: weeks 12 and 13. Interrupted: week 12 written, the world closed mid-week, reopened in a fresh climate and
     * store, the same land marked again as its columns come back, then week 13. */
    gw_rig_t whole, first, second;
    REQUIRE(gw_start(t, &whole, MC_THREADS_WORKERS));
    gw_open(&whole, nullptr, 11u);
    gw_mark_tiles(&whole, -2, 6, -2, 6);
    gw_weeks(&whole, 12u, 13u, 0u);
    const uint64_t uninterrupted = gw_digest(t, whole.climate);
    gw_stop(&whole);

    char *const directory = gw_directory(t);
    REQUIRE(directory != nullptr);
    REQUIRE(gw_start(t, &first, MC_THREADS_WORKERS));
    gw_open(&first, directory, 11u);
    gw_mark_tiles(&first, -2, 6, -2, 6);
    gw_weeks(&first, 12u, 12u, 0u);
    gw_stop(&first);
    char path[FILE_PATH_BYTES];
    REQUIRE(save_climate_path(directory, 12u, path));
    uint64_t bytes = 0u;
    const uint8_t *const saved = gw_file(t, path, &bytes);
    REQUIRE(saved != nullptr);

    REQUIRE(gw_start(t, &second, MC_THREADS_WORKERS));
    gw_open(&second, directory, 12u);
    CHECK_EQ_U64(climate_week_store(second.climate)->tile_count, 25u);
    gw_mark_tiles(&second, -2, 6, -2, 6);
    /* Reopened and saved again with no census between: the same file. */
    uint32_t sorted[64], scratch[64];
    REQUIRE(save_climate_write(directory, GW_SEED, climate_week_store(second.climate), sorted, scratch) == FILE_OK);
    uint64_t again_bytes = 0u;
    const uint8_t *const again = gw_file(t, path, &again_bytes);
    REQUIRE(again != nullptr && again_bytes == bytes);
    CHECK_MEM_EQ(again, saved, bytes);
    gw_weeks(&second, 13u, 13u, 0u);
    const uint64_t reopened = gw_digest(t, second.climate);
    printf("weeks 12 and 13 straight %016" PRIx64 ", reopened mid-week %016" PRIx64 "\n", uninterrupted, reopened);
    CHECK(uninterrupted == reopened);
    gw_stop(&second);
    gw_remove(t, directory);
}

TEST(a_crash_after_the_file_reruns_the_same_census) {
    char *const directory = gw_directory(t);
    REQUIRE(directory != nullptr);
    gw_rig_t rig;
    REQUIRE(gw_start(t, &rig, MC_THREADS_WORKERS));
    gw_open(&rig, directory, 11u);
    gw_mark_tiles(&rig, -2, 6, -2, 6);
    gw_weeks(&rig, 12u, 12u, 0u);
    gw_weeks(&rig, 13u, 13u, 0u);
    gw_stop(&rig);
    char path_12[FILE_PATH_BYTES], path_13[FILE_PATH_BYTES];
    REQUIRE(save_climate_path(directory, 12u, path_12));
    REQUIRE(save_climate_path(directory, 13u, path_13));
    uint64_t bytes = 0u, kept = 0u;
    const uint8_t *const first = gw_file(t, path_13, &bytes);
    REQUIRE(first != nullptr);
    CHECK(gw_file(t, path_12, &kept) != nullptr); /* the one before stays */

    /* Killed after week.13 was written but before the state file said 13: it opens at 12, and the census for 13 runs again. */
    REQUIRE(gw_start(t, &rig, MC_THREADS_WORKERS));
    gw_open(&rig, directory, 12u);
    gw_mark_tiles(&rig, -2, 6, -2, 6);
    gw_weeks(&rig, 13u, 13u, 0u);
    gw_stop(&rig);
    uint64_t rerun_bytes = 0u;
    const uint8_t *const rerun = gw_file(t, path_13, &rerun_bytes);
    REQUIRE(rerun != nullptr && rerun_bytes == bytes);
    CHECK_MEM_EQ(rerun, first, bytes);

    /* Killed after the trigger moved the state on to 13 but before week.13 was written: only week.12 is there, and opening at
     * 13 owes week 13 again by itself. */
    REQUIRE(file_remove(path_13) == FILE_OK);
    REQUIRE(gw_start(t, &rig, MC_THREADS_WORKERS));
    gw_open(&rig, directory, 13u);
    CHECK(!game_census_idle(rig.census));
    gw_settle(&rig, -1500.0f64, (float64_t)GW_Y);
    gw_stop(&rig);
    uint64_t owed_bytes = 0u;
    const uint8_t *const owed = gw_file(t, path_13, &owed_bytes);
    REQUIRE(owed != nullptr && owed_bytes == bytes);
    CHECK_MEM_EQ(owed, first, bytes);
    printf("week.13 three ways, %" PRIu64 " bytes each: %s\n", bytes,
           memcmp(owed, first, bytes) == 0 && memcmp(rerun, first, bytes) == 0 ? "the same" : "different");
    gw_remove(t, directory);
}

TEST(a_world_from_before_has_spin_up) {
    /* No climate/ directory: nothing to load, the store at the state file's week, and the first census starts each tile from
     * its spin-up. One cell worked out by hand from the spin-up and climate_weather_day gives the census's bytes. */
    char *const directory = gw_directory(t);
    REQUIRE(directory != nullptr);
    gw_rig_t rig;
    REQUIRE(gw_start(t, &rig, MC_THREADS_WORKERS));
    gw_open(&rig, directory, 40u);
    const climate_week_store_t *const store = climate_week_store(rig.climate);
    CHECK_EQ_U64(rig.census->refused, 0u);
    CHECK_EQ_U64(store->week, 40u);
    CHECK_EQ_U64(store->tile_count, 0u);
    gw_mark_tiles(&rig, -2, 6, -2, 6);
    CHECK_EQ_U64(store->borrowed_sides[0].fresh, 1u);
    gw_weeks(&rig, 41u, 41u, 0u);
    const int32_t cell_x = -2 * 16 + 5, cell_y = 6 * 16 + 9;
    const climate_week_t *const census = climate_week_store_cell(store, cell_x, cell_y);
    REQUIRE(census != nullptr);
    climate_week_t hand;
    REQUIRE(climate_week_spinup(rig.climate, cell_x, cell_y, 40u, &hand, nullptr));
    arena_t *const arena = gw_aligned(t, sizeof(arena_t));
    arena_init(arena, "test_spinup", gw_aligned(t, 16ull << 20), 16ull << 20);
    climate_day_t days[CLIMATE_WEEK_DAYS];
    for (uint32_t i = 0u; i < CLIMATE_WEEK_DAYS; i++) {
        arena_reset(arena);
        REQUIRE(climate_weather_day(rig.climate, cell_x * 64 + 32, cell_y * 64 + 32, 7 * 40 + (int64_t)i, &hand, arena,
                                    &days[i]));
    }
    const float32_t flood = census->flood;
    climate_week_update(&hand, days, &hand);
    hand.flood = flood; /* flow accumulation's, over all the kept cells */
    printf("cell %d, %d: the census's snow %.3f soil %.3f runoff %.3f rain %.3f; by hand %.3f %.3f %.3f %.3f\n", cell_x,
           cell_y, (libc_double_t)census->snow_mm, (libc_double_t)census->soil_mm, (libc_double_t)census->runoff_mm,
           (libc_double_t)census->rain_mm, (libc_double_t)hand.snow_mm, (libc_double_t)hand.soil_mm,
           (libc_double_t)hand.runoff_mm, (libc_double_t)hand.rain_mm);
    CHECK_MEM_EQ(census, &hand, sizeof(hand));
    gw_stop(&rig);
    gw_remove(t, directory);
}

TEST_MAIN()
