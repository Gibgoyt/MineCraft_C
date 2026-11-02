/* bench_simd: every NASM kernel against its scalar reference. See README.md.
 *
 * The rule from plans/CODING_STANDARD.md section 7 is that a kernel with no gain gets deleted, so this exits non-zero if any
 * gated row shows none, and it also compares the two outputs, since a fast wrong answer is no gain. */
#include "heap/index.h"
#include "math/mat4/index.h"
#include "simd/bitmask/index.h"
#include "simd/blend/index.h"
#include "simd/copy/index.h"
#include "simd/noise/index.h"
#include "simd/reference/index.h"
#include "simd/vec_batch/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "types/index.h"

#include <sched.h>
#include <stdio.h>
#include <string.h>

enum : uint64_t {
    BENCH_FRAME_WIDTH = 1920u,
    BENCH_FRAME_HEIGHT = 1080u,
    BENCH_PIXEL_BYTES = 4u,
    BENCH_FRAME_BYTES = BENCH_FRAME_WIDTH * BENCH_FRAME_HEIGHT * BENCH_PIXEL_BYTES,
    BENCH_BUFFER_BYTES = 2u * BENCH_FRAME_BYTES,
    BENCH_HUD_PIXELS = 65536u,
    BENCH_WORDS_SHORT = 8u,
    BENCH_WORDS_POPCOUNT_FLOOR = 24u, /* just past where the prototype says bitmask_popcount_batch starts to pay */
    BENCH_WORDS_SECTION = 64u,        /* one bit for each of the 4096 blocks of a section */
    BENCH_WORDS_LONG = 4096u,
    BENCH_SECTION_VALUES = 4096u,
    BENCH_NOISE_TAIL = 5u,
    BENCH_NOISE_COLUMN = 256u, /* one sample per column of a chunk */
    BENCH_NOISE_SECTION = 4096u,
    BENCH_VEC_FEW = 8u,
    BENCH_VEC_MANY = 64u,
    BENCH_VEC_MOST = 1024u,
    /* Short runs repeat, so that a round is long enough for the clock to read. */
    BENCH_ROUND_MIN_ELEMENTS = 16384u,
    BENCH_FLOATS_PER_POINT = 3u,
    BENCH_FILL_SEED = 0x9E3779B97F4A7C15ull,
};
static const int64_t bench_side_budget_ns = 30 * TIME_NS_PER_MS;
enum : uint32_t { BENCH_MIN_ROUNDS = 5u, BENCH_NOISE_SEED = 0x51D7A3C9u, BENCH_EQUAL_VALUE = 0x0101u };

/* All the memory, reused by every row. in_a and in_b are read, and each side writes its own out. */
typedef struct bench_t {
    heap_region_t owned_region;
    uint8_t *borrowed_in_a;
    uint8_t *borrowed_in_b;
    uint8_t *borrowed_out_kernel;
    uint8_t *borrowed_out_reference;
    uint8_t _pad0[16];
    mat4_t matrix;
} bench_t;
static_assert(sizeof(bench_t) == 128);
static_assert(alignof(bench_t) == 32);

/* Runs one side. out is where it writes, and count is what the row says. */
typedef void (*bench_run_t)(const bench_t *bench, uint8_t *out, uint64_t count);
/* Puts in place what the row reads. */
typedef void (*bench_fill_t)(const bench_t *bench);
/* Resets out before each round, for a kernel that reads its own output. Nullptr if it needs none. */
typedef void (*bench_reset_t)(const bench_t *bench, uint8_t *out, uint64_t count);

typedef struct bench_row_t {
    const char *kernel;
    const char *input;
    const char *unit;
    bench_run_t run_kernel;
    bench_run_t run_reference;
    bench_fill_t fill;
    bench_reset_t reset;
    uint64_t count;
    uint64_t compare_bytes; /* of out, for this row */
    uint8_t gated;          /* a length the kernel is for, so no gain here means the kernel goes */
    uint8_t _pad0[7];
} bench_row_t;
static_assert(sizeof(bench_row_t) == 80);

