/* ctest run_types. See README.md. */
#include "harness/index.h"

#include "assert/index.h"
#include "log/index.h"
#include "machine/index.h"
#include "main/cpu_check/index.h"
#include "main/index.h"
#include "simd/cpuid/index.h"
#include "types/index.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- A forked child, for what ends its process ------------------------------------------------------------------- */

enum : uint64_t { CHILD_OUTPUT_BYTES = 1u << 20 };

typedef struct child_result_t {
    char *output;
    uint64_t output_length;
    int32_t exit_code; /* -1 if a signal ended it */
    int32_t signal;    /* 0 unless a signal ended it */
} child_result_t;
static_assert(sizeof(child_result_t) == 24);

typedef void (*child_body_t)(void);

static bool child_read_all(test_context_t *t, int32_t fd, char **out_text, uint64_t *out_length) {
    char *text = test_alloc(t, CHILD_OUTPUT_BYTES);
    uint64_t length = 0;
    while (length + 1u < CHILD_OUTPUT_BYTES) {
        const int64_t count = read(fd, text + length, CHILD_OUTPUT_BYTES - 1u - length);
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        length += (uint64_t)count;
    }
    text[length] = '\0';
    *out_text = text;
    *out_length = length;
    return true;
}

/* Runs body in a child with its stdout and stderr captured. No core file: an abort here is the expected result. */
static bool child_run(test_context_t *t, child_body_t body, child_result_t *out) {
    int32_t fds[2];
    if (pipe(fds) != 0) {
        return false;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        const struct rlimit no_core = {.rlim_cur = 0, .rlim_max = 0};
        setrlimit(RLIMIT_CORE, &no_core);
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        body();
        _exit(0);
    }
    close(fds[1]);
    const bool read_ok = child_read_all(t, fds[0], &out->output, &out->output_length);
    close(fds[0]);
    int32_t status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    out->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    out->signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    return read_ok;
}

/* ---- types ------------------------------------------------------------------------------------------------------- */

/* In C, these are different types. If they ever became the same one, this would not compile, and the printf rule would
 * have nothing left to solve. */
static_assert(_Generic((float64_t)0, float64_t: 1, libc_double_t: 2) == 1);
static_assert(_Generic((libc_double_t)0, float64_t: 1, libc_double_t: 2) == 2);
static_assert(_Generic(F32(1.0), float32_t: true, default: false));
static_assert(_Generic(F64(1.0), float64_t: true, default: false));

TEST(macros_make_the_same_value_as_the_suffix) {
    CHECK(F32(1.5) == 1.5f32);
    CHECK(F64(1.5) == 1.5f64);
    /* Not exact in binary, so it shows that F32 did not go through a double on the way, where it would round twice. */
    CHECK(F32(0.1) == 0.1f32);
    CHECK(F64(0.1) == 0.1f64);
    CHECK(F32(1e3) == 1000.0f32);
}

TEST(varargs_cast_prints_the_value) {
    char text[64];
    const float32_t single = 1.5f32;
    const float64_t twin = 2.25f64;
    snprintf(text, sizeof(text), "%.2f %.3f", (libc_double_t)single, (libc_double_t)twin);
    CHECK_STR_EQ(text, "1.50 2.250");
}

TEST(sized_types_pass_to_and_from_libm) {
    volatile float32_t single = 4.0f32;
    volatile float64_t twin = 9.0f64;
    const float32_t root_single = sqrtf(single);
    const float64_t root_twin = sqrt(twin);
    CHECK(root_single == 2.0f32);
    CHECK(root_twin == 3.0f64);
}

/* ---- assert ------------------------------------------------------------------------------------------------------ */

static uint32_t assert_counter;

static bool assert_count_and_pass(void) {
    assert_counter++;
    return true;
}

TEST(assert_always_evaluates_once_and_passes) {
    assert_counter = 0;
    ASSERT_ALWAYS(assert_count_and_pass());
    CHECK_EQ_U64(assert_counter, 1u);
}

static void child_assert_always_fails(void) {
    const volatile uint32_t two = 2;
    ASSERT_ALWAYS(two == 3u);
}

TEST(assert_always_aborts_and_says_where) {
    child_result_t result;
    REQUIRE(child_run(t, child_assert_always_fails, &result));
    CHECK_EQ_I64(result.signal, SIGABRT);
    CHECK_CONTAINS(result.output, "assertion failed");
    CHECK_CONTAINS(result.output, "tests/run/types/index.c");
    CHECK_CONTAINS(result.output, "child_assert_always_fails");
    CHECK_CONTAINS(result.output, "two == 3u");
}

static void child_assert_fails(void) {
    const volatile uint32_t two = 2;
    ASSERT(two == 3u);
}

TEST(assert_follows_ndebug) {
    child_result_t result;
    REQUIRE(child_run(t, child_assert_fails, &result));
#ifdef NDEBUG
    CHECK_EQ_I64(result.signal, 0);
    CHECK_EQ_I64(result.exit_code, 0);
    assert_counter = 0;
    ASSERT(assert_count_and_pass());
    CHECK_EQ_U64(assert_counter, 0u);
#else
    CHECK_EQ_I64(result.signal, SIGABRT);
    CHECK_CONTAINS(result.output, "two == 3u");
    assert_counter = 0;
    ASSERT(assert_count_and_pass());
    CHECK_EQ_U64(assert_counter, 1u);
#endif
}

