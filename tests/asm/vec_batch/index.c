/* ctest asm_vec_batch. See README.md. */
#include "asm/support/index.h"

/* Generated. Has the compiler confirm every number compile/src/struct_offsets gave to NASM. */
#include "struct_offsets/index.h"

#include "math/mat4/index.h"
#include "simd/reference/index.h"
#include "simd/vec_batch/index.h"

#include <string.h>

enum : uint64_t {
    MAX_SHORT = 80u,
    CANARY_BYTES = 64u,
    MANY = 20000u,
    UNALIGNED_COUNT = 1024u,
};
enum : uint8_t { CANARY = 0xA5u };

typedef void (*batch3_t)(vec3_t *out, const vec3_t *in, uint64_t count, const mat4_t *m);
typedef void (*batch4_t)(vec4_t *out, const vec4_t *in, uint64_t count, const mat4_t *m);

static float32_t random_float(uint64_t *state, float32_t scale) {
    const float32_t unit = (float32_t)(test_random(state) >> 40) / 16777216.0f32;
    return (unit * 2.0f32 - 1.0f32) * scale;
}

static void fill_vec3s(uint64_t *state, vec3_t *values, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        values[i] =
            vec3_make(random_float(state, 1000.0f32), random_float(state, 1000.0f32), random_float(state, 1000.0f32));
    }
}

static void fill_vec4s(uint64_t *state, vec4_t *values, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        values[i] = vec4_make(random_float(state, 1000.0f32), random_float(state, 1000.0f32),
                              random_float(state, 1000.0f32), random_float(state, 2.0f32));
    }
}

/* No zero, nothing alike and nothing tidy, so that a mixed-up element or a skipped term shows in every result. */
static void fill_matrix(uint64_t *state, mat4_t *m) {
    for (uint32_t i = 0; i < 16u; i++) {
        m->m[i] = random_float(state, 3.0f32) + (random_float(state, 1.0f32) < 0.0f32 ? -0.1f32 : 0.1f32);
    }
}

/* An input with its element number in every component shows any lane going to the wrong place. */
TEST(the_identity_keeps_every_element_in_its_place) {
    enum : uint64_t { COUNT = 67u };
    vec3_t in3[COUNT];
    vec3_t out3[COUNT];
    vec4_t in4[COUNT];
    vec4_t out4[COUNT];
    for (uint64_t i = 0; i < COUNT; i++) {
        const float32_t n = (float32_t)i;
        in3[i] = vec3_make(n + 0.25f32, n + 1000.5f32, n + 2000.75f32);
        in4[i] = vec4_make(n + 0.25f32, n + 1000.5f32, n + 2000.75f32, n + 3000.125f32);
    }
    mat4_t m;
    mat4_identity(&m);
    vec_batch_transform_points(out3, in3, COUNT, &m);
    CHECK_MEM_EQ(out3, in3, sizeof(in3));
    vec_batch_transform_directions(out3, in3, COUNT, &m);
    CHECK_MEM_EQ(out3, in3, sizeof(in3));
    vec_batch_mul_vec4(out4, in4, COUNT, &m);
    CHECK_MEM_EQ(out4, in4, sizeof(in4));
}

TEST(known_answers) {
    mat4_t m;
    mat4_translation(vec3_make(10.0f32, 20.0f32, 30.0f32), &m);
    /* A row of 9 in a batch and a tail: both paths have to get them right. */
    vec3_t in[9];
    vec3_t out[9];
    for (uint32_t i = 0; i < 9u; i++) {
        in[i] = vec3_make((float32_t)i, 1.0f32, 2.0f32);
    }
    vec_batch_transform_points(out, in, 9u, &m);
    for (uint32_t i = 0; i < 9u; i++) {
        CHECK(out[i].x == (float32_t)i + 10.0f32);
        CHECK(out[i].y == 21.0f32);
        CHECK(out[i].z == 32.0f32);
    }
    /* A direction does not move. */
    vec_batch_transform_directions(out, in, 9u, &m);
    for (uint32_t i = 0; i < 9u; i++) {
        CHECK(out[i].x == (float32_t)i);
        CHECK(out[i].y == 1.0f32);
        CHECK(out[i].z == 2.0f32);
    }
    /* A vec4_t's w scales the translation. */
    vec4_t in4[3];
    vec4_t out4[3];
    for (uint32_t i = 0; i < 3u; i++) {
        in4[i] = vec4_make(1.0f32, 2.0f32, 3.0f32, (float32_t)i);
    }
    vec_batch_mul_vec4(out4, in4, 3u, &m);
    for (uint32_t i = 0; i < 3u; i++) {
        CHECK(out4[i].x == 1.0f32 + 10.0f32 * (float32_t)i);
        CHECK(out4[i].y == 2.0f32 + 20.0f32 * (float32_t)i);
        CHECK(out4[i].z == 3.0f32 + 30.0f32 * (float32_t)i);
        CHECK(out4[i].w == (float32_t)i);
    }
}