/* The wrappers are never inlined, so that nothing can decide a call's output is unused. */
#define BENCH_SIDE(name) [[gnu::noinline]] static void name(const bench_t *bench, uint8_t *out, uint64_t count)

/* ---- What the rows read ------------------------------------------------------------------------------------------ */

static uint64_t bench_random(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static void bench_fill_random_bytes(uint8_t *bytes, uint64_t count, uint64_t seed) {
    uint64_t state = seed;
    for (uint64_t i = 0; i < count; i += sizeof(uint64_t)) {
        const uint64_t word = bench_random(&state);
        memcpy(bytes + i, &word, sizeof(word));
    }
}

static void bench_fill_random(const bench_t *bench) {
    bench_fill_random_bytes(bench->borrowed_in_a, BENCH_BUFFER_BYTES, BENCH_FILL_SEED);
    bench_fill_random_bytes(bench->borrowed_in_b, BENCH_BUFFER_BYTES, ~(uint64_t)BENCH_FILL_SEED);
}

/* A HUD is runs of fully transparent and fully opaque pixels, with some partial alpha among them. */
static void bench_fill_hud(const bench_t *bench, uint64_t partial_percent) {
    bench_fill_random(bench);
    uint32_t *src = (uint32_t *)(void *)bench->borrowed_in_a;
    uint64_t state = BENCH_FILL_SEED;
    uint64_t i = 0;
    while (i < BENCH_HUD_PIXELS) {
        const uint64_t kind = bench_random(&state) % 100u;
        const uint64_t run = 8u + bench_random(&state) % 64u;
        for (uint64_t j = 0; j < run && i < BENCH_HUD_PIXELS; j++, i++) {
            if (kind < partial_percent) {
                continue; /* random alpha already */
            }
            src[i] = kind % 2u == 0u ? 0u : (src[i] | 0xFF000000u);
        }
    }
}

static void bench_fill_hud_none_partial(const bench_t *bench) {
    bench_fill_hud(bench, 0u);
}

static void bench_fill_hud_some_partial(const bench_t *bench) {
    bench_fill_hud(bench, 20u);
}

static void bench_fill_all_partial(const bench_t *bench) {
    bench_fill_random(bench);
    uint32_t *src = (uint32_t *)(void *)bench->borrowed_in_a;
    for (uint64_t i = 0; i < BENCH_HUD_PIXELS; i++) {
        /* An alpha of 1 to 254, which neither shortcut can take. */
        src[i] = (src[i] & 0x00FFFFFFu) | ((1u + (src[i] >> 24) % 254u) << 24);
    }
}

/* Palette-like values: mostly small, with many zeros. */
static void bench_fill_values(const bench_t *bench) {
    bench_fill_random(bench);
    for (uint64_t i = 0; i < BENCH_BUFFER_BYTES; i++) {
        bench->borrowed_in_a[i] &= 0x03u;
    }
}

/* Coordinates with a fraction, as a terrain sampler has, and a matrix with no zero in it. */
static void bench_fill_floats(const bench_t *bench) {
    float32_t *values = (float32_t *)(void *)bench->borrowed_in_a;
    uint64_t state = BENCH_FILL_SEED;
    for (uint64_t i = 0; i < BENCH_BUFFER_BYTES / sizeof(float32_t); i++) {
        values[i] = (float32_t)(int32_t)(bench_random(&state) >> 44) * 0.001f32 - 500.0f32;
    }
}

/* ---- The two sides of each row ------------------------------------------------------------------------------------ */

BENCH_SIDE(bench_copy_kernel) {
    copy_stream_batch(out, bench->borrowed_in_a, count);
}
BENCH_SIDE(bench_copy_reference) {
    simd_reference_copy_stream(out, bench->borrowed_in_a, count);
}
BENCH_SIDE(bench_copy_rows_kernel) {
    const uint64_t pitch = BENCH_FRAME_WIDTH * BENCH_PIXEL_BYTES;
    copy_stream_rows_batch(out, pitch, bench->borrowed_in_a, pitch, pitch, count / pitch);
}
BENCH_SIDE(bench_copy_rows_reference) {
    const uint64_t pitch = BENCH_FRAME_WIDTH * BENCH_PIXEL_BYTES;
    simd_reference_copy_stream_rows(out, pitch, bench->borrowed_in_a, pitch, pitch, count / pitch);
}

static void bench_reset_from_b(const bench_t *bench, uint8_t *out, uint64_t count) {
    memcpy(out, bench->borrowed_in_b, count * sizeof(uint32_t));
}

BENCH_SIDE(bench_over_kernel) {
    blend_over_batch((uint32_t *)(void *)out, (const uint32_t *)(const void *)bench->borrowed_in_a, count);
}
BENCH_SIDE(bench_over_reference) {
    simd_reference_blend_over((uint32_t *)(void *)out, (const uint32_t *)(const void *)bench->borrowed_in_a, count);
}
BENCH_SIDE(bench_premul_kernel) {
    blend_premul_batch((uint32_t *)(void *)out, (const uint32_t *)(const void *)bench->borrowed_in_a, count);
}
BENCH_SIDE(bench_premul_reference) {
    simd_reference_blend_premul((uint32_t *)(void *)out, (const uint32_t *)(const void *)bench->borrowed_in_a, count);
}

BENCH_SIDE(bench_popcount_kernel) {
    const uint64_t total = bitmask_popcount_batch((const uint64_t *)(const void *)bench->borrowed_in_a, count);
    memcpy(out, &total, sizeof(total));
}
BENCH_SIDE(bench_popcount_reference) {
    const uint64_t total = simd_reference_bitmask_popcount((const uint64_t *)(const void *)bench->borrowed_in_a, count);
    memcpy(out, &total, sizeof(total));
}

#define BENCH_BINARY_SIDE(name, function)                                                                              \
    BENCH_SIDE(name) {                                                                                                 \
        function((uint64_t *)(void *)out, (const uint64_t *)(const void *)bench->borrowed_in_a,                        \
                 (const uint64_t *)(const void *)bench->borrowed_in_b, count);                                         \
    }
BENCH_BINARY_SIDE(bench_and_kernel, bitmask_and_batch)
BENCH_BINARY_SIDE(bench_and_reference, simd_reference_bitmask_and)
BENCH_BINARY_SIDE(bench_or_kernel, bitmask_or_batch)
BENCH_BINARY_SIDE(bench_or_reference, simd_reference_bitmask_or)
BENCH_BINARY_SIDE(bench_andnot_kernel, bitmask_andnot_batch)
BENCH_BINARY_SIDE(bench_andnot_reference, simd_reference_bitmask_andnot)

BENCH_SIDE(bench_nonzero_u8_kernel) {
    bitmask_from_nonzero_u8_batch(bench->borrowed_in_a, count, (uint64_t *)(void *)out);
}
BENCH_SIDE(bench_nonzero_u8_reference) {
    simd_reference_bitmask_from_nonzero_u8(bench->borrowed_in_a, count, (uint64_t *)(void *)out);
}
BENCH_SIDE(bench_nonzero_u16_kernel) {
    bitmask_from_nonzero_u16_batch((const uint16_t *)(const void *)bench->borrowed_in_a, count,
                                   (uint64_t *)(void *)out);
}
BENCH_SIDE(bench_nonzero_u16_reference) {
    simd_reference_bitmask_from_nonzero_u16((const uint16_t *)(const void *)bench->borrowed_in_a, count,
                                            (uint64_t *)(void *)out);
}
BENCH_SIDE(bench_equal_u16_kernel) {
    bitmask_from_equal_u16_batch((const uint16_t *)(const void *)bench->borrowed_in_a, count, BENCH_EQUAL_VALUE,
                                 (uint64_t *)(void *)out);
}
BENCH_SIDE(bench_equal_u16_reference) {
    simd_reference_bitmask_from_equal_u16((const uint16_t *)(const void *)bench->borrowed_in_a, count,
                                          BENCH_EQUAL_VALUE, (uint64_t *)(void *)out);
}

/* The coordinates come from three parts of the one array. */
static const float32_t *bench_axis(const bench_t *bench, uint64_t axis) {
    const uint64_t floats_per_axis = BENCH_BUFFER_BYTES / sizeof(float32_t) / BENCH_FLOATS_PER_POINT;
    return (const float32_t *)(const void *)bench->borrowed_in_a + axis * floats_per_axis;
}

BENCH_SIDE(bench_noise_kernel) {
    noise3_batch8((float32_t *)(void *)out, bench_axis(bench, 0u), bench_axis(bench, 1u), bench_axis(bench, 2u), count,
                  BENCH_NOISE_SEED);
}
BENCH_SIDE(bench_noise_reference) {
    simd_reference_noise3((float32_t *)(void *)out, bench_axis(bench, 0u), bench_axis(bench, 1u), bench_axis(bench, 2u),
                          count, BENCH_NOISE_SEED);
}

#define BENCH_VEC_SIDE(name, function, vector_t)                                                                       \
    BENCH_SIDE(name) {                                                                                                 \
        function((vector_t *)(void *)out, (const vector_t *)(const void *)bench->borrowed_in_a, count,                 \
                 &bench->matrix);                                                                                      \
    }
BENCH_VEC_SIDE(bench_points_kernel, vec_batch_transform_points, vec3_t)
BENCH_VEC_SIDE(bench_points_reference, simd_reference_vec_batch_transform_points, vec3_t)
BENCH_VEC_SIDE(bench_directions_kernel, vec_batch_transform_directions, vec3_t)
BENCH_VEC_SIDE(bench_directions_reference, simd_reference_vec_batch_transform_directions, vec3_t)
BENCH_VEC_SIDE(bench_vec4_kernel, vec_batch_mul_vec4, vec4_t)
BENCH_VEC_SIDE(bench_vec4_reference, simd_reference_vec_batch_mul_vec4, vec4_t)

/* ---- The rows ------------------------------------------------------------------------------------------------------ */

#define BENCH_ROW(kernel_name, input_name, unit_name, prefix, fill_function, reset_function, element_count, bytes,     \
                  is_gated)                                                                                            \
    {.kernel = kernel_name,                                                                                            \
     .input = input_name,                                                                                              \
     .unit = unit_name,                                                                                                \
     .run_kernel = prefix##_kernel,                                                                                    \
     .run_reference = prefix##_reference,                                                                              \
     .fill = fill_function,                                                                                            \
     .reset = reset_function,                                                                                          \
     .count = element_count,                                                                                           \
     .compare_bytes = bytes,                                                                                           \
     .gated = is_gated,                                                                                                \
     ._pad0 = {0}}

