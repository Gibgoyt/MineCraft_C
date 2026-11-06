/* ctest run_climate_normals. See README.md. */
#include "harness/index.h"

#include "climate/normals/index.h"

#include <stdio.h>

static const float64_t NORMALS_TEST_LAT[7] = {0.0f64, 15.0f64, 30.0f64, 45.0f64, 60.0f64, 75.0f64, 90.0f64};

TEST(the_knots_are_the_master_plan_s_and_the_curves_pass_through_them) {
    climate_normals_t normals;
    climate_normals_fill(&normals);
    static const float64_t P_BELT[7] = {1008.0f64, 1012.0f64, 1020.0f64, 1013.0f64, 1000.0f64, 1010.0f64, 1020.0f64};
    static const float64_t T_MEAN[7] = {27.0f64, 26.0f64, 20.0f64, 11.0f64, 0.0f64, -14.0f64, -24.0f64};
    for (uint32_t i = 0u; i < 7u; i++) {
        CHECK_NEAR(climate_normals_p_belt(&normals, NORMALS_TEST_LAT[i]), P_BELT[i], 1.0e-12f64);
        CHECK_NEAR(climate_normals_t_mean(&normals, NORMALS_TEST_LAT[i]), T_MEAN[i], 1.0e-12f64);
        /* By |latitude|: the south is the same. */
        CHECK_NEAR(climate_normals_p_belt(&normals, -NORMALS_TEST_LAT[i]), P_BELT[i], 1.0e-12f64);
        CHECK_NEAR(climate_normals_t_mean(&normals, -NORMALS_TEST_LAT[i]), T_MEAN[i], 1.0e-12f64);
    }
    static const float64_t SWING_LAT[4] = {0.0f64, 30.0f64, 60.0f64, 90.0f64};
    static const float64_t A_SEA[4] = {1.0f64, 3.0f64, 6.0f64, 10.0f64};
    static const float64_t A_LAND[4] = {2.0f64, 9.0f64, 18.0f64, 22.0f64};
    for (uint32_t i = 0u; i < 4u; i++) {
        CHECK_NEAR(climate_normals_a_sea(&normals, SWING_LAT[i]), A_SEA[i], 1.0e-12f64);
        CHECK_NEAR(climate_normals_a_land(&normals, SWING_LAT[i]), A_LAND[i], 1.0e-12f64);
        CHECK_NEAR(climate_normals_a_sea(&normals, -SWING_LAT[i]), A_SEA[i], 1.0e-12f64);
        CHECK_NEAR(climate_normals_a_land(&normals, -SWING_LAT[i]), A_LAND[i], 1.0e-12f64);
    }
}

TEST(between_knots_the_curves_are_the_terrain_s_spline) {
    climate_normals_t normals;
    climate_normals_fill(&normals);
    const float64_t p_37 = climate_normals_p_belt(&normals, 37.5f64);
    const float64_t p_20 = climate_normals_p_belt(&normals, 20.0f64);
    const float64_t a_sea_15 = climate_normals_a_sea(&normals, 15.0f64);
    const float64_t a_land_15 = climate_normals_a_land(&normals, 15.0f64);
    const float64_t a_sea_45 = climate_normals_a_sea(&normals, 45.0f64);
    const float64_t a_land_45 = climate_normals_a_land(&normals, 45.0f64);
    const float64_t t_28 = climate_normals_t_mean(&normals, 28.0f64);
    printf("p_belt(37.5) %.4f, p_belt(20) %.4f, A_sea(15) %.4f, A_land(15) %.4f, A_sea(45) %.4f, A_land(45) %.4f, "
           "T_lat(28) %.4f\n",
           (libc_double_t)p_37, (libc_double_t)p_20, (libc_double_t)a_sea_15, (libc_double_t)a_land_15,
           (libc_double_t)a_sea_45, (libc_double_t)a_land_45, (libc_double_t)t_28);
    CHECK_NEAR(p_37, 1017.6375f64, 1.0e-4f64);
    CHECK_NEAR(p_20, 1014.8642f64, 1.0e-4f64);
    CHECK_NEAR(a_sea_15, 1.9500f64, 1.0e-4f64);
    CHECK_NEAR(a_land_45, 13.7921f64, 1.0e-4f64);
    CHECK_NEAR(t_28, 20.9862f64, 1.0e-4f64);
    /* Not a straight line: the worked examples' 1016.5, 2, 13.5 and 20.8 are all off. */
    CHECK(p_37 > 1016.6f64);
    CHECK(a_land_45 > 13.6f64);
}

TEST(a_value_past_ninety_is_the_value_at_ninety) {
    climate_normals_t normals;
    climate_normals_fill(&normals);
    CHECK_NEAR(climate_normals_p_belt(&normals, 96.7f64), 1020.0f64, 1.0e-12f64);
    CHECK_NEAR(climate_normals_p_belt(&normals, -96.7f64), 1020.0f64, 1.0e-12f64);
    CHECK_NEAR(climate_normals_t_mean(&normals, 120.0f64), -24.0f64, 1.0e-12f64);
}

TEST(built_from_an_arena_the_curves_are_the_same) {
    uint8_t *const memory = test_alloc(t, 4096u);
    arena_t arena;
    arena_init(&arena, "normals_test", memory, 4096u);
    const climate_normals_t *const built = climate_normals_build(&arena);
    climate_normals_t filled;
    climate_normals_fill(&filled);
    CHECK_MEM_EQ(built, &filled, sizeof(filled));
    arena_deinit(&arena);
}

TEST_MAIN()