static void child_assert_long_expression(void) {
    static char expression[4000];
    memset(expression, 'x', sizeof(expression) - 1u);
    assert_fail("file.c", 7, "function", expression);
}

TEST(assert_cuts_a_long_line_and_still_ends_it) {
    child_result_t result;
    REQUIRE(child_run(t, child_assert_long_expression, &result));
    CHECK_EQ_I64(result.signal, SIGABRT);
    CHECK_EQ_U64(result.output_length, 1023u);
    REQUIRE(result.output_length > 0);
    CHECK_EQ_U64((uint64_t)result.output[result.output_length - 1u], (uint64_t)'\n');
}

/* ---- log --------------------------------------------------------------------------------------------------------- */

/* Sends log lines to a pipe and hands back what arrived. Restores what it changed. */
typedef struct log_capture_t {
    int32_t fds[2];
    int32_t previous_fd;
    log_level_t previous_level;
    uint8_t _pad0[3];
} log_capture_t;
static_assert(sizeof(log_capture_t) == 16);

static bool log_capture_begin(log_capture_t *capture, log_level_t level) {
    if (pipe(capture->fds) != 0) {
        return false;
    }
    capture->previous_fd = log_set_fd(capture->fds[1]);
    capture->previous_level = log_get_level();
    log_set_level(level);
    return true;
}

static void log_capture_end(test_context_t *t, log_capture_t *capture, char **out_text) {
    log_set_fd(capture->previous_fd);
    log_set_level(capture->previous_level);
    close(capture->fds[1]);
    uint64_t length = 0;
    CHECK(child_read_all(t, capture->fds[0], out_text, &length));
    close(capture->fds[0]);
}

TEST(log_writes_level_place_and_message) {
    log_capture_t capture;
    REQUIRE(log_capture_begin(&capture, LOG_LEVEL_DEBUG));
    log_write(LOG_LEVEL_WARN, "/somewhere/run/src/world/section/index.c", 12, "%s %" PRIu32, "count", (uint32_t)3);
    char *text = nullptr;
    log_capture_end(t, &capture, &text);
    CHECK_STR_EQ(text, "[warn] world/section/index.c:12: count 3\n");
}

TEST(log_macros_pass_a_float_through_the_cast) {
    log_capture_t capture;
    REQUIRE(log_capture_begin(&capture, LOG_LEVEL_DEBUG));
    const float64_t elapsed = 1.25f64;
    LOG_INFO("took %.2f ms", (libc_double_t)elapsed);
    char *text = nullptr;
    log_capture_end(t, &capture, &text);
    CHECK_CONTAINS(text, "[info] ");
    CHECK_CONTAINS(text, "took 1.25 ms\n");
}

TEST(log_drops_lines_below_the_level) {
    log_capture_t capture;
    REQUIRE(log_capture_begin(&capture, LOG_LEVEL_WARN));
    LOG_DEBUG("dropped debug");
    LOG_INFO("dropped info");
    LOG_WARN("kept warn");
    LOG_ERROR("kept error");
    char *text = nullptr;
    log_capture_end(t, &capture, &text);
    CHECK(strstr(text, "dropped") == nullptr);
    CHECK_CONTAINS(text, "kept warn");
    CHECK_CONTAINS(text, "[error] ");
    CHECK_CONTAINS(text, "kept error");
}

TEST(log_keeps_errno) {
    log_capture_t capture;
    REQUIRE(log_capture_begin(&capture, LOG_LEVEL_DEBUG));
    errno = ENOENT;
    LOG_INFO("a line");
    CHECK_EQ_I64(errno, ENOENT);
    char *text = nullptr;
    log_capture_end(t, &capture, &text);
}

[[gnu::format(printf, 4, 5)]] static uint64_t log_test_format(char *out, uint64_t capacity, const char *file,
                                                              const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const uint64_t length = log_format_line(out, capacity, LOG_LEVEL_INFO, file, 5, format, arguments);
    va_end(arguments);
    return length;
}

TEST(log_cuts_a_long_message_and_marks_it) {
    enum { CAPACITY = 64, CANARY = 8 };
    char buffer[CAPACITY + CANARY];
    memset(buffer, '#', sizeof(buffer));
    const uint64_t length =
        log_test_format(buffer, CAPACITY, "a/b.c", "%s",
                        "a message far longer than the sixty four bytes this buffer has room for in it");
    CHECK_EQ_U64(length, CAPACITY - 2u);
    CHECK_EQ_U64(strlen(buffer), length);
    CHECK_EQ_I64(buffer[length - 4u], '.');
    CHECK_EQ_I64(buffer[length - 1u], '\n');
    for (uint32_t i = 0; i < CANARY; i++) {
        CHECK_EQ_I64(buffer[CAPACITY + i], '#');
    }
}