#define BENCH_WORD_ROWS(name, prefix)                                                                                  \
    BENCH_ROW(name, "8 words", "word", prefix, bench_fill_random, nullptr, BENCH_WORDS_SHORT, BENCH_WORDS_SHORT * 8u,  \
              0u),                                                                                                     \
        BENCH_ROW(name, "64 words, a section", "word", prefix, bench_fill_random, nullptr, BENCH_WORDS_SECTION,        \
                  BENCH_WORDS_SECTION * 8u, 1u),                                                                       \
        BENCH_ROW(name, "4096 words", "word", prefix, bench_fill_random, nullptr, BENCH_WORDS_LONG,                    \
                  BENCH_WORDS_LONG * 8u, 1u)

#define BENCH_VEC_ROWS(name, prefix, vector_t)                                                                         \
    BENCH_ROW(name, "8", "vector", prefix, bench_fill_floats, nullptr, BENCH_VEC_FEW,                                  \
              BENCH_VEC_FEW * sizeof(vector_t), 1u),                                                                   \
        BENCH_ROW(name, "64", "vector", prefix, bench_fill_floats, nullptr, BENCH_VEC_MANY,                            \
                  BENCH_VEC_MANY * sizeof(vector_t), 1u),                                                              \
        BENCH_ROW(name, "1024", "vector", prefix, bench_fill_floats, nullptr, BENCH_VEC_MOST,                          \
                  BENCH_VEC_MOST * sizeof(vector_t), 1u)

