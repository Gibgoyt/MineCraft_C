/*
 * ctest compile_machine_config. See README.md. Arguments come from test.cmake:
 *   tool report_tool machine_yaml generated_dir work_dir gcc nasm nvcc cmake
 */
#include "harness/index.h"
#include "machine/index.h"
#include "yaml/index.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define MCT_FILE_BYTES (1u << 20)
#define MCT_PATH_BYTES 1024u
#define MCT_MAX_ARGS 32u
#define MCT_TIMEOUT_TOOL "20"
#define MCT_TIMEOUT_COMPILER "120"
#define MCT_EXIT_KILLED 1000

typedef struct mct_paths_t {
    const char *tool;          /* 0 */
    const char *report;        /* 8 */
    const char *machine_yaml;  /* 16 */
    const char *generated_dir; /* 24 */
    const char *work_dir;      /* 32 */
    const char *gcc;           /* 40 */
    const char *nasm;          /* 48 */
    const char *nvcc;          /* 56 */
    const char *cmake;         /* 64 */
} mct_paths_t;
static_assert(sizeof(mct_paths_t) == 72);

/* A one-word change to the fixture, the line it must be reported on, and what the message must say. */
typedef struct mct_bad_case_t {
    const char *from;     /* 0 */
    const char *to;       /* 8 */
    const char *expected; /* 16 */
    uint32_t line;        /* 24 */
    uint8_t _pad0[4];     /* 28 */
} mct_bad_case_t;
static_assert(sizeof(mct_bad_case_t) == 32);

/* Deliberately not the real config/machine.yaml, whose values may change. The numbers on the right are line numbers. */
static const char MCT_FIXTURE[] =
    "platform: { os: linux, posix: true }\n"                                                                    /*  1 */
    "toolchain: { c_standard: c23, gcc_min: 16, nvcc_min: \"13.4\", nasm_min: \"3.02\", cmake_min: \"4.4\" }\n" /*  2 */
    "cpu:\n"                                                                                                    /*  3 */
    "  model: i9-10900K\n"                                                                                      /*  4 */
    "  arch_flag: skylake\n"                                                                                    /*  5 */
    "  logical_threads: 20\n"                                                                                   /*  6 */
    "  physical_cores: 10\n"                                                                                    /*  7 */
    "  cache_line_bytes: 64\n"                                                                                  /*  8 */
    "  l1d_kib: 32\n"                                                                                           /*  9 */
    "  l2_kib: 256\n"                                                                                           /* 10 */
    "  l3_mib: 20\n"                                                                                            /* 11 */
    "  simd: { sse42: true, avx: true, avx2: true, fma3: true, bmi1: true, bmi2: true, popcnt: true, "
    "lzcnt: true, f16c: true, avx512: false }\n"                                                              /* 12 */
    "threads: { main: 1, sim: 1, gpu: 1, workers: 16, audio: 1, reserved_os: 0, pin_physical_first: true }\n" /* 13 */
    "gpu:\n"                                                                                                  /* 14 */
    "  enabled: true\n"                                                                                       /* 15 */
    "  api: cuda\n"                                                                                           /* 16 */
    "  model: RTX 2080 Super\n"                                                                               /* 17 */
    "  architecture: sm_75\n"                                                                                 /* 18 */
    "  vram_mib: 8192\n"                                                                                      /* 19 */
    "  budget: { voxels_mib: 6144, textures_mib: 256, frames_mib: 256 }\n"                                    /* 20 */
    "display: { backend: kms, device: /dev/dri/card1, connector: auto, width: 1920, height: 1080, refresh_hz: 120 "
    "}\n"                                                                               /* 21 */
    "memory: { huge_page_bytes: 2097152, process_budget_mib: 16384 }\n"                 /* 22 */
    "game: { render_distance_default: 32, render_distance_max: 512, tick_hz: 20 }\n"    /* 23 */
    "world: { z_min: -250, z_sea: 0, z_max: 500, chunk_size: 16, section_size: 16 }\n"; /* 24 */

