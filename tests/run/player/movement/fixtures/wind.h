/* ctest run_player_movement: plan 004 phase 08's wind on the body. Here and not in index.c to keep that file under the 1500 lines
 * where plans/CODING_STANDARD.md section 5 asks for a split. Included once, by index.c, after the helpers these use. */
#ifndef MC_TESTS_RUN_PLAYER_MOVEMENT_FIXTURES_WIND_H
#define MC_TESTS_RUN_PLAYER_MOVEMENT_FIXTURES_WIND_H

static void pm_wind_ticks(test_world_t *w, player_movement_t *body, const player_intent_t *intent, float32_t wind_u,
                          float32_t wind_v, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        player_movement_tick_wind(body, w->physics, w->map, intent, wind_u, wind_v);
    }
}

/* Blocks a second along x over the last ticks, in a wind: west is below 0. */
static float64_t pm_wind_speed_x(test_world_t *w, player_movement_t *body, float32_t wind_u, float32_t wind_v,
                                 uint32_t ticks) {
    const float64_t x = body->position[0];
    pm_wind_ticks(w, body, &pm_idle, wind_u, wind_v, ticks);
    return (body->position[0] - x) * PM_TICKS_PER_SECOND / (float64_t)ticks;
}

TEST(hurricane_drifts_the_body_at_2_blocks_a_second) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_movement_t body;
    /* A hurricane's eyewall, 60 blocks a second from the east: 0.04 of the 50 over the threshold, west. */
    player_movement_start(&body, 30.5f64, 0.5f64, 0.0f64);
    pm_wind_ticks(&w, &body, &pm_idle, -60.0f32, 0.0f32, 160u);
    CHECK_NEAR(pm_wind_speed_x(&w, &body, -60.0f32, 0.0f32, 40u), -2.0f64, 0.02f64);
    CHECK_NEAR(body.position[1], 0.5f64, 1.0e-12f64); /* straight downwind */
    CHECK_EQ_U64(body.on_ground, 1u);
    /* A 30 blocks a second storm: 0.8. */
    player_movement_start(&body, 30.5f64, 0.5f64, 0.0f64);
    pm_wind_ticks(&w, &body, &pm_idle, -30.0f32, 0.0f32, 160u);
    CHECK_NEAR(pm_wind_speed_x(&w, &body, -30.0f32, 0.0f32, 40u), -0.8f64, 0.02f64);
    /* The same drift along a slant: 60 from the north-east, down the diagonal. */
    player_movement_start(&body, 30.5f64, 30.5f64, 0.0f64);
    pm_wind_ticks(&w, &body, &pm_idle, -42.4264069f32, -42.4264069f32, 160u);
    const float64_t x = body.position[0], y = body.position[1];
    pm_wind_ticks(&w, &body, &pm_idle, -42.4264069f32, -42.4264069f32, 40u);
    const float64_t dx = body.position[0] - x, dy = body.position[1] - y;
    CHECK_NEAR(sqrt(dx * dx + dy * dy) * PM_TICKS_PER_SECOND / 40.0f64, 2.0f64, 0.02f64);
    /* Walking into it is slower than with it, and a jump in it goes under 2 blocks downwind. */
    player_intent_t into = pm_idle;
    into.forward = 1; /* a yaw of 0 walks east, into a wind from the east */
    player_movement_start(&body, -20.5f64, 0.5f64, 0.0f64);
    pm_wind_ticks(&w, &body, &into, -60.0f32, 0.0f32, 40u);
    const float64_t start = body.position[0];
    pm_wind_ticks(&w, &body, &into, -60.0f32, 0.0f32, 40u);
    CHECK_NEAR((body.position[0] - start) * PM_TICKS_PER_SECOND / 40.0f64, 4.317f64 - 2.0f64, 0.05f64);
    player_movement_start(&body, 30.5f64, 0.5f64, 0.0f64);
    player_intent_t jump = pm_idle;
    jump.jump = 1u;
    pm_wind_ticks(&w, &body, &jump, -60.0f32, 0.0f32, 1u);
    pm_wind_ticks(&w, &body, &pm_idle, -60.0f32, 0.0f32, 11u);
    CHECK(body.position[0] > 28.5f64);
    test_world_end(&w);
}