static const bench_row_t bench_rows[] = {
    BENCH_ROW("copy_stream_batch", "1080p frame", "byte", bench_copy, bench_fill_random, nullptr, BENCH_FRAME_BYTES,
              BENCH_FRAME_BYTES, 1u),
    BENCH_ROW("copy_stream_rows_batch", "1080p frame", "byte", bench_copy_rows, bench_fill_random, nullptr,
              BENCH_FRAME_BYTES, BENCH_FRAME_BYTES, 1u),
    BENCH_ROW("blend_over_batch", "runs, no partial alpha", "pixel", bench_over, bench_fill_hud_none_partial,
              bench_reset_from_b, BENCH_HUD_PIXELS, BENCH_HUD_PIXELS * 4u, 1u),
    BENCH_ROW("blend_over_batch", "runs, 20% partial", "pixel", bench_over, bench_fill_hud_some_partial,
              bench_reset_from_b, BENCH_HUD_PIXELS, BENCH_HUD_PIXELS * 4u, 1u),
    BENCH_ROW("blend_over_batch", "all partial", "pixel", bench_over, bench_fill_all_partial, bench_reset_from_b,
              BENCH_HUD_PIXELS, BENCH_HUD_PIXELS * 4u, 1u),
    BENCH_ROW("blend_premul_batch", "runs, no partial alpha", "pixel", bench_premul, bench_fill_hud_none_partial,
              bench_reset_from_b, BENCH_HUD_PIXELS, BENCH_HUD_PIXELS * 4u, 1u),
    BENCH_ROW("blend_premul_batch", "all partial", "pixel", bench_premul, bench_fill_all_partial, bench_reset_from_b,
              BENCH_HUD_PIXELS, BENCH_HUD_PIXELS * 4u, 1u),
    BENCH_ROW("bitmask_popcount_batch", "24 words", "word", bench_popcount, bench_fill_random, nullptr,
              BENCH_WORDS_POPCOUNT_FLOOR, sizeof(uint64_t), 1u),
    BENCH_WORD_ROWS("bitmask_popcount_batch", bench_popcount),
    BENCH_WORD_ROWS("bitmask_and_batch", bench_and),
    BENCH_WORD_ROWS("bitmask_or_batch", bench_or),
    BENCH_WORD_ROWS("bitmask_andnot_batch", bench_andnot),
    BENCH_ROW("bitmask_from_nonzero_u8_batch", "4096 values", "value", bench_nonzero_u8, bench_fill_values, nullptr,
              BENCH_SECTION_VALUES, BENCH_WORDS_SECTION * 8u, 1u),
    BENCH_ROW("bitmask_from_nonzero_u16_batch", "4096 values", "value", bench_nonzero_u16, bench_fill_values, nullptr,
              BENCH_SECTION_VALUES, BENCH_WORDS_SECTION * 8u, 1u),
    BENCH_ROW("bitmask_from_equal_u16_batch", "4096 values", "value", bench_equal_u16, bench_fill_values, nullptr,
              BENCH_SECTION_VALUES, BENCH_WORDS_SECTION * 8u, 1u),
    BENCH_ROW("noise3_batch8", "5, all tail", "point", bench_noise, bench_fill_floats, nullptr, BENCH_NOISE_TAIL,
              BENCH_NOISE_TAIL * sizeof(float32_t), 0u),
    BENCH_ROW("noise3_batch8", "256, a column layer", "point", bench_noise, bench_fill_floats, nullptr,
              BENCH_NOISE_COLUMN, BENCH_NOISE_COLUMN * sizeof(float32_t), 1u),
    BENCH_ROW("noise3_batch8", "4096, a section", "point", bench_noise, bench_fill_floats, nullptr, BENCH_NOISE_SECTION,
              BENCH_NOISE_SECTION * sizeof(float32_t), 1u),
    BENCH_VEC_ROWS("vec_batch_transform_points", bench_points, vec3_t),
    BENCH_VEC_ROWS("vec_batch_transform_directions", bench_directions, vec3_t),
    BENCH_VEC_ROWS("vec_batch_mul_vec4", bench_vec4, vec4_t),
};
enum { BENCH_ROW_COUNT = sizeof(bench_rows) / sizeof(bench_rows[0]) };