static const mct_bad_case_t MCT_BAD_CASES[] = {
    /* Wrong types and ranges, each on its own line. */
    {"workers: 16", "workers: many", "integer", 13u, {0}},
    {"workers: 16", "workers: 0", "threads.workers is 0", 13u, {0}},
    {"workers: 16", "workers: -1", "threads.workers is -1", 13u, {0}},
    {"workers: 16", "workers: 99999999999999999999", "", 13u, {0}},
    {"workers: 16", "workers:", "", 13u, {0}},
    {"workers: 16", "workers: [1, 2]", "", 13u, {0}},
    {"posix: true", "posix: \"true\"", "", 1u, {0}},
    {"posix: true", "posix: yes", "", 1u, {0}},
    {"enabled: true", "enabled: 1", "", 15u, {0}},
    {"vram_mib: 8192", "vram_mib: 2000000000000", "gpu.vram_mib is", 19u, {0}},
    {"main: 1", "main: 2", "threads.main is 2", 13u, {0}},
    /* Words from a fixed list. */
    {"os: linux", "os: plan9", "platform.os must be one of: linux", 1u, {0}},
    {"backend: kms", "backend: x11", "display.backend must be one of: kms", 21u, {0}},
    {"api: cuda", "api: opencl", "gpu.api must be one of: cuda", 16u, {0}},
    /* Text that could break out of its quotes, or out of a compiler flag. */
    {"model: i9-10900K", "model: \"a\\\"b\"", "0x22", 4u, {0}},
    {"model: i9-10900K", "model: \"a\\\\b\"", "0x5c", 4u, {0}},
    {"model: i9-10900K", "model: \"a;b\"", "0x3b", 4u, {0}},
    {"model: i9-10900K", "model: \"a$b\"", "0x24", 4u, {0}},
    {"model: i9-10900K", "model: \"a`b\"", "0x60", 4u, {0}},
    {"model: i9-10900K", "model: \"a\\nb\"", "0x0a", 4u, {0}},
    {"model: i9-10900K", "model: \"\"", "may not be empty", 4u, {0}},
    {"arch_flag: skylake", "arch_flag: sky lake", "0x20", 5u, {0}},
    {"arch_flag: skylake", "arch_flag: \"skylake;rm\"", "0x3b", 5u, {0}},
    /* Keys that do not exist, and sections that are not sections. */
    {"workers: 16", "workers: 16, wrkers: 3", "unknown key 'threads.wrkers'", 13u, {0}},
    {"sse42: true", "sse42: true, sse5: true", "unknown key 'cpu.simd.sse5'", 12u, {0}},
    {"world: {", "worlds: { a: 1 }\nworld: {", "unknown key 'worlds'", 24u, {0}},
    {"memory: { huge_page_bytes: 2097152, process_budget_mib: 16384 }",
     "memory: 5",
     "'memory' must be a mapping",
     22u,
     {0}},
    {"budget: { voxels_mib: 6144, textures_mib: 256, frames_mib: 256 }",
     "budget: [1]",
     "'gpu.budget' must be a mapping",
     20u,
     {0}},
    {"l1d_kib: 32", "l1d_kib: { deep: { deeper: { deepest: 1 } } }", "", 9u, {0}},
    /* A missing key can only be blamed on the document, which starts at line 1. */
    {", tick_hz: 20", "", "missing key 'game.tick_hz'", 1u, {0}},
    {"  model: i9-10900K\n", "", "missing key 'cpu.model'", 1u, {0}},
    /* Rules across keys. */
    {"workers: 16", "workers: 17", "threads add up to 21, past cpu.logical_threads of 20", 13u, {0}},
    {"physical_cores: 10", "physical_cores: 21", "cpu.physical_cores", 7u, {0}},
    {"cache_line_bytes: 64", "cache_line_bytes: 60", "power of two", 8u, {0}},
    {"huge_page_bytes: 2097152", "huge_page_bytes: 2097153", "power of two", 22u, {0}},
    {"sse42: true", "sse42: false", "cpu.simd.avx needs cpu.simd.sse42", 12u, {0}},
    {"avx: true", "avx: false", "cpu.simd.avx2 needs cpu.simd.avx", 12u, {0}},
    {"avx: true, avx2: true, fma3: true",
     "avx: false, avx2: false, fma3: false",
     "cpu.simd.f16c needs cpu.simd.avx",
     12u,
     {0}},
    {"f16c: true, ", "", "missing key 'cpu.simd.f16c'", 1u, {0}},
    {"voxels_mib: 6144", "voxels_mib: 7681", "gpu.budget adds up to 8193 MiB", 20u, {0}},
    {"gpu: 1, workers", "gpu: 0, workers", "threads.gpu must be 1 while gpu.enabled is true", 13u, {0}},
    {"render_distance_default: 32", "render_distance_default: 513", "render_distance_default", 23u, {0}},
    {"z_min: -250", "z_min: 500", "world.z_min must be below world.z_max", 24u, {0}},
    {"z_sea: 0", "z_sea: 501", "world.z_sea", 24u, {0}},
    {"chunk_size: 16", "chunk_size: 15", "world.chunk_size must be a power of two", 24u, {0}},
    {"section_size: 16", "section_size: 12", "world.section_size must be a power of two", 24u, {0}},
    /* YAML the parser refuses keeps the parser's own line. */
    {"  l2_kib: 256\n", "  l2_kib: 256\n\tl2b: 1\n", "tab", 11u, {0}},
    {"  l2_kib: 256\n", "  l2_kib: 256\n  l2_kib: 257\n", "", 11u, {0}},
    {"  model: i9-10900K\n", "  model: &a i9-10900K\n", "", 4u, {0}},
};

static uint8_t mct_buffer_a[MCT_FILE_BYTES];
static uint8_t mct_buffer_b[MCT_FILE_BYTES];
static char mct_text[MCT_FILE_BYTES];
static char mct_captured[MCT_FILE_BYTES];

static void mct_join(char *out, const char *directory, const char *name) {
    const int32_t length = snprintf(out, MCT_PATH_BYTES, "%s/%s", directory, name);
    if (length < 0 || (uint32_t)length >= MCT_PATH_BYTES) {
        printf("FAIL path too long: %s/%s\n", directory, name);
        _exit(1);
    }
}