/* The reference is the mat4 function, and a batch has to give what one vector at a time gives, to the bit. */
static void check_vec3_many(test_context_t *t, batch3_t kernel, batch3_t reference) {
    uint64_t state = 0x1234ABCDu;
    vec3_t *in = test_alloc(t, MANY * sizeof(vec3_t));
    vec3_t *got = test_alloc(t, MANY * sizeof(vec3_t));
    vec3_t *want = test_alloc(t, MANY * sizeof(vec3_t));
    for (uint32_t round = 0; round < 40u; round++) {
        mat4_t m;
        fill_matrix(&state, &m);
        fill_vec3s(&state, in, MANY);
        const uint64_t count = MANY - test_random(&state) % 64u;
        memset(got, 0, MANY * sizeof(vec3_t));
        memset(want, 0, MANY * sizeof(vec3_t));
        kernel(got, in, count, &m);
        reference(want, in, count, &m);
        if (!CHECK_MEM_EQ(got, want, MANY * sizeof(vec3_t))) {
            return;
        }
    }
}

TEST(points_match_their_reference_bit_for_bit) {
    check_vec3_many(t, vec_batch_transform_points, simd_reference_vec_batch_transform_points);
}

TEST(directions_match_their_reference_bit_for_bit) {
    check_vec3_many(t, vec_batch_transform_directions, simd_reference_vec_batch_transform_directions);
}

TEST(vec4s_match_their_reference_bit_for_bit) {
    uint64_t state = 0x4321DCBAu;
    vec4_t *in = test_alloc(t, (MANY + 1u) * sizeof(vec4_t));
    vec4_t *got = test_alloc(t, (MANY + 1u) * sizeof(vec4_t));
    vec4_t *want = test_alloc(t, (MANY + 1u) * sizeof(vec4_t));
    for (uint32_t round = 0; round < 40u; round++) {
        mat4_t m;
        fill_matrix(&state, &m);
        fill_vec4s(&state, in, MANY);
        const uint64_t count = MANY - test_random(&state) % 8u;
        memset(got, 0, MANY * sizeof(vec4_t));
        memset(want, 0, MANY * sizeof(vec4_t));
        vec_batch_mul_vec4(got, in, count, &m);
        simd_reference_vec_batch_mul_vec4(want, in, count, &m);
        if (!CHECK_MEM_EQ(got, want, MANY * sizeof(vec4_t))) {
            return;
        }
    }
}

/* Nothing past count is written, or read: both arrays sit against a page that faults. */
static void check_vec3_short(test_context_t *t, batch3_t kernel, batch3_t reference) {
    guard_t in_guard;
    guard_t out_guard;
    REQUIRE(guard_init(&in_guard));
    REQUIRE(guard_init(&out_guard));
    uint64_t state = 0xF00Du;
    mat4_t m;
    fill_matrix(&state, &m);
    vec3_t values[MAX_SHORT];
    vec3_t want[MAX_SHORT];
    for (uint64_t count = 0; count <= MAX_SHORT; count++) {
        const uint64_t bytes = count * sizeof(vec3_t);
        fill_vec3s(&state, values, MAX_SHORT);
        reference(want, values, count, &m);

        vec3_t *in_high = (vec3_t *)(void *)(in_guard.borrowed_high - bytes);
        vec3_t *out_high = (vec3_t *)(void *)(out_guard.borrowed_high - bytes);
        memcpy(in_high, values, bytes);
        kernel(out_high, in_high, count, &m);
        CHECK_MEM_EQ(out_high, want, bytes);

        vec3_t *in_low = (vec3_t *)(void *)in_guard.borrowed_low;
        vec3_t *out_low = (vec3_t *)(void *)out_guard.borrowed_low;
        memcpy(in_low, values, bytes);
        uint8_t *past = out_guard.borrowed_low + bytes;
        memset(past, CANARY, CANARY_BYTES);
        kernel(out_low, in_low, count, &m);
        CHECK_MEM_EQ(out_low, want, bytes);
        for (uint64_t i = 0; i < CANARY_BYTES; i++) {
            CHECK_EQ_U64(past[i], CANARY);
        }
    }
    guard_deinit(&out_guard);
    guard_deinit(&in_guard);
}