TEST(log_cuts_a_long_path_and_stays_in_bounds) {
    enum { CAPACITY = 32, CANARY = 8 };
    char buffer[CAPACITY + CANARY];
    memset(buffer, '#', sizeof(buffer));
    const uint64_t length =
        log_test_format(buffer, CAPACITY, "a/path/longer/than/the/whole/buffer/index.c", "never seen");
    CHECK_EQ_U64(strlen(buffer), length);
    CHECK(strstr(buffer, "never seen") == nullptr);
    CHECK_EQ_I64(buffer[length - 1u], '\n');
    for (uint32_t i = 0; i < CANARY; i++) {
        CHECK_EQ_I64(buffer[CAPACITY + i], '#');
    }

    const uint64_t fits = log_test_format(buffer, CAPACITY, "a/b.c", "fits");
    CHECK_EQ_U64(strlen(buffer), fits);
    CHECK_STR_EQ(buffer, "[info] a/b.c:5: fits\n");
}

TEST(log_refuses_a_buffer_too_small_to_hold_a_line) {
    char buffer[8] = "intact";
    CHECK_EQ_U64(log_test_format(buffer, sizeof(buffer), "a/b.c", "x"), 0u);
    CHECK_STR_EQ(buffer, "intact");
}

enum { LOG_WRITER_COUNT = 4, LOG_LINES_PER_WRITER = 300 };

static int32_t log_writer_id;

static void log_writer_body(void) {
    for (uint32_t i = 0; i < LOG_LINES_PER_WRITER; i++) {
        LOG_INFO("writer=%d line=%" PRIu32 " end", log_writer_id, i);
    }
}

/* What the header promises: a whole line is one write, so concurrent writers cannot mix. Processes and not threads,
 * because thread/ is a later stage. The kernel's rule for a pipe is the same. */