static bool mct_write_file(const char *path, const void *bytes, uint64_t length) {
    const int32_t descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (descriptor < 0) {
        printf("FAIL cannot create %s: %s\n", path, strerror(errno));
        return false;
    }
    const int64_t wrote = write(descriptor, bytes, length);
    close(descriptor);
    return wrote == (int64_t)length;
}

/* Reads a whole file and NUL-terminates it. Returns false if it cannot, or if it does not fit. */
static bool mct_read_file(const char *path, uint8_t *out, uint64_t capacity, uint64_t *out_length) {
    const int32_t descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return false;
    }
    uint64_t total = 0u;
    bool ok = true;
    while (total + 1u < capacity) {
        const int64_t got = read(descriptor, out + total, capacity - 1u - total);
        if (got < 0) {
            ok = false;
        }
        if (got <= 0) {
            break;
        }
        total += (uint64_t)got;
    }
    close(descriptor);
    out[total] = 0u;
    *out_length = total;
    return ok && total + 1u < capacity;
}

/*
 * Runs a program under `timeout`, with its output in mct_captured. Returns its exit status, 128 + signal if a signal
 * ended it, or MCT_EXIT_KILLED if it overran or could not start. arguments ends in nullptr.
 */
static int32_t mct_run(const mct_paths_t *paths, const char *seconds, const char *const *arguments) {
    char *argv[MCT_MAX_ARGS + 3u];
    uint32_t count = 0u;
    argv[count++] = (char *)"timeout";
    argv[count++] = (char *)seconds;
    for (uint32_t i = 0u; arguments[i] != nullptr && count + 1u < MCT_MAX_ARGS + 3u; i++) {
        argv[count++] = (char *)arguments[i];
    }
    argv[count] = nullptr;

    char capture[MCT_PATH_BYTES];
    mct_join(capture, paths->work_dir, "captured.txt");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, capture, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);

    pid_t child = 0;
    const int32_t spawned = posix_spawnp(&child, "timeout", &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) {
        printf("FAIL cannot start %s: %s\n", arguments[0], strerror(spawned));
        return MCT_EXIT_KILLED;
    }
    int32_t status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return MCT_EXIT_KILLED;
        }
    }
    uint64_t captured_length = 0u;
    if (!mct_read_file(capture, (uint8_t *)mct_captured, sizeof(mct_captured), &captured_length)) {
        mct_captured[0] = '\0';
    }
    if (!WIFEXITED(status)) {
        return 128 + WTERMSIG(status);
    }
    /* timeout's own status for an overrun, and for a child killed by a signal it reports as 128 + signal. */
    const int32_t code = WEXITSTATUS(status);
    return code == 124 ? MCT_EXIT_KILLED : code;
}

static int32_t mct_tool(const mct_paths_t *paths, const char *input, const char *output_directory) {
    const char *const arguments[] = {paths->tool, input, output_directory, nullptr};
    return mct_run(paths, MCT_TIMEOUT_TOOL, arguments);
}

/* out receives the fixture with its only occurrence of from replaced. Fails the run if from is not in the fixture. */
static uint64_t mct_mutate(char *out, const char *from, const char *to) {
    const char *at = strstr(MCT_FIXTURE, from);
    if (at == nullptr || strstr(at + 1, from) != nullptr) {
        printf("FAIL the fixture must hold '%s' exactly once\n", from);
        _exit(1);
    }
    const uint64_t prefix = (uint64_t)(at - MCT_FIXTURE);
    const int32_t length =
        snprintf(out, MCT_FILE_BYTES, "%.*s%s%s", (int32_t)prefix, MCT_FIXTURE, to, at + strlen(from));
    return (uint64_t)length;
}

/* True if the file holds this whole line. */
static bool mct_file_has_line(const char *directory, const char *name, const char *line) {
    char path[MCT_PATH_BYTES];
    mct_join(path, directory, name);
    uint64_t length = 0u;
    if (!mct_read_file(path, mct_buffer_a, sizeof(mct_buffer_a), &length)) {
        printf("FAIL cannot read %s\n", path);
        return false;
    }
    const uint64_t line_length = strlen(line);
    const char *cursor = (const char *)mct_buffer_a;
    while ((cursor = strstr(cursor, line)) != nullptr) {
        const bool starts = cursor == (const char *)mct_buffer_a || cursor[-1] == '\n';
        if (starts && cursor[line_length] == '\n') {
            return true;
        }
        cursor += 1;
    }
    printf("  %s has no line: %s\n", path, line);
    return false;
}

/* Every language's spelling of one value. */
static void mct_check_value(test_context_t *t, const char *directory, const char *name, const char *c_value,
                            const char *cmake_value) {
    char line[256];
    snprintf(line, sizeof(line), "#define %s %s", name, c_value);
    CHECK(mct_file_has_line(directory, "index.h", line));
    CHECK(mct_file_has_line(directory, "index.hu", line));
    snprintf(line, sizeof(line), "%%define %s %s", name, c_value);
    CHECK(mct_file_has_line(directory, "index.inc", line));
    snprintf(line, sizeof(line), "set(%s %s)", name, cmake_value);
    CHECK(mct_file_has_line(directory, "index.cmake", line));
}