TEST(short_runs_of_points_stay_inside_their_buffers) {
    check_vec3_short(t, vec_batch_transform_points, simd_reference_vec_batch_transform_points);
}

TEST(short_runs_of_directions_stay_inside_their_buffers) {
    check_vec3_short(t, vec_batch_transform_directions, simd_reference_vec_batch_transform_directions);
}

TEST(short_runs_of_vec4s_stay_inside_their_buffers) {
    guard_t in_guard;
    guard_t out_guard;
    REQUIRE(guard_init(&in_guard));
    REQUIRE(guard_init(&out_guard));
    uint64_t state = 0xF00Du;
    mat4_t m;
    fill_matrix(&state, &m);
    vec4_t values[MAX_SHORT];
    vec4_t want[MAX_SHORT];
    for (uint64_t count = 0; count <= MAX_SHORT; count++) {
        const uint64_t bytes = count * sizeof(vec4_t);
        fill_vec4s(&state, values, MAX_SHORT);
        vec_batch_mul_vec4(want, values, 0u, &m); /* a count of 0 touches nothing */
        simd_reference_vec_batch_mul_vec4(want, values, count, &m);

        vec4_t *in_high = (vec4_t *)(void *)(in_guard.borrowed_high - bytes);
        vec4_t *out_high = (vec4_t *)(void *)(out_guard.borrowed_high - bytes);
        memcpy(in_high, values, bytes);
        vec_batch_mul_vec4(out_high, in_high, count, &m);
        CHECK_MEM_EQ(out_high, want, bytes);

        vec4_t *in_low = (vec4_t *)(void *)in_guard.borrowed_low;
        vec4_t *out_low = (vec4_t *)(void *)out_guard.borrowed_low;
        memcpy(in_low, values, bytes);
        uint8_t *past = out_guard.borrowed_low + bytes;
        memset(past, CANARY, CANARY_BYTES);
        vec_batch_mul_vec4(out_low, in_low, count, &m);
        CHECK_MEM_EQ(out_low, want, bytes);
        for (uint64_t i = 0; i < CANARY_BYTES; i++) {
            CHECK_EQ_U64(past[i], CANARY);
        }
    }
    guard_deinit(&out_guard);
    guard_deinit(&in_guard);
}

/* A vec3_t needs only 4-byte alignment, so an array may start anywhere in a vector. */
TEST(a_vec3_array_may_start_anywhere_in_a_vector) {
    uint64_t state = 0xCAFEu;
    mat4_t m;
    fill_matrix(&state, &m);
    const uint64_t bytes = UNALIGNED_COUNT * sizeof(vec3_t);
    vec3_t *values = test_alloc(t, bytes);
    vec3_t *want = test_alloc(t, bytes);
    uint8_t *in = test_alloc(t, bytes + 64u);
    uint8_t *out = test_alloc(t, bytes + 64u);
    fill_vec3s(&state, values, UNALIGNED_COUNT);
    simd_reference_vec_batch_transform_points(want, values, UNALIGNED_COUNT, &m);
    for (uint64_t offset = 0; offset < 32u; offset += 4u) {
        memcpy(in + offset, values, bytes);
        vec_batch_transform_points((vec3_t *)(void *)(out + 28u - offset), (const vec3_t *)(const void *)(in + offset),
                                   UNALIGNED_COUNT, &m);
        CHECK_MEM_EQ(out + 28u - offset, want, bytes);
    }
}

TEST(in_and_out_may_be_the_same_array) {
    uint64_t state = 0xBEEFu;
    enum : uint64_t { COUNT = 203u };
    mat4_t m;
    fill_matrix(&state, &m);
    vec3_t values3[COUNT];
    vec3_t want3[COUNT];
    fill_vec3s(&state, values3, COUNT);
    simd_reference_vec_batch_transform_points(want3, values3, COUNT, &m);
    vec_batch_transform_points(values3, values3, COUNT, &m);
    CHECK_MEM_EQ(values3, want3, sizeof(values3));

    vec4_t values4[COUNT];
    vec4_t want4[COUNT];
    fill_vec4s(&state, values4, COUNT);
    simd_reference_vec_batch_mul_vec4(want4, values4, COUNT, &m);
    vec_batch_mul_vec4(values4, values4, COUNT, &m);
    CHECK_MEM_EQ(values4, want4, sizeof(values4));
}

TEST_MAIN()