TEST(log_lines_from_concurrent_writers_stay_whole) {
    int32_t fds[2];
    REQUIRE(pipe(fds) == 0);
    pid_t pids[LOG_WRITER_COUNT];
    for (int32_t w = 0; w < LOG_WRITER_COUNT; w++) {
        pids[w] = fork();
        REQUIRE(pids[w] >= 0);
        if (pids[w] == 0) {
            close(fds[0]);
            log_set_fd(fds[1]);
            log_set_level(LOG_LEVEL_DEBUG);
            log_writer_id = w;
            log_writer_body();
            _exit(0);
        }
    }
    close(fds[1]);
    char *text = nullptr;
    uint64_t length = 0;
    CHECK(child_read_all(t, fds[0], &text, &length));
    close(fds[0]);
    for (int32_t w = 0; w < LOG_WRITER_COUNT; w++) {
        int32_t status = 0;
        CHECK(waitpid(pids[w], &status, 0) == pids[w]);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    uint32_t seen[LOG_WRITER_COUNT] = {0};
    uint32_t bad_lines = 0;
    char *line = text;
    while (*line != '\0') {
        char *end = strchr(line, '\n');
        REQUIRE(end != nullptr);
        *end = '\0';
        const char *fields = strstr(line, "writer=");
        int32_t writer = -1;
        uint32_t number = 0;
        char tail[8] = {0};
        const bool parsed = fields != nullptr && line[0] == '[' &&
                            sscanf(fields, "writer=%d line=%" SCNu32 " %7s", &writer, &number, tail) == 3 &&
                            strcmp(tail, "end") == 0 && writer >= 0 && writer < LOG_WRITER_COUNT;
        /* Order within one writer holds too, since each process writes in sequence. */
        if (parsed && seen[writer] == number) {
            seen[writer]++;
        } else {
            bad_lines++;
        }
        line = end + 1;
    }
    CHECK_EQ_U64(bad_lines, 0u);
    for (int32_t w = 0; w < LOG_WRITER_COUNT; w++) {
        CHECK_EQ_U64(seen[w], LOG_LINES_PER_WRITER);
    }
}

/* ---- asm/src/cpuid ----------------------------------------------------------------------------------------------- */

/* Reads the first "key : value" line of /proc/cpuinfo with this key. The flags line is long. */
static bool proc_cpuinfo_value(const char *key, char *out, uint64_t capacity) {
    FILE *file = fopen("/proc/cpuinfo", "r");
    if (file == nullptr) {
        return false;
    }
    bool found = false;
    const uint64_t key_length = strlen(key);
    while (!found && fgets(out, (int32_t)capacity, file) != nullptr) {
        found = strncmp(out, key, key_length) == 0 && (out[key_length] == ' ' || out[key_length] == '\t');
    }
    fclose(file);
    if (found) {
        const char *value = strchr(out, ':');
        if (value == nullptr) {
            return false;
        }
        value += value[1] == ' ' ? 2 : 1;
        memmove(out, value, strlen(value) + 1u);
    }
    return found;
}

static bool flags_has(const char *flags, const char *flag) {
    const uint64_t length = strlen(flag);
    const char *at = strstr(flags, flag);
    while (at != nullptr) {
        const bool starts = at == flags || at[-1] == ' ';
        const bool ends = at[length] == ' ' || at[length] == '\n' || at[length] == '\0';
        if (starts && ends) {
            return true;
        }
        at = strstr(at + 1, flag);
    }
    return false;
}

TEST(cpuid_vendor_matches_the_kernel) {
    static const uint32_t leaf[1] = {0u};
    static const uint32_t subleaf[1] = {0u};
    uint32_t regs[4] = {0};
    cpuid_read_batch(leaf, subleaf, 1u, regs);
    char vendor[13] = {0};
    memcpy(vendor, &regs[1], 4u); /* ebx, edx, ecx: the order the vendor string is defined in */
    memcpy(vendor + 4, &regs[3], 4u);
    memcpy(vendor + 8, &regs[2], 4u);
    char expected[256];
    REQUIRE(proc_cpuinfo_value("vendor_id", expected, sizeof(expected)));
    expected[strcspn(expected, "\n")] = '\0';
    CHECK_STR_EQ(vendor, expected);
    CHECK(regs[0] >= 7u);
}

TEST(cpuid_batch_matches_single_calls_and_keeps_its_order) {
    static const uint32_t leaves[3] = {7u, 1u, 0u};
    static const uint32_t subleaves[3] = {0u, 0u, 0u};
    uint32_t batch[12];
    /* A count of zero writes nothing. */
    uint32_t sentinel[4] = {0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu};
    cpuid_read_batch(leaves, subleaves, 0u, sentinel);
    CHECK_EQ_U64(sentinel[0], 0xAAAAAAAAu);

    cpuid_read_batch(leaves, subleaves, 3u, batch);
    uint32_t single[4];
    /* Leaf 1's ebx holds the running core's ID and differs between cores, so compare only what cannot: ecx. */
    cpuid_read_batch(&leaves[1], &subleaves[1], 1u, single);
    CHECK_EQ_U64(batch[4 + 2], single[2]);
    cpuid_read_batch(&leaves[0], &subleaves[0], 1u, single);
    CHECK_EQ_U64(batch[0 + 1], single[1]);
    cpuid_read_batch(&leaves[2], &subleaves[2], 1u, single);
    CHECK_MEM_EQ(&batch[8], single, sizeof(single));
}

TEST(xgetbv_batch_reads_each_register_asked_for) {
    cpu_check_raw_t raw;
    cpu_check_read_raw(&raw);
    REQUIRE(raw.xcr0 != 0);
    static const uint32_t registers[2] = {0u, 0u};
    uint64_t out[2] = {0, 0};
    cpuid_xgetbv_batch(registers, 2u, out);
    CHECK_EQ_U64(out[0], raw.xcr0);
    CHECK_EQ_U64(out[1], raw.xcr0);
    /* x87 is architecturally always set, in bit 0. */
    CHECK_EQ_U64(out[0] & 1u, 1u);
}

/* ---- main/cpu_check ---------------------------------------------------------------------------------------------- */

/* A CPU and OS with everything on, so a test can turn one thing off. */
static cpu_check_raw_t raw_everything(void) {
    return (cpu_check_raw_t){
        .xcr0 = 0xE7u,
        .max_leaf = 0x16u,
        .max_extended_leaf = 0x80000008u,
        .leaf1_ecx = (1u << 12) | (1u << 20) | (1u << 23) | (1u << 27) | (1u << 28) | (1u << 29),
        .leaf7_ebx = (1u << 3) | (1u << 5) | (1u << 8) | (1u << 16),
        .extended1_ecx = 1u << 5,
        ._pad0 = {0},
    };
}

enum : uint32_t { CPU_ALL = (1u << CPU_FEATURE_COUNT) - 1u };

/* ---- The early guard: asm/src/cpuid/ ------------------------------------------------------------------------------- */

/* The C check also counts what -march lets GCC use by itself, and the guard knows only the yaml, so a yaml that says less
 * than its arch_flag would leave the guard blind to what the code does use. */
TEST(the_guard_requires_what_the_build_requires) {
    CHECK_EQ_U64(cpuid_guard_required[0], cpu_check_required());
}

extern void (*const __preinit_array_start[])(void);
extern void (*const __preinit_array_end[])(void);

TEST(the_guard_runs_before_anything_compiled_with_march) {
    bool found = false;
    for (void (*const *entry)(void) = __preinit_array_start; entry != __preinit_array_end; entry++) {
        found = found || *entry == cpuid_guard_preinit;
    }
    CHECK(found);
}

enum { CHILD_GOT_PAST_THE_GUARD = 77 };

static void child_guard_passes_what_this_cpu_has(void) {
    const uint32_t required[2] = {cpu_check_required(), cpu_check_detect()};
    cpuid_guard_batch(required, 2u);
    cpuid_guard_batch(required, 0u);
    _exit(CHILD_GOT_PAST_THE_GUARD);
}

static void child_guard_asked_for_avx512(void) {
    const uint32_t required[2] = {CPU_FEATURE_SSE42, CPU_FEATURE_AVX512};
    cpuid_guard_batch(required, 2u);
    _exit(CHILD_GOT_PAST_THE_GUARD);
}

static void child_guard_stops_at_the_count(void) {
    const uint32_t required[2] = {CPU_FEATURE_SSE42, CPU_FEATURE_AVX512};
    cpuid_guard_batch(required, 1u);
    _exit(CHILD_GOT_PAST_THE_GUARD);
}

TEST(the_guard_passes_a_cpu_with_what_is_asked) {
    child_result_t result;
    REQUIRE(child_run(t, child_guard_passes_what_this_cpu_has, &result));
    CHECK_EQ_I64(result.exit_code, CHILD_GOT_PAST_THE_GUARD);
    REQUIRE(child_run(t, child_guard_stops_at_the_count, &result));
    CHECK_EQ_I64(result.exit_code, CHILD_GOT_PAST_THE_GUARD);
}

TEST(the_guard_refuses_a_cpu_without_a_feature_and_says_which) {
    if ((cpu_check_detect() & CPU_FEATURE_AVX512) != 0) {
        printf("  this CPU has AVX-512, the one feature it could be asked for and lack\n");
        return;
    }
    child_result_t result;
    REQUIRE(child_run(t, child_guard_asked_for_avx512, &result));
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_CPU_MISMATCH);
    CHECK_CONTAINS(result.output, "Missing: avx512.");
    CHECK_CONTAINS(result.output, "-march=" MC_CPU_ARCH_FLAG);
    CHECK_CONTAINS(result.output, "rebuild.\n");
}