static bool mct_files_equal(const char *path_a, const char *path_b) {
    uint64_t length_a = 0u;
    uint64_t length_b = 0u;
    if (!mct_read_file(path_a, mct_buffer_a, sizeof(mct_buffer_a), &length_a) ||
        !mct_read_file(path_b, mct_buffer_b, sizeof(mct_buffer_b), &length_b)) {
        printf("FAIL cannot read %s or %s\n", path_a, path_b);
        return false;
    }
    if (length_a != length_b || memcmp(mct_buffer_a, mct_buffer_b, length_a) != 0) {
        printf("  %s and %s differ\n", path_a, path_b);
        return false;
    }
    return true;
}

static const char *const MCT_OUTPUT_NAMES[] = {"index.h", "index.hu", "index.inc", "index.cmake"};
enum : uint32_t { MCT_OUTPUT_COUNT = 4u };

static void mct_make_dir(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        printf("FAIL cannot create %s: %s\n", path, strerror(errno));
        _exit(1);
    }
}

/* Creates each missing directory of the work path, which lives under the build directory. */
static void mct_make_work_dir(const char *work_dir) {
    char path[MCT_PATH_BYTES];
    const uint64_t length = strlen(work_dir);
    if (length >= sizeof(path)) {
        printf("FAIL work path too long\n");
        _exit(1);
    }
    memcpy(path, work_dir, length + 1u);
    for (uint64_t i = 1u; i < length; i++) {
        if (path[i] == '/') {
            path[i] = '\0';
            mct_make_dir(path);
            path[i] = '/';
        }
    }
    mct_make_dir(path);
}

/* The paths from test.cmake, in memory that lasts until the running test ends. */
static const mct_paths_t *mct_paths(test_context_t *t) {
    if (t->arg_count != 9) {
        printf("usage: tool report machine_yaml generated_dir work_dir gcc nasm nvcc cmake\n");
        _exit(2);
    }
    char **argv = t->borrowed_args;
    mct_paths_t *paths = test_alloc(t, sizeof(mct_paths_t));
    *paths = (mct_paths_t){
        .tool = argv[0],
        .report = argv[1],
        .machine_yaml = argv[2],
        .generated_dir = argv[3],
        .work_dir = argv[4],
        .gcc = argv[5],
        .nasm = argv[6],
        .nvcc = argv[7],
        .cmake = argv[8],
    };
    mct_make_work_dir(paths->work_dir);
    return paths;
}

/* Writes the fixture and generates from it, into work/<name>/machine, so that <name> works as an include root. */
static void mct_generate_fixture(test_context_t *t, const mct_paths_t *paths, const char *name, const char *text,
                                 uint64_t length, char *out_directory) {
    char root[MCT_PATH_BYTES];
    char input[MCT_PATH_BYTES];
    mct_join(root, paths->work_dir, name);
    mct_make_dir(root);
    mct_join(input, root, "machine.yaml");
    CHECK(mct_write_file(input, text, length));
    mct_join(out_directory, root, "machine");
    const int32_t status = mct_tool(paths, input, out_directory);
    if (status != 0) {
        printf("  tool said: %s", mct_captured);
    }
    CHECK(status == 0);
}

TEST(values) {
    const mct_paths_t *paths = mct_paths(t);
    char directory[MCT_PATH_BYTES];
    mct_generate_fixture(t, paths, "values", MCT_FIXTURE, sizeof(MCT_FIXTURE) - 1u, directory);
    mct_check_value(t, directory, "MC_PLATFORM_POSIX", "1", "1");
    mct_check_value(t, directory, "MC_THREADS_WORKERS", "16", "16");
    mct_check_value(t, directory, "MC_CPU_HAS_AVX2", "1", "1");
    mct_check_value(t, directory, "MC_CPU_HAS_F16C", "1", "1");
    mct_check_value(t, directory, "MC_CPU_HAS_AVX512", "0", "0");
    mct_check_value(t, directory, "MC_GPU_ENABLED", "1", "1");
    mct_check_value(t, directory, "MC_WORLD_Z_MIN", "(-250)", "-250");
    mct_check_value(t, directory, "MC_WORLD_Z_SEA", "0", "0");
    mct_check_value(t, directory, "MC_GPU_BUDGET_VOXELS_MIB", "6144", "6144");
    mct_check_value(t, directory, "MC_MEMORY_HUGE_PAGE_BYTES", "2097152", "2097152");
    mct_check_value(t, directory, "MC_CPU_ARCH_FLAG", "\"skylake\"", "\"skylake\"");
    mct_check_value(t, directory, "MC_CPU_MODEL", "\"i9-10900K\"", "\"i9-10900K\"");
    mct_check_value(t, directory, "MC_GPU_MODEL", "\"RTX 2080 Super\"", "\"RTX 2080 Super\"");
    mct_check_value(t, directory, "MC_DISPLAY_DEVICE", "\"/dev/dri/card1\"", "\"/dev/dri/card1\"");
    mct_check_value(t, directory, "MC_TOOLCHAIN_NVCC_MIN", "\"13.4\"", "\"13.4\"");
    mct_check_value(t, directory, "MC_DISPLAY_BACKEND", "\"kms\"", "\"kms\"");
    mct_check_value(t, directory, "MC_DISPLAY_BACKEND_KMS", "1", "1");
    mct_check_value(t, directory, "MC_PLATFORM_OS_LINUX", "1", "1");
    mct_check_value(t, directory, "MC_GPU_API_CUDA", "1", "1");
}