TEST(no_push_below_the_threshold) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_movement_t still, blown;
    player_movement_start(&still, 0.5f64, 0.5f64, 0.0f64);
    player_movement_start(&blown, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &still, &pm_idle, 200u);
    /* Trade winds, westerlies, and the threshold itself, from any side. */
    pm_wind_ticks(&w, &blown, &pm_idle, 10.0f32, 0.0f32, 50u);
    pm_wind_ticks(&w, &blown, &pm_idle, 0.0f32, -8.0f32, 50u);
    pm_wind_ticks(&w, &blown, &pm_idle, -6.0f32, 6.0f32, 50u);
    pm_wind_ticks(&w, &blown, &pm_idle, 7.0710678f32, 7.0710678f32, 50u);
    CHECK_MEM_EQ(&blown, &still, sizeof(still));
    test_world_end(&w);
}

TEST(a_wall_upwind_shelters) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    /* A stone wall 2 blocks east of the body's middle, as tall as it: the hurricane from the east is a quarter, 0.5 blocks a
     * second, for as long as the wall is within 3 blocks. */
    test_world_fill(t, &w, 4, -5, 0, 4, 5, 2, BLOCK_ID_STONE);
    player_movement_t body;
    player_movement_start(&body, 2.5f64, 0.5f64, 0.0f64);
    pm_wind_ticks(&w, &body, &pm_idle, -60.0f32, 0.0f32, 10u);
    CHECK_NEAR(pm_wind_speed_x(&w, &body, -60.0f32, 0.0f32, 20u), -0.5f64, 0.02f64);
    /* Once it has gone more than 3 blocks from the wall, the whole push again. */
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_wind_ticks(&w, &body, &pm_idle, -60.0f32, 0.0f32, 40u);
    CHECK_NEAR(pm_wind_speed_x(&w, &body, -60.0f32, 0.0f32, 20u), -2.0f64, 0.02f64);
    /* A wall downwind shelters nothing. */
    player_movement_start(&body, 6.5f64, 0.5f64, 0.0f64);
    pm_wind_ticks(&w, &body, &pm_idle, 60.0f32, 0.0f32, 10u);
    CHECK_NEAR(pm_wind_speed_x(&w, &body, 60.0f32, 0.0f32, 10u), 0.5f64, 0.02f64);
    test_world_end(&w);
}

TEST(no_push_in_water_or_on_a_ladder) {
    test_world_t w;
    REQUIRE(pm_begin_pool(t, &w, BLOCK_ID_WATER));
    player_movement_t still, blown;
    player_movement_start(&still, 0.5f64, 0.5f64, 10.0f64);
    player_movement_start(&blown, 0.5f64, 0.5f64, 10.0f64);
    pm_ticks(&w, &still, &pm_idle, 200u);
    pm_wind_ticks(&w, &blown, &pm_idle, -60.0f32, 25.0f32, 200u);
    CHECK_EQ_U64(blown.mode, PLAYER_MOVEMENT_MODE_WATER);
    CHECK_MEM_EQ(&blown, &still, sizeof(still));
    test_world_end(&w);

    REQUIRE(pm_begin_ladder(t, &w, BLOCK_ID_LADDER));
    player_intent_t climb = pm_idle;
    climb.jump = 1u;
    player_movement_start(&still, 2.5f64, 2.5f64, 0.0f64);
    player_movement_start(&blown, 2.5f64, 2.5f64, 0.0f64);
    pm_ticks(&w, &still, &climb, 30u);
    pm_wind_ticks(&w, &blown, &climb, 60.0f32, -45.0f32, 30u);
    CHECK_EQ_U64(blown.mode, PLAYER_MOVEMENT_MODE_CLIMB);
    CHECK_MEM_EQ(&blown, &still, sizeof(still));
    test_world_end(&w);
}

TEST(zero_wind_is_the_same_bytes_as_before) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    uint64_t rng = 0x0ddba11deadbeefull;
    pm_fill_random(t, &w, &rng);
    player_movement_t before, now;
    player_movement_start(&before, 0.5f64, 0.5f64, 9.0f64);
    player_movement_start(&now, 0.5f64, 0.5f64, 9.0f64);
    bool same = true;
    for (uint32_t i = 0u; i < 2000u && same; i++) {
        const player_intent_t intent = pm_random_intent(&rng);
        player_movement_tick(&before, w.physics, w.map, &intent);
        player_movement_tick_wind(&now, w.physics, w.map, &intent, 0.0f32, 0.0f32);
        same = memcmp(&before, &now, sizeof(before)) == 0;
    }
    CHECK(same);
    test_world_end(&w);
}

#endif