TEST(decode_reads_every_bit) {
    cpu_check_raw_t raw = raw_everything();
    CHECK_EQ_U64(cpu_check_decode(&raw), CPU_ALL);

    typedef struct bit_case_t {
        uint32_t *field;
        uint32_t bit;
        uint32_t feature;
    } bit_case_t;
    static_assert(sizeof(bit_case_t) == 16);
    const bit_case_t cases[] = {
        {&raw.leaf1_ecx, 1u << 12, CPU_FEATURE_FMA3},     {&raw.leaf1_ecx, 1u << 20, CPU_FEATURE_SSE42},
        {&raw.leaf1_ecx, 1u << 23, CPU_FEATURE_POPCNT},   {&raw.leaf1_ecx, 1u << 29, CPU_FEATURE_F16C},
        {&raw.leaf7_ebx, 1u << 3, CPU_FEATURE_BMI1},      {&raw.leaf7_ebx, 1u << 5, CPU_FEATURE_AVX2},
        {&raw.leaf7_ebx, 1u << 8, CPU_FEATURE_BMI2},      {&raw.leaf7_ebx, 1u << 16, CPU_FEATURE_AVX512},
        {&raw.extended1_ecx, 1u << 5, CPU_FEATURE_LZCNT},
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        *cases[i].field &= ~cases[i].bit;
        CHECK_EQ_U64(cpu_check_decode(&raw), CPU_ALL & ~cases[i].feature);
        *cases[i].field |= cases[i].bit;
    }
    raw.leaf1_ecx &= ~(1u << 28);
    CHECK_EQ_U64(cpu_check_decode(&raw) & CPU_FEATURE_AVX, 0u);
}

TEST(decode_wants_the_os_to_save_ymm) {
    const uint32_t needs_ymm =
        CPU_FEATURE_AVX | CPU_FEATURE_AVX2 | CPU_FEATURE_FMA3 | CPU_FEATURE_F16C | CPU_FEATURE_AVX512;

    cpu_check_raw_t no_osxsave = raw_everything();
    no_osxsave.leaf1_ecx &= ~(1u << 27);
    CHECK_EQ_U64(cpu_check_decode(&no_osxsave), CPU_ALL & ~needs_ymm);

    cpu_check_raw_t no_ymm = raw_everything();
    no_ymm.xcr0 = 0x03u;
    CHECK_EQ_U64(cpu_check_decode(&no_ymm), CPU_ALL & ~needs_ymm);

    cpu_check_raw_t no_zmm = raw_everything();
    no_zmm.xcr0 = 0x07u;
    CHECK_EQ_U64(cpu_check_decode(&no_zmm), CPU_ALL & ~(uint32_t)CPU_FEATURE_AVX512);

    /* One of the three ZMM bits is not enough. */
    cpu_check_raw_t part_zmm = raw_everything();
    part_zmm.xcr0 = 0x67u;
    CHECK_EQ_U64(cpu_check_decode(&part_zmm), CPU_ALL & ~(uint32_t)CPU_FEATURE_AVX512);
}

TEST(decode_does_not_trust_a_leaf_past_the_maximum) {
    cpu_check_raw_t old_cpu = raw_everything();
    old_cpu.max_leaf = 6u;
    const uint32_t from_leaf7 = CPU_FEATURE_BMI1 | CPU_FEATURE_BMI2 | CPU_FEATURE_AVX2 | CPU_FEATURE_AVX512;
    CHECK_EQ_U64(cpu_check_decode(&old_cpu), CPU_ALL & ~from_leaf7);

    cpu_check_raw_t no_extended = raw_everything();
    no_extended.max_extended_leaf = 0x80000000u;
    CHECK_EQ_U64(cpu_check_decode(&no_extended), CPU_ALL & ~(uint32_t)CPU_FEATURE_LZCNT);

    cpu_check_raw_t nothing = raw_everything();
    nothing.max_leaf = 0u;
    nothing.max_extended_leaf = 0u;
    CHECK_EQ_U64(cpu_check_decode(&nothing), 0u);
}