/* The stage's proof, at the file level: one yaml value moves, and only that define moves, in all four languages. */
TEST(change_propagates) {
    const mct_paths_t *paths = mct_paths(t);
    char before[MCT_PATH_BYTES];
    char after[MCT_PATH_BYTES];
    mct_generate_fixture(t, paths, "before", MCT_FIXTURE, sizeof(MCT_FIXTURE) - 1u, before);
    const uint64_t length = mct_mutate(mct_text, "workers: 16", "workers: 12");
    mct_generate_fixture(t, paths, "after", mct_text, length, after);
    mct_check_value(t, after, "MC_THREADS_WORKERS", "12", "12");

    for (uint32_t i = 0u; i < MCT_OUTPUT_COUNT; i++) {
        char path_before[MCT_PATH_BYTES];
        char path_after[MCT_PATH_BYTES];
        mct_join(path_before, before, MCT_OUTPUT_NAMES[i]);
        mct_join(path_after, after, MCT_OUTPUT_NAMES[i]);
        uint64_t length_before = 0u;
        uint64_t length_after = 0u;
        CHECK(mct_read_file(path_before, mct_buffer_a, sizeof(mct_buffer_a), &length_before));
        CHECK(mct_read_file(path_after, mct_buffer_b, sizeof(mct_buffer_b), &length_after));
        /* Two digits became two digits, so the files line up, and exactly one byte differs: 6 became 2, the 1 stays. */
        CHECK(length_before == length_after);
        uint32_t differing = 0u;
        for (uint64_t b = 0u; b < length_before && b < length_after; b++) {
            differing += mct_buffer_a[b] != mct_buffer_b[b] ? 1u : 0u;
        }
        CHECK(differing == 1u);
    }
}

TEST(stable_output) {
    const mct_paths_t *paths = mct_paths(t);
    char directory[MCT_PATH_BYTES];
    char input[MCT_PATH_BYTES];
    char root[MCT_PATH_BYTES];
    mct_generate_fixture(t, paths, "stable", MCT_FIXTURE, sizeof(MCT_FIXTURE) - 1u, directory);
    mct_join(root, paths->work_dir, "stable");
    mct_join(input, root, "machine.yaml");

    /* Backdate every output: rewriting one would bring its time forward, and a rebuild of everything with it. */
    struct timespec backdated[2] = {{.tv_sec = 1000000000, .tv_nsec = 0}, {.tv_sec = 1000000000, .tv_nsec = 0}};
    for (uint32_t i = 0u; i < MCT_OUTPUT_COUNT; i++) {
        char path[MCT_PATH_BYTES];
        mct_join(path, directory, MCT_OUTPUT_NAMES[i]);
        CHECK(utimensat(AT_FDCWD, path, backdated, 0) == 0);
    }
    CHECK(mct_tool(paths, input, directory) == 0);
    for (uint32_t i = 0u; i < MCT_OUTPUT_COUNT; i++) {
        char path[MCT_PATH_BYTES];
        struct stat status;
        mct_join(path, directory, MCT_OUTPUT_NAMES[i]);
        CHECK(stat(path, &status) == 0);
        CHECK(status.st_mtim.tv_sec == backdated[0].tv_sec);
    }

    /* A changed value does rewrite, with nothing left behind but the four files. */
    const uint64_t length = mct_mutate(mct_text, "tick_hz: 20", "tick_hz: 30");
    CHECK(mct_write_file(input, mct_text, length));
    CHECK(mct_tool(paths, input, directory) == 0);
    CHECK(mct_file_has_line(directory, "index.h", "#define MC_GAME_TICK_HZ 30"));
    CHECK(mct_file_has_line(directory, "index.cmake", "set(MC_GAME_TICK_HZ 30)"));
    DIR *listing = opendir(directory);
    CHECK(listing != nullptr);
    uint32_t entries = 0u;
    while (listing != nullptr && readdir(listing) != nullptr) {
        entries++;
    }
    if (listing != nullptr) {
        closedir(listing);
    }
    CHECK(entries == MCT_OUTPUT_COUNT + 2u); /* . and .. */

    /* Key order and comments in the yaml do not reach the output. */
    char moved[MCT_PATH_BYTES];
    uint64_t swapped = mct_mutate(mct_text, "platform: { os: linux, posix: true }\n",
                                  "# a comment\nplatform: { posix: true, os: linux }  # another\n");
    mct_generate_fixture(t, paths, "swapped", mct_text, swapped, moved);
    char original[MCT_PATH_BYTES];
    mct_generate_fixture(t, paths, "plain", MCT_FIXTURE, sizeof(MCT_FIXTURE) - 1u, original);
    for (uint32_t i = 0u; i < MCT_OUTPUT_COUNT; i++) {
        char path_a[MCT_PATH_BYTES];
        char path_b[MCT_PATH_BYTES];
        mct_join(path_a, moved, MCT_OUTPUT_NAMES[i]);
        mct_join(path_b, original, MCT_OUTPUT_NAMES[i]);
        CHECK(mct_files_equal(path_a, path_b));
    }
}

