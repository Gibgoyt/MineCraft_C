/* ctest run_terrain_landform_measure. See README.md. */
#include "harness/index.h"

#include <stdint.h>
#include <stdio.h>

enum : uint32_t { LANDFORM_TEST_TIMEOUT_MS = 240000u };

TEST(the_landform_measures_give_the_known_numbers_on_synthetic_ground) {
    char *const argv[] = {t->borrowed_args[0], (char *)"--self-test", nullptr};
    test_process_t run;
    test_process_run(t, argv, LANDFORM_TEST_TIMEOUT_MS, &run);
    /* What each check got, so a failure says by how much. */
    fputs(run.output, stdout);
    CHECK(run.started != 0u);
    CHECK(run.timed_out == 0u);
    CHECK_EQ_I64((int64_t)run.exit_code, 0);
    CHECK_CONTAINS(run.output, "a_one_to_one_cone_is_all_steep_and_has_concavity_one: ");
    CHECK_CONTAINS(run.output, "a_power_two_and_a_half_peak_has_concavity_three: ");
    CHECK_CONTAINS(run.output, "a_five_by_five_km_plateau_is_one_piece: ");
    CHECK_CONTAINS(run.output, "a_plane_of_five_to_one_is_all_moderate: ");
    CHECK_CONTAINS(run.output, "knobs_are_binned_by_their_base: ");
    CHECK_CONTAINS(run.output, "the_same_input_gives_the_same_table: ");
    CHECK_CONTAINS(run.output, "bench_landform --self-test: every check held");
}

TEST_MAIN()