/* The reference is the kernel's own reading of the same CPU, in code that is not ours. */
TEST(detect_matches_proc_cpuinfo_on_this_machine) {
    typedef struct flag_case_t {
        const char *flag;
        uint32_t feature;
        uint8_t _pad0[4];
    } flag_case_t;
    static_assert(sizeof(flag_case_t) == 16);
    static const flag_case_t cases[CPU_FEATURE_COUNT] = {
        {"sse4_2", CPU_FEATURE_SSE42, {0}},   {"avx", CPU_FEATURE_AVX, {0}},   {"avx2", CPU_FEATURE_AVX2, {0}},
        {"fma", CPU_FEATURE_FMA3, {0}},       {"bmi1", CPU_FEATURE_BMI1, {0}}, {"bmi2", CPU_FEATURE_BMI2, {0}},
        {"popcnt", CPU_FEATURE_POPCNT, {0}},  {"abm", CPU_FEATURE_LZCNT, {0}}, {"f16c", CPU_FEATURE_F16C, {0}},
        {"avx512f", CPU_FEATURE_AVX512, {0}},
    };
    char *flags = test_alloc(t, 16384u);
    REQUIRE(proc_cpuinfo_value("flags", flags, 16384u));
    const uint32_t detected = cpu_check_detect();
    for (uint32_t i = 0; i < CPU_FEATURE_COUNT; i++) {
        const bool in_kernel = flags_has(flags, cases[i].flag);
        const bool in_ours = (detected & cases[i].feature) != 0;
        if (in_kernel != in_ours) {
            fprintf(stderr, "feature %s: /proc/cpuinfo says %d, CPUID says %d\n", cases[i].flag, in_kernel, in_ours);
        }
        CHECK(in_kernel == in_ours);
    }
}

/* A true in the yaml is required. A false is required anyway if -march lets GCC use the feature by itself. */
TEST(required_covers_the_yaml_and_the_compiler) {
    const uint32_t required = cpu_check_required();
    CHECK(!MC_CPU_HAS_SSE42 || (required & CPU_FEATURE_SSE42) != 0);
    CHECK(!MC_CPU_HAS_AVX || (required & CPU_FEATURE_AVX) != 0);
    CHECK(!MC_CPU_HAS_AVX2 || (required & CPU_FEATURE_AVX2) != 0);
    CHECK(!MC_CPU_HAS_FMA3 || (required & CPU_FEATURE_FMA3) != 0);
    CHECK(!MC_CPU_HAS_BMI1 || (required & CPU_FEATURE_BMI1) != 0);
    CHECK(!MC_CPU_HAS_BMI2 || (required & CPU_FEATURE_BMI2) != 0);
    CHECK(!MC_CPU_HAS_POPCNT || (required & CPU_FEATURE_POPCNT) != 0);
    CHECK(!MC_CPU_HAS_LZCNT || (required & CPU_FEATURE_LZCNT) != 0);
    CHECK(!MC_CPU_HAS_F16C || (required & CPU_FEATURE_F16C) != 0);
    CHECK(!MC_CPU_HAS_AVX512 || (required & CPU_FEATURE_AVX512) != 0);
#ifdef __AVX2__
    CHECK((required & CPU_FEATURE_AVX2) != 0);
#endif
#ifdef __BMI2__
    CHECK((required & CPU_FEATURE_BMI2) != 0);
#endif
#if !MC_CPU_HAS_AVX512 && !defined(__AVX512F__)
    CHECK((required & CPU_FEATURE_AVX512) == 0);
#endif
}

TEST(feature_names_match_the_yaml_keys) {
    FILE *file = fopen("config/machine.yaml", "r");
    REQUIRE(file != nullptr);
    char line[512];
    const char *simd = nullptr;
    while (simd == nullptr && fgets(line, sizeof(line), file) != nullptr) {
        simd = strstr(line, "simd:");
    }
    fclose(file);
    REQUIRE(simd != nullptr);
    for (uint32_t i = 0; i < CPU_FEATURE_COUNT; i++) {
        const uint32_t feature = 1u << i;
        const char *name = cpu_check_feature_name(feature);
        REQUIRE(name != nullptr);
        char key[32];
        snprintf(key, sizeof(key), " %s:", name);
        CHECK_CONTAINS(simd, key);
        uint32_t back = 0;
        CHECK(cpu_check_feature_from_name(name, &back));
        CHECK_EQ_U64(back, feature);
    }
    uint32_t unused = 0;
    CHECK(!cpu_check_feature_from_name("sse3", &unused));
    CHECK(!cpu_check_feature_from_name("", &unused));
    CHECK(cpu_check_feature_name(0u) == nullptr);
    CHECK(cpu_check_feature_name(CPU_FEATURE_AVX | CPU_FEATURE_AVX2) == nullptr);
}

TEST(names_lists_a_mask_and_stays_in_bounds) {
    char text[128];
    CHECK_EQ_U64(cpu_check_names(0u, text, sizeof(text)), 4u);
    CHECK_STR_EQ(text, "none");
    cpu_check_names(CPU_FEATURE_AVX | CPU_FEATURE_LZCNT, text, sizeof(text));
    CHECK_STR_EQ(text, "avx lzcnt");
    cpu_check_names(CPU_ALL, text, sizeof(text));
    CHECK_STR_EQ(text, "sse42 avx avx2 fma3 bmi1 bmi2 popcnt lzcnt f16c avx512");

    enum { SMALL = 7, CANARY = 8 };
    char small[SMALL + CANARY];
    memset(small, '#', sizeof(small));
    const uint64_t length = cpu_check_names(CPU_ALL, small, SMALL);
    CHECK_EQ_U64(length, SMALL - 1u);
    CHECK_STR_EQ(small, "sse42 ");
    for (uint32_t i = 0; i < CANARY; i++) {
        CHECK_EQ_I64(small[SMALL + i], '#');
    }
}