/* Each generated file is real input to the program it was written for, and each sees the right values at compile time. */
TEST(consumers) {
    const mct_paths_t *paths = mct_paths(t);
    char directory[MCT_PATH_BYTES];
    char root[MCT_PATH_BYTES];
    char include_flag[MCT_PATH_BYTES + 8u];
    char source[MCT_PATH_BYTES];
    char object[MCT_PATH_BYTES];
    mct_generate_fixture(t, paths, "consumers", MCT_FIXTURE, sizeof(MCT_FIXTURE) - 1u, directory);
    mct_join(root, paths->work_dir, "consumers");
    snprintf(include_flag, sizeof(include_flag), "-I%s", root);
    mct_join(object, root, "probe.o");

    static const char c_probe[] =
        "#include \"machine/index.h\"\n"
        "#include \"machine/index.h\"\n" /* the guard must hold */
        "static_assert(MC_THREADS_WORKERS == 16);\n"
        "static_assert(MC_WORLD_Z_MIN == -250);\n"
        "static_assert(MC_WORLD_Z_MIN * 2 == -500);\n"
        "static_assert(MC_CPU_HAS_AVX2 == 1 && MC_CPU_HAS_AVX512 == 0 && MC_GPU_ENABLED && MC_PLATFORM_POSIX);\n"
        "static_assert(sizeof(MC_GPU_MODEL) == 15);\n"
        "#if !MC_DISPLAY_BACKEND_KMS || MC_WORLD_Z_MIN >= 0\n#error wrong\n#endif\n";
    mct_join(source, root, "probe.c");
    CHECK(mct_write_file(source, c_probe, sizeof(c_probe) - 1u));
    const char *const gcc[] = {paths->gcc,   "-std=c23", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                               include_flag, "-c",       source,  "-o",      object,       nullptr};
    int32_t status = mct_run(paths, MCT_TIMEOUT_COMPILER, gcc);
    if (status != 0) {
        printf("  gcc said: %s", mct_captured);
    }
    CHECK(status == 0);

    static const char nasm_probe[] =
        "%include \"machine/index.inc\"\n"
        "%include \"machine/index.inc\"\n"
        "%if MC_THREADS_WORKERS != 16 || MC_WORLD_Z_MIN != -250 || MC_CPU_HAS_AVX512 != 0\n"
        "%error wrong values\n"
        "%endif\n"
        "%if !MC_DISPLAY_BACKEND_KMS\n"
        "%error wrong backend\n"
        "%endif\n"
        "section .note.GNU-stack noalloc noexec nowrite progbits\n"
        "section .rodata\n"
        "workers: dd MC_THREADS_WORKERS\n"
        "z_min: dd MC_WORLD_Z_MIN\n"
        "model: db MC_GPU_MODEL, 0\n";
    char nasm_include[MCT_PATH_BYTES + 8u];
    snprintf(nasm_include, sizeof(nasm_include), "-I%s/", root);
    mct_join(source, root, "probe.asm");
    CHECK(mct_write_file(source, nasm_probe, sizeof(nasm_probe) - 1u));
    const char *const nasm[] = {paths->nasm,  "-w+all", "-Werror", "-f",   "elf64",
                                nasm_include, source,   "-o",      object, nullptr};
    status = mct_run(paths, MCT_TIMEOUT_COMPILER, nasm);
    if (status != 0) {
        printf("  nasm said: %s", mct_captured);
    }
    CHECK(status == 0);

    static const char cuda_probe[] = "#include \"machine/index.hu\"\n"
                                     "#include \"machine/index.hu\"\n"
                                     "static_assert(MC_THREADS_WORKERS == 16);\n"
                                     "static_assert(MC_WORLD_Z_MIN == -250);\n"
                                     "static_assert(MC_CPU_HAS_AVX2 == 1 && MC_GPU_ENABLED == 1);\n"
                                     "__global__ void probe(unsigned *out) { out[0] = MC_THREADS_WORKERS; }\n";
    mct_join(source, root, "probe.cu");
    CHECK(mct_write_file(source, cuda_probe, sizeof(cuda_probe) - 1u));
    const char *const nvcc[] = {paths->nvcc, "-std=c++20", "-arch=sm_75", include_flag, "-c",
                                source,      "-o",         object,        nullptr};
    status = mct_run(paths, MCT_TIMEOUT_COMPILER, nvcc);
    if (status != 0) {
        printf("  nvcc said: %s", mct_captured);
    }
    CHECK(status == 0);

    char script_text[MCT_PATH_BYTES * 2u];
    const int32_t script_length =
        snprintf(script_text, sizeof(script_text),
                 "include(\"%s/index.cmake\")\n"
                 "if(NOT MC_THREADS_WORKERS EQUAL 16 OR NOT MC_WORLD_Z_MIN EQUAL -250 OR MC_CPU_HAS_AVX512 OR\n"
                 "   NOT MC_CPU_HAS_AVX2 OR NOT MC_GPU_MODEL STREQUAL \"RTX 2080 Super\" OR\n"
                 "   NOT MC_CPU_ARCH_FLAG STREQUAL \"skylake\" OR NOT MC_DISPLAY_BACKEND_KMS)\n"
                 "  message(FATAL_ERROR \"wrong values\")\n"
                 "endif()\n",
                 directory);
    mct_join(source, root, "probe.cmake");
    CHECK(mct_write_file(source, script_text, (uint64_t)script_length));
    const char *const cmake[] = {paths->cmake, "-P", source, nullptr};
    status = mct_run(paths, MCT_TIMEOUT_COMPILER, cmake);
    if (status != 0) {
        printf("  cmake said: %s", mct_captured);
    }
    CHECK(status == 0);
}