/* ---- Measuring ----------------------------------------------------------------------------------------------------- */

/* The best round of as many as fit the budget, in ns per element. The best is the one the least else interfered with. */
static float64_t bench_measure(const bench_t *bench, const bench_row_t *row, bench_run_t run, uint8_t *out) {
    uint64_t repeats = 1u;
    if (row->reset == nullptr && row->count < BENCH_ROUND_MIN_ELEMENTS) {
        repeats = BENCH_ROUND_MIN_ELEMENTS / row->count;
    }
    int64_t best_ns = INT64_MAX;
    int64_t spent_ns = 0;
    uint32_t rounds = 0;
    while (spent_ns < bench_side_budget_ns || rounds < BENCH_MIN_ROUNDS) {
        if (row->reset != nullptr) {
            row->reset(bench, out, row->count);
        }
        const int64_t start_ns = time_now_ns();
        for (uint64_t i = 0; i < repeats; i++) {
            run(bench, out, row->count);
        }
        const int64_t round_ns = time_now_ns() - start_ns;
        best_ns = round_ns < best_ns ? round_ns : best_ns;
        spent_ns += round_ns;
        rounds++;
    }
    return (float64_t)best_ns / (float64_t)(repeats * row->count);
}

/* False if this row says the kernel should go. */
static bool bench_run_row(const bench_t *bench, const bench_row_t *row) {
    row->fill(bench);
    const float64_t kernel_ns = bench_measure(bench, row, row->run_kernel, bench->borrowed_out_kernel);
    const float64_t reference_ns = bench_measure(bench, row, row->run_reference, bench->borrowed_out_reference);
    const float64_t gain = reference_ns / kernel_ns;
    const bool same = memcmp(bench->borrowed_out_kernel, bench->borrowed_out_reference, row->compare_bytes) == 0;
    const bool enough = gain > 1.0f64 || row->gated == 0u;
    printf("%-32s %-24s %9.3f %9.3f %8.2fx%s%s\n", row->kernel, row->input, (libc_double_t)kernel_ns,
           (libc_double_t)reference_ns, (libc_double_t)gain, row->gated == 0u ? " (not gated)" : "",
           same ? (enough ? "" : "  NO GAIN") : "  OUTPUT DIFFERS");
    return same && enough;
}

