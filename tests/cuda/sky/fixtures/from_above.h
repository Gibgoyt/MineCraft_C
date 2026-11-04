/* ctest render_feature_sky: the fog seen from high over the ground, the way the top of a tall world sees it. Included once, by index.c, after everything
 * these name. */
#ifndef MC_TESTS_CUDA_SKY_FIXTURES_FROM_ABOVE_H
#define MC_TESTS_CUDA_SKY_FIXTURES_FROM_ABOVE_H

/* The arithmetic. Ordinary heights are the old fog to the byte, and no more than that ever changes. */
TEST(what_goes_up_or_down_counts_for_fog_only_as_far_as_where_fog_starts) {
    const post_settings_t post = {.fog_start = 347.2f32, .fog_end = 496.0f32};
    for (uint32_t turn = 0u; turn <= 200u; turn++) {
        const float32_t z = -1.0f32 + (float32_t)turn * 0.01f32;
        for (float32_t distance = 1.0f32; distance < 5000.0f32; distance *= 1.05f32) {
            const float32_t fog = post_fog_along(&post, z, distance);
            CHECK(fog >= 0.0f32 && fog <= post_fog(&post, distance));
            if (fabsf(z) * distance <= post.fog_start) {
                CHECK(fog == post_fog(&post, distance));
            }
        }
    }
    /* Straight down, from any height: the ground is in clear air. */
    CHECK(post_fog_along(&post, -1.0f32, 1186.0f32) == 0.0f32);
    CHECK(post_fog_along(&post, 1.0f32, 1186.0f32) == 0.0f32);
    /* Level, it is all as it was. */
    CHECK(post_fog_along(&post, 0.0f32, 496.0f32) == 1.0f32);
    CHECK_NEAR(post_fog_along(&post, 0.0f32, 421.6f32), 0.25f32, 1e-3f32);
    /* Whole at the side that makes the way fog_end, from any height, and a chunk short of the edge of what is loaded. */
    const float32_t whole = sqrtf(496.0f32 * 496.0f32 - 347.2f32 * 347.2f32);
    const float32_t height = 1186.0f32;
    const float32_t past = sqrtf((whole + 1.0f32) * (whole + 1.0f32) + height * height);
    const float32_t before = sqrtf((whole - 8.0f32) * (whole - 8.0f32) + height * height);
    CHECK(post_fog_along(&post, -height / past, past) == 1.0f32);
    CHECK(post_fog_along(&post, -height / before, before) < 1.0f32);
    /* Sky is past all of it, and asks for no square of itself. */
    CHECK(post_fog_along(&post, -1.0f32, TRAVERSE_COARSE_SKY) == 1.0f32);
    /* With fog off, nothing that is hit has any. */
    const post_settings_t off = {.fog_start = TRAVERSE_COARSE_SKY * 0.5f32, .fog_end = TRAVERSE_COARSE_SKY};
    CHECK(post_fog_along(&off, -1.0f32, 1186.0f32) == 0.0f32);
    CHECK(post_fog_along(&off, -1.0f32, TRAVERSE_COARSE_SKY) == 1.0f32);
}