TEST(bad_input) {
    const mct_paths_t *paths = mct_paths(t);
    char directory[MCT_PATH_BYTES];
    char root[MCT_PATH_BYTES];
    char input[MCT_PATH_BYTES];
    char header[MCT_PATH_BYTES];
    char reference[MCT_PATH_BYTES];
    mct_generate_fixture(t, paths, "bad", MCT_FIXTURE, sizeof(MCT_FIXTURE) - 1u, directory);
    mct_join(root, paths->work_dir, "bad");
    mct_join(input, root, "machine.yaml");
    mct_join(header, directory, "index.h");
    mct_join(reference, root, "index.h.reference");
    uint64_t header_length = 0u;
    CHECK(mct_read_file(header, mct_buffer_a, sizeof(mct_buffer_a), &header_length));
    CHECK(mct_write_file(reference, mct_buffer_a, header_length));

    const uint32_t count = sizeof(MCT_BAD_CASES) / sizeof(MCT_BAD_CASES[0]);
    for (uint32_t i = 0u; i < count; i++) {
        const mct_bad_case_t *bad = &MCT_BAD_CASES[i];
        const uint64_t length = mct_mutate(mct_text, bad->from, bad->to);
        CHECK(mct_write_file(input, mct_text, length));
        const int32_t status = mct_tool(paths, input, directory);

        char position[MCT_PATH_BYTES + 32u];
        snprintf(position, sizeof(position), "%s:%u:", input, bad->line);
        const bool right =
            status == 1 && strstr(mct_captured, position) != nullptr && strstr(mct_captured, bad->expected) != nullptr;
        if (!CHECK(right)) {
            printf("FAIL bad case %u (%s -> %s): expected status 1 at line %u with '%s', got status %d: %s\n", i,
                   bad->from, bad->to, bad->line, bad->expected, status, mct_captured);
        }
        /* A refusal writes nothing. */
        CHECK(mct_files_equal(header, reference));
    }

    /* Whole-document problems. */
    CHECK(mct_write_file(input, "", 0u));
    CHECK(mct_tool(paths, input, directory) == 1);
    CHECK(strstr(mct_captured, "must be a mapping") != nullptr);
    CHECK(mct_write_file(input, "- a\n- b\n", 8u));
    CHECK(mct_tool(paths, input, directory) == 1);
    CHECK(strstr(mct_captured, "must be a mapping") != nullptr);
    CHECK(mct_write_file(input, "just text\n", 10u));
    CHECK(mct_tool(paths, input, directory) == 1);
    CHECK(mct_files_equal(header, reference));
}

/* No damage to the input may crash the tool. It either accepts a still-complete file or reports a problem. */
TEST(damaged_input) {
    const mct_paths_t *paths = mct_paths(t);
    char root[MCT_PATH_BYTES];
    char input[MCT_PATH_BYTES];
    char directory[MCT_PATH_BYTES];
    mct_generate_fixture(t, paths, "damage", MCT_FIXTURE, sizeof(MCT_FIXTURE) - 1u, directory);
    mct_join(root, paths->work_dir, "damage");
    mct_join(input, root, "machine.yaml");

    const uint32_t length = (uint32_t)sizeof(MCT_FIXTURE) - 1u;
    uint32_t accepted = 0u;
    uint32_t unexpected = 0u;
    for (uint32_t at = 0u; at <= length; at++) {
        CHECK(mct_write_file(input, MCT_FIXTURE, at));
        const int32_t status = mct_tool(paths, input, directory);
        if (status == 0) {
            accepted++;
        } else if (status != 1) {
            printf("FAIL truncation at %u gave status %d: %s\n", at, status, mct_captured);
            unexpected++;
        }
    }
    /* Only the file itself, and the file without its last line break, still has every key. */
    CHECK(unexpected == 0u);
    CHECK(accepted == 2u);

    /* Three of the bytes most likely to confuse a parser, at every 7th position, to keep this to a few seconds. */
    static const char confusing[] = {':', '{', '\n'};
    unexpected = 0u;
    for (uint32_t at = 0u; at < length; at += 7u) {
        for (uint32_t c = 0u; c < sizeof(confusing); c++) {
            memcpy(mct_text, MCT_FIXTURE, length);
            mct_text[at] = confusing[c];
            CHECK(mct_write_file(input, mct_text, length));
            const int32_t status = mct_tool(paths, input, directory);
            if (status != 0 && status != 1) {
                printf("FAIL overwrite at %u with 0x%02x gave status %d: %s\n", at, (uint32_t)confusing[c], status,
                       mct_captured);
                unexpected++;
            }
        }
    }
    CHECK(unexpected == 0u);
}