TEST(message_names_what_is_missing_and_what_to_change) {
    char text[512];
    const uint64_t length = cpu_check_message(CPU_FEATURE_AVX2 | CPU_FEATURE_BMI2, text, sizeof(text));
    CHECK_EQ_U64(length, strlen(text));
    CHECK_CONTAINS(text, "Missing: avx2 bmi2.");
    CHECK_CONTAINS(text, "config/machine.yaml");
    CHECK_CONTAINS(text, "-march=" MC_CPU_ARCH_FLAG);

    char small[16];
    const uint64_t cut = cpu_check_message(CPU_FEATURE_AVX2, small, sizeof(small));
    CHECK_EQ_U64(cut, sizeof(small) - 1u);
    CHECK_EQ_U64(strlen(small), cut);
}

/* ---- The test's command line --------------------------------------------------------------------------------------- */

/* The value after --name, or nullptr. */
static char *arg_value(test_context_t *t, const char *name) {
    for (int32_t i = 0; i + 1 < t->arg_count; i++) {
        if (strcmp(t->borrowed_args[i], name) == 0) {
            return t->borrowed_args[i + 1];
        }
    }
    return nullptr;
}

/* The arguments between --name and the next flag group, as first index and count. */
static bool arg_group(test_context_t *t, const char *name, const char *next, int32_t *out_first, int32_t *out_count) {
    for (int32_t i = 0; i < t->arg_count; i++) {
        if (strcmp(t->borrowed_args[i], name) == 0) {
            int32_t end = i + 1;
            while (end < t->arg_count && (next == nullptr || strcmp(t->borrowed_args[end], next) != 0)) {
                end++;
            }
            *out_first = i + 1;
            *out_count = end - i - 1;
            return true;
        }
    }
    return false;
}

/* ---- The compiler's answer: the printf rule in plans/CODING_STANDARD.md section 2 --------------------------------- */

enum { COMPILE_MAX_ARGV = 128 };

static void compile_c_fixture(test_context_t *t, const char *name, test_process_t *out) {
    int32_t first = 0;
    int32_t count = 0;
    char *cc = arg_value(t, "--cc");
    char *fixtures = arg_value(t, "--fixtures");
    char *include = arg_value(t, "--include");
    const bool have_flags = arg_group(t, "--c-flags", "--cuda-flags", &first, &count);
    *out = (test_process_t){.output = test_alloc(t, 1u)};
    if (!CHECK(cc != nullptr && fixtures != nullptr && include != nullptr && have_flags)) {
        return;
    }
    if (!CHECK(count + 8 < COMPILE_MAX_ARGV)) {
        return;
    }
    char *path = test_alloc(t, 4096u);
    snprintf(path, 4096u, "%s/%s", fixtures, name);
    char *include_flag = test_alloc(t, 4096u);
    snprintf(include_flag, 4096u, "-I%s", include);
    static char std_flag[] = "-std=c23";
    static char syntax_flag[] = "-fsyntax-only";
    char *argv[COMPILE_MAX_ARGV];
    int32_t n = 0;
    argv[n++] = cc;
    argv[n++] = std_flag;
    argv[n++] = syntax_flag;
    argv[n++] = include_flag;
    for (int32_t i = 0; i < count; i++) {
        argv[n++] = t->borrowed_args[first + i];
    }
    argv[n++] = path;
    argv[n] = nullptr;
    test_process_run(t, argv, 60000u, out);
    CHECK(out->started != 0);
    CHECK(out->timed_out == 0);
}

TEST(printf_rejects_a_bare_float64) {
    test_process_t result;
    compile_c_fixture(t, "printf_float64_bare.c", &result);
    CHECK(result.exit_code != 0);
    CHECK_CONTAINS(result.output, "-Werror=format=");
}

TEST(printf_rejects_a_bare_float32) {
    test_process_t result;
    compile_c_fixture(t, "printf_float32_bare.c", &result);
    CHECK(result.exit_code != 0);
    CHECK_CONTAINS(result.output, "-Werror=format=");
}

TEST(printf_accepts_the_cast) {
    test_process_t result;
    compile_c_fixture(t, "printf_cast.c", &result);
    CHECK_EQ_I64(result.exit_code, 0);
}

TEST(log_macro_rejects_a_bare_float64) {
    test_process_t result;
    compile_c_fixture(t, "log_float64_bare.c", &result);
    CHECK(result.exit_code != 0);
    CHECK_CONTAINS(result.output, "-Werror=format=");
}

TEST(log_macro_rejects_a_wrong_type) {
    test_process_t result;
    compile_c_fixture(t, "log_wrong_type.c", &result);
    CHECK(result.exit_code != 0);
    CHECK_CONTAINS(result.output, "-Werror=format=");
}

TEST(log_macro_accepts_the_cast) {
    test_process_t result;
    compile_c_fixture(t, "log_cast.c", &result);
    CHECK_EQ_I64(result.exit_code, 0);
}