/* On the card: stone at z = 0 under an eye looking down from far past where fog is whole, as the top of the world is at the default render distance. */
TEST(from_high_above_the_ground_shows_under_the_eye_and_melts_into_the_sky_at_the_side) {
    enum : uint32_t { FROM_ABOVE_HEIGHTS = 2u };
    enum : int32_t { FLOOR_REACH = 72 };
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    const float32_t fog_end = 40.0f32;
    REQUIRE(sk_open(t, &rig, 8u, SK_WIDTH, SK_HEIGHT, fog_end));
    for (int32_t y = -FLOOR_REACH; y < FLOOR_REACH; y++) {
        for (int32_t x = -FLOOR_REACH; x < FLOOR_REACH; x++) {
            REQUIRE(world_edit_block_set(&rig.world->edit, x, y, 0, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
        }
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_frame_t *const frame = &rig.render->frame;
    const post_settings_t *const post = &frame->post;
    uint8_t *const clear = test_alloc(t, (uint64_t)SK_WIDTH * SK_HEIGHT * 4u);
    const float64_t heights[FROM_ABOVE_HEIGHTS] = {160.0f64, 1240.0f64};
    for (uint32_t h = 0u; h < FROM_ABOVE_HEIGHTS; h++) {
        render_camera_t camera;
        render_camera_start(&camera, 0.5f64, 0.5f64, heights[h], 0.0f32, -1.5707f32);
        traverse_camera_t view;
        render_camera_to_traverse(&camera, SK_WIDTH, SK_HEIGHT, &view);
        render_frame_features_set(frame, SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_FOG);
        const uint8_t *picture = sk_draw(t, &rig, &camera);
        REQUIRE(picture != nullptr);
        memcpy(clear, picture, (uint64_t)SK_WIDTH * SK_HEIGHT * 4u);
        render_frame_features_set(frame, SK_FEATURES_ALL);
        picture = sk_draw(t, &rig, &camera);
        REQUIRE(picture != nullptr);
        uint64_t in_clear_air = 0u, in_fog = 0u, wrong_clear = 0u, wrong_fog = 0u;
        for (uint32_t py = 0u; py < SK_HEIGHT; py++) {
            for (uint32_t px = 0u; px < SK_WIDTH; px++) {
                const float32_t a = 2.0f32 * ((float32_t)px + 0.5f32) / (float32_t)SK_WIDTH - 1.0f32;
                const float32_t b = 1.0f32 - 2.0f32 * ((float32_t)py + 0.5f32) / (float32_t)SK_HEIGHT;
                float32_t direction[3];
                float32_t length = 0.0f32;
                for (uint32_t i = 0u; i < 3u; i++) {
                    direction[i] = view.forward[i] + a * view.right[i] + b * view.up[i];
                    length += direction[i] * direction[i];
                }
                length = sqrtf(length);
                for (uint32_t i = 0u; i < 3u; i++) {
                    direction[i] /= length;
                }
                /* To the floor's top, by the eye's own height. */
                const float32_t way = ((float32_t)heights[h] - 1.0f32) / -direction[2];
                const float32_t x = 0.5f32 + direction[0] * way, y = 0.5f32 + direction[1] * way;
                if (fabsf(x) > (float32_t)FLOOR_REACH - 2.0f32 || fabsf(y) > (float32_t)FLOOR_REACH - 2.0f32) {
                    continue;
                }
                const float32_t fog = post_fog_along(post, direction[2], way);
                const uint32_t seen = sk_pixel(picture, SK_WIDTH, px, py);
                if (fog < 0.01f32) {
                    in_clear_air++;
                    wrong_clear += sk_bytes_near(seen, sk_pixel(clear, SK_WIDTH, px, py), 6u) ? 0u : 1u;
                } else if (fog >= 1.0f32) {
                    in_fog++;
                    float32_t sky[3];
                    post_sky(post, direction, sky);
                    const uint32_t only_sky =
                        post_xrgb(post_tone_map(sky[0]), post_tone_map(sky[1]), post_tone_map(sky[2]));
                    wrong_fog += sk_bytes_near(seen, only_sky, 2u) ? 0u : 1u;
                }
            }
        }
        printf("  from %.0f blocks up: %" PRIu64 " pixels of ground in clear air, %" PRIu64 " wrong; %" PRIu64
               " over the floor all fog, %" PRIu64 " wrong\n",
               (libc_double_t)heights[h], in_clear_air, wrong_clear, in_fog, wrong_fog);
        CHECK(in_clear_air >= 1u);
        CHECK_EQ_U64(wrong_clear, 0u);
        CHECK_EQ_U64(wrong_fog, 0u);
        if (h == 0u) {
            CHECK(in_fog > 1000u); /* near enough that the floor goes out past where fog is whole */
        }
    }
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

#endif