TEST(cannot_run) {
    const mct_paths_t *paths = mct_paths(t);
    char root[MCT_PATH_BYTES];
    char input[MCT_PATH_BYTES];
    char blocker[MCT_PATH_BYTES];
    char blocked[MCT_PATH_BYTES];
    char missing[MCT_PATH_BYTES];
    char big[MCT_PATH_BYTES];
    mct_join(root, paths->work_dir, "cannot");
    mct_make_dir(root);
    mct_join(input, root, "machine.yaml");
    CHECK(mct_write_file(input, MCT_FIXTURE, sizeof(MCT_FIXTURE) - 1u));

    /* The usage error. */
    const char *const no_arguments[] = {paths->tool, nullptr};
    CHECK(mct_run(paths, MCT_TIMEOUT_TOOL, no_arguments) == 2);
    CHECK(strstr(mct_captured, "usage:") != nullptr);

    /* An input that cannot be read. */
    mct_join(missing, root, "no_such_file.yaml");
    CHECK(mct_tool(paths, missing, root) == 2);
    CHECK(strstr(mct_captured, "no_such_file.yaml") != nullptr);
    CHECK(mct_tool(paths, root, root) == 2);

    /* An input too big to be the real thing, without mapping nodes for it. */
    mct_join(big, root, "big.yaml");
    memset(mct_buffer_b, ' ', sizeof(mct_buffer_b));
    CHECK(mct_write_file(big, mct_buffer_b, sizeof(mct_buffer_b)));
    CHECK(truncate(big, (int64_t)(1u << 20) + 1) == 0);
    CHECK(mct_tool(paths, big, root) == 2);

    /* An output that cannot be written: the directory's parent is a plain file. */
    mct_join(blocker, root, "blocker");
    CHECK(mct_write_file(blocker, "x", 1u));
    mct_join(blocked, blocker, "machine");
    CHECK(mct_tool(paths, input, blocked) == 2);
}

/* The real file, as this build saw it. */
TEST(real_config) {
    const mct_paths_t *paths = mct_paths(t);
    char root[MCT_PATH_BYTES];
    char directory[MCT_PATH_BYTES];
    mct_join(root, paths->work_dir, "real");
    mct_make_dir(root);
    mct_join(directory, root, "machine");
    CHECK(mct_tool(paths, paths->machine_yaml, directory) == 0);
    for (uint32_t i = 0u; i < MCT_OUTPUT_COUNT; i++) {
        char path_a[MCT_PATH_BYTES];
        char path_b[MCT_PATH_BYTES];
        mct_join(path_a, directory, MCT_OUTPUT_NAMES[i]);
        mct_join(path_b, paths->generated_dir, MCT_OUTPUT_NAMES[i]);
        CHECK(mct_files_equal(path_a, path_b));
    }

    /* The defines compiled into this very test, against the yaml as read by the parser directly. */
    static yaml_node_t nodes[4096];
    static uint8_t strings[16384];
    const yaml_storage_t storage = {nodes, strings, 4096u, 16384u};
    uint64_t length = 0u;
    CHECK(mct_read_file(paths->machine_yaml, mct_buffer_a, sizeof(mct_buffer_a), &length));
    yaml_document_t document;
    yaml_error_t error;
    CHECK(yaml_parse(&document, mct_buffer_a, length, storage, &error));
    const yaml_node_t *top = yaml_root(&document);
    int64_t workers = 0;
    bool posix = false;
    bool avx2 = false;
    bool enabled = false;
    CHECK(yaml_get_int64(yaml_path_get(&document, top, "threads.workers"), &workers, &error));
    CHECK(yaml_get_bool(yaml_path_get(&document, top, "platform.posix"), &posix, &error));
    CHECK(yaml_get_bool(yaml_path_get(&document, top, "cpu.simd.avx2"), &avx2, &error));
    CHECK(yaml_get_bool(yaml_path_get(&document, top, "gpu.enabled"), &enabled, &error));
    CHECK(workers == MC_THREADS_WORKERS);
    CHECK((posix ? 1 : 0) == MC_PLATFORM_POSIX);
    CHECK((avx2 ? 1 : 0) == MC_CPU_HAS_AVX2);
    CHECK((enabled ? 1 : 0) == MC_GPU_ENABLED);

    /* The report binary is a different program, compiled from the same header. */
    const char *const report[] = {paths->report, nullptr};
    CHECK(mct_run(paths, MCT_TIMEOUT_TOOL, report) == 0);
    char expected[64];
    snprintf(expected, sizeof(expected), "MC_THREADS_WORKERS=%d\n", MC_THREADS_WORKERS);
    CHECK(strstr(mct_captured, expected) != nullptr);
    snprintf(expected, sizeof(expected), "MC_PLATFORM_POSIX=%d\n", MC_PLATFORM_POSIX);
    CHECK(strstr(mct_captured, expected) != nullptr);
    snprintf(expected, sizeof(expected), "MC_CPU_HAS_AVX2=%d\n", MC_CPU_HAS_AVX2);
    CHECK(strstr(mct_captured, expected) != nullptr);
    snprintf(expected, sizeof(expected), "MC_GPU_ENABLED=%d\n", MC_GPU_ENABLED);
    CHECK(strstr(mct_captured, expected) != nullptr);
}

TEST_MAIN()