TEST(types_header_compiles_on_both_sides_under_nvcc) {
    int32_t first = 0;
    int32_t count = 0;
    char *nvcc = arg_value(t, "--nvcc");
    char *arch = arg_value(t, "--nvcc-arch");
    char *fixtures = arg_value(t, "--fixtures");
    char *tmp = arg_value(t, "--tmp");
    char *include = arg_value(t, "--include");
    REQUIRE(nvcc != nullptr && arch != nullptr && fixtures != nullptr && tmp != nullptr && include != nullptr);
    REQUIRE(arg_group(t, "--cuda-flags", nullptr, &first, &count));
    REQUIRE(count + 12 < COMPILE_MAX_ARGV);

    char *path = test_alloc(t, 4096u);
    snprintf(path, 4096u, "%s/both_sides.cu", fixtures);
    char *object = test_alloc(t, 4096u);
    snprintf(object, 4096u, "%s/both_sides.o", tmp);
    char *include_flag = test_alloc(t, 4096u);
    snprintf(include_flag, 4096u, "-I%s", include);
    char *arch_flag = test_alloc(t, 64u);
    snprintf(arch_flag, 64u, "-arch=%s", arch);
    static char std_flag[] = "-std=c++20";
    static char compile_flag[] = "-c";
    static char object_flag[] = "-o";
    char *argv[COMPILE_MAX_ARGV];
    int32_t n = 0;
    argv[n++] = nvcc;
    argv[n++] = std_flag;
    argv[n++] = arch_flag;
    argv[n++] = include_flag;
    for (int32_t i = 0; i < count; i++) {
        argv[n++] = t->borrowed_args[first + i];
    }
    argv[n++] = compile_flag;
    argv[n++] = path;
    argv[n++] = object_flag;
    argv[n++] = object;
    argv[n] = nullptr;
    test_process_t result;
    test_process_run(t, argv, 120000u, &result);
    REQUIRE(result.started != 0);
    if (result.exit_code != 0) {
        fprintf(stderr, "%s\n", result.output);
    }
    CHECK_EQ_I64(result.exit_code, 0);
    unlink(object);
}

/* ---- The built program: --report and the failure paths never open a display ---------------------------------------- */

static void game_run(test_context_t *t, char *first_arg, char *second_arg, test_process_t *out) {
    char *game = arg_value(t, "--game");
    *out = (test_process_t){.output = test_alloc(t, 1u)};
    if (!CHECK(game != nullptr)) {
        return;
    }
    char *argv[4] = {game, first_arg, second_arg, nullptr};
    test_process_run(t, argv, 30000u, out);
    CHECK(out->started != 0);
    CHECK(out->timed_out == 0);
}

static char game_report_flag[] = "--report";
static char game_pretend_flag[] = "--pretend-missing-cpu";

/* The default run starts the real core runtime: the counts show that the workers did run jobs, and that the zero at exit
 * follows a real reservation and release, and is not a zero because nothing happened. */
TEST(game_runs_the_core_runtime_and_ends_with_nothing_live) {
    test_process_t result;
    game_run(t, nullptr, nullptr, &result);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_OK);
    char expected[64];
    snprintf(expected, sizeof(expected), "%d workers ran %d jobs", MC_THREADS_WORKERS, MC_THREADS_WORKERS);
    CHECK_CONTAINS(result.output, expected);
    CHECK_CONTAINS(result.output, "no game systems exist yet");
    CHECK_CONTAINS(result.output, "heap at exit: 6 reserved, 6 released, 0 bytes live");
}

TEST(game_exits_with_its_own_status_on_a_leak) {
    static char pretend_leak[] = "--pretend-leak";
    test_process_t result;
    game_run(t, pretend_leak, nullptr, &result);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_LEAK);
    CHECK_CONTAINS(result.output, "heap: leak at exit: 6 regions");
}

TEST(game_reports_its_compiled_defines) {
    test_process_t result;
    game_run(t, game_report_flag, nullptr, &result);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_OK);
    char expected[64];
    snprintf(expected, sizeof(expected), "MC_THREADS_WORKERS=%d\n", MC_THREADS_WORKERS);
    CHECK_CONTAINS(result.output, expected);
    CHECK_CONTAINS(result.output, "MC_CPU_ARCH_FLAG=" MC_CPU_ARCH_FLAG "\n");
    CHECK_CONTAINS(result.output, "cpu missing: none\n");
    CHECK(strstr(result.output, "core runtime") == nullptr); /* --report starts no threads */
}

TEST(game_exits_with_a_message_on_a_cpu_mismatch) {
    static char avx2[] = "avx2";
    test_process_t result;
    game_run(t, game_pretend_flag, avx2, &result);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_CPU_MISMATCH);
    CHECK_CONTAINS(result.output, "[error] ");
    CHECK_CONTAINS(result.output, "Missing: avx2.");
    CHECK_CONTAINS(result.output, "config/machine.yaml");
    CHECK(strstr(result.output, "no game systems exist yet") == nullptr);
}

TEST(game_ignores_a_missing_feature_the_build_does_not_use) {
#if MC_CPU_HAS_AVX512
    CHECK(false);
#else
    static char avx512[] = "avx512";
    test_process_t result;
    game_run(t, game_pretend_flag, avx512, &result);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_OK);
#endif
}

TEST(game_refuses_a_bad_command_line) {
    static char unknown[] = "--no-such-option";
    static char not_a_key[] = "mmx";
    test_process_t result;
    game_run(t, unknown, nullptr, &result);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(result.output, "--no-such-option");
    CHECK_CONTAINS(result.output, "usage:");
    game_run(t, game_pretend_flag, not_a_key, &result);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(result.output, "mmx");
    game_run(t, game_pretend_flag, nullptr, &result);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_USAGE);
}

TEST_MAIN()