static bool bench_init(bench_t *bench) {
    memset(bench, 0, sizeof(*bench));
    if (!heap_reserve(4u * BENCH_BUFFER_BYTES, &bench->owned_region)) {
        return false; /* heap_reserve has said why */
    }
    bench->borrowed_in_a = bench->owned_region.owned_base;
    bench->borrowed_in_b = bench->borrowed_in_a + BENCH_BUFFER_BYTES;
    bench->borrowed_out_kernel = bench->borrowed_in_b + BENCH_BUFFER_BYTES;
    bench->borrowed_out_reference = bench->borrowed_out_kernel + BENCH_BUFFER_BYTES;
    for (uint32_t i = 0; i < 16u; i++) {
        bench->matrix.m[i] = ((float32_t)i - 7.5f32) * 0.25f32;
    }
    return true;
}

static void bench_deinit(bench_t *bench) {
    heap_release(&bench->owned_region);
}

int32_t main(void) {
    /* The clock and the caches mean more on one CPU, and the one it started on is the one it may certainly use. */
    const int32_t cpu = sched_getcpu();
    if (cpu < 0 || !thread_pin_self((uint32_t)cpu)) {
        fprintf(stderr, "bench_simd: could not pin to a CPU, so these numbers will be noisier\n");
    }
    bench_t bench;
    if (!bench_init(&bench)) {
        return 2;
    }
    printf("%-32s %-24s %9s %9s %9s\n", "kernel", "input", "ns/elem", "scalar", "gain");
    uint32_t failed = 0;
    for (uint32_t i = 0; i < BENCH_ROW_COUNT; i++) {
        if (!bench_run_row(&bench, &bench_rows[i])) {
            failed++;
        }
    }
    bench_deinit(&bench);
    if (failed != 0u) {
        printf("%u rows failed: a kernel with no gain gets removed, and one that differs is a bug.\n", failed);
        return 1;
    }
    printf("Every gated row gained, and every kernel gave its reference's bytes.\n");
    return 0;
}
