/*
 * config/machine.yaml -> index.h, index.hu, index.inc, index.cmake. Runs at configure time, because the compiler
 * flags depend on its output, so cmake/machine.cmake builds this file and the YAML parser by hand.
 *
 * All checking happens before any file is touched, so a bad machine.yaml leaves the previous output in place. An
 * unchanged output keeps its modification time, so re-running on an unchanged yaml rebuilds nothing.
 */
#include "machine_config/index.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define MACHINE_INT_ANY INT64_C(1099511627776) /* 2^40: past any real value, and fits every consumer's arithmetic */

#define KEY_ROW(key_path, key_macro, key_choices, key_min, key_max, key_kind)                                          \
    {.path = key_path,                                                                                                 \
     .macro = key_macro,                                                                                               \
     .choices = key_choices,                                                                                           \
     .min = key_min,                                                                                                   \
     .max = key_max,                                                                                                   \
     .kind = key_kind,                                                                                                 \
     ._pad0 = {0}}
#define KEY_BOOL(key_path, key_macro) KEY_ROW(key_path, key_macro, nullptr, 0, 1, MACHINE_KIND_BOOL)
#define KEY_INT(key_path, key_macro, key_min, key_max)                                                                 \
    KEY_ROW(key_path, key_macro, nullptr, key_min, key_max, MACHINE_KIND_INT)
#define KEY_TEXT(key_path, key_macro) KEY_ROW(key_path, key_macro, nullptr, 0, 0, MACHINE_KIND_TEXT)
#define KEY_WORD(key_path, key_macro) KEY_ROW(key_path, key_macro, nullptr, 0, 0, MACHINE_KIND_WORD)
#define KEY_CHOICE(key_path, key_macro, key_choices)                                                                   \
    KEY_ROW(key_path, key_macro, key_choices, 0, 0, MACHINE_KIND_CHOICE)

/* Everything machine.yaml must say, in output order. A key not listed here is refused, so a typo cannot pass. */
typedef enum machine_key_id_t : uint8_t {
    KEY_PLATFORM_OS,
    KEY_PLATFORM_POSIX,
    KEY_TOOLCHAIN_C_STANDARD,
    KEY_TOOLCHAIN_GCC_MIN,
    KEY_TOOLCHAIN_NVCC_MIN,
    KEY_TOOLCHAIN_NASM_MIN,
    KEY_TOOLCHAIN_CMAKE_MIN,
    KEY_CPU_MODEL,
    KEY_CPU_ARCH_FLAG,
    KEY_CPU_LOGICAL_THREADS,
    KEY_CPU_PHYSICAL_CORES,
    KEY_CPU_CACHE_LINE_BYTES,
    KEY_CPU_L1D_KIB,
    KEY_CPU_L2_KIB,
    KEY_CPU_L3_MIB,
    KEY_CPU_HAS_SSE42,
    KEY_CPU_HAS_AVX,
    KEY_CPU_HAS_AVX2,
    KEY_CPU_HAS_FMA3,
    KEY_CPU_HAS_BMI1,
    KEY_CPU_HAS_BMI2,
    KEY_CPU_HAS_POPCNT,
    KEY_CPU_HAS_LZCNT,
    KEY_CPU_HAS_F16C,
    KEY_CPU_HAS_AVX512,
    KEY_THREADS_MAIN,
    KEY_THREADS_SIM,
    KEY_THREADS_GPU,
    KEY_THREADS_WORKERS,
    KEY_THREADS_AUDIO,
    KEY_THREADS_RESERVED_OS,
    KEY_THREADS_PIN_PHYSICAL_FIRST,
    KEY_GPU_ENABLED,
    KEY_GPU_API,
    KEY_GPU_MODEL,
    KEY_GPU_ARCHITECTURE,
    KEY_GPU_VRAM_MIB,
    KEY_GPU_BUDGET_VOXELS_MIB,
    KEY_GPU_BUDGET_TEXTURES_MIB,
    KEY_GPU_BUDGET_FRAMES_MIB,
    KEY_DISPLAY_BACKEND,
    KEY_DISPLAY_DEVICE,
    KEY_DISPLAY_CONNECTOR,
    KEY_DISPLAY_WIDTH,
    KEY_DISPLAY_HEIGHT,
    KEY_DISPLAY_REFRESH_HZ,
    KEY_MEMORY_HUGE_PAGE_BYTES,
    KEY_MEMORY_PROCESS_BUDGET_MIB,
    KEY_GAME_RENDER_DISTANCE_DEFAULT,
    KEY_GAME_RENDER_DISTANCE_MAX,
    KEY_GAME_TICK_HZ,
    KEY_WORLD_Z_MIN,
    KEY_WORLD_Z_SEA,
    KEY_WORLD_Z_MAX,
    KEY_WORLD_CHUNK_SIZE,
    KEY_WORLD_SECTION_SIZE,
    KEY_COUNT,
} machine_key_id_t;

static const machine_key_t machine_keys[KEY_COUNT] = {
    [KEY_PLATFORM_OS] = KEY_CHOICE("platform.os", "MC_PLATFORM_OS", "linux"),
    [KEY_PLATFORM_POSIX] = KEY_BOOL("platform.posix", "MC_PLATFORM_POSIX"),
    [KEY_TOOLCHAIN_C_STANDARD] = KEY_WORD("toolchain.c_standard", "MC_TOOLCHAIN_C_STANDARD"),
    [KEY_TOOLCHAIN_GCC_MIN] = KEY_INT("toolchain.gcc_min", "MC_TOOLCHAIN_GCC_MIN", 1, 1000),
    [KEY_TOOLCHAIN_NVCC_MIN] = KEY_WORD("toolchain.nvcc_min", "MC_TOOLCHAIN_NVCC_MIN"),
    [KEY_TOOLCHAIN_NASM_MIN] = KEY_WORD("toolchain.nasm_min", "MC_TOOLCHAIN_NASM_MIN"),
    [KEY_TOOLCHAIN_CMAKE_MIN] = KEY_WORD("toolchain.cmake_min", "MC_TOOLCHAIN_CMAKE_MIN"),
    [KEY_CPU_MODEL] = KEY_TEXT("cpu.model", "MC_CPU_MODEL"),
    [KEY_CPU_ARCH_FLAG] = KEY_WORD("cpu.arch_flag", "MC_CPU_ARCH_FLAG"),
    [KEY_CPU_LOGICAL_THREADS] = KEY_INT("cpu.logical_threads", "MC_CPU_LOGICAL_THREADS", 1, 4096),
    [KEY_CPU_PHYSICAL_CORES] = KEY_INT("cpu.physical_cores", "MC_CPU_PHYSICAL_CORES", 1, 4096),
    [KEY_CPU_CACHE_LINE_BYTES] = KEY_INT("cpu.cache_line_bytes", "MC_CPU_CACHE_LINE_BYTES", 8, 4096),
    [KEY_CPU_L1D_KIB] = KEY_INT("cpu.l1d_kib", "MC_CPU_L1D_KIB", 1, 1048576),
    [KEY_CPU_L2_KIB] = KEY_INT("cpu.l2_kib", "MC_CPU_L2_KIB", 1, 1048576),
    [KEY_CPU_L3_MIB] = KEY_INT("cpu.l3_mib", "MC_CPU_L3_MIB", 1, 1048576),
    [KEY_CPU_HAS_SSE42] = KEY_BOOL("cpu.simd.sse42", "MC_CPU_HAS_SSE42"),
    [KEY_CPU_HAS_AVX] = KEY_BOOL("cpu.simd.avx", "MC_CPU_HAS_AVX"),
    [KEY_CPU_HAS_AVX2] = KEY_BOOL("cpu.simd.avx2", "MC_CPU_HAS_AVX2"),
    [KEY_CPU_HAS_FMA3] = KEY_BOOL("cpu.simd.fma3", "MC_CPU_HAS_FMA3"),
    [KEY_CPU_HAS_BMI1] = KEY_BOOL("cpu.simd.bmi1", "MC_CPU_HAS_BMI1"),
    [KEY_CPU_HAS_BMI2] = KEY_BOOL("cpu.simd.bmi2", "MC_CPU_HAS_BMI2"),
    [KEY_CPU_HAS_POPCNT] = KEY_BOOL("cpu.simd.popcnt", "MC_CPU_HAS_POPCNT"),
    [KEY_CPU_HAS_LZCNT] = KEY_BOOL("cpu.simd.lzcnt", "MC_CPU_HAS_LZCNT"),
    [KEY_CPU_HAS_F16C] = KEY_BOOL("cpu.simd.f16c", "MC_CPU_HAS_F16C"),
    [KEY_CPU_HAS_AVX512] = KEY_BOOL("cpu.simd.avx512", "MC_CPU_HAS_AVX512"),
    /* Plan section 2 has one main thread and one sim thread. threads.gpu may be 0 only with gpu.enabled false. */
    [KEY_THREADS_MAIN] = KEY_INT("threads.main", "MC_THREADS_MAIN", 1, 1),
    [KEY_THREADS_SIM] = KEY_INT("threads.sim", "MC_THREADS_SIM", 1, 1),
    [KEY_THREADS_GPU] = KEY_INT("threads.gpu", "MC_THREADS_GPU", 0, 1),
    [KEY_THREADS_WORKERS] = KEY_INT("threads.workers", "MC_THREADS_WORKERS", 1, 4096),
    [KEY_THREADS_AUDIO] = KEY_INT("threads.audio", "MC_THREADS_AUDIO", 0, 1),
    [KEY_THREADS_RESERVED_OS] = KEY_INT("threads.reserved_os", "MC_THREADS_RESERVED_OS", 0, 4096),
    [KEY_THREADS_PIN_PHYSICAL_FIRST] = KEY_BOOL("threads.pin_physical_first", "MC_THREADS_PIN_PHYSICAL_FIRST"),
    [KEY_GPU_ENABLED] = KEY_BOOL("gpu.enabled", "MC_GPU_ENABLED"),
    [KEY_GPU_API] = KEY_CHOICE("gpu.api", "MC_GPU_API", "cuda"),
    [KEY_GPU_MODEL] = KEY_TEXT("gpu.model", "MC_GPU_MODEL"),
    [KEY_GPU_ARCHITECTURE] = KEY_WORD("gpu.architecture", "MC_GPU_ARCHITECTURE"),
    [KEY_GPU_VRAM_MIB] = KEY_INT("gpu.vram_mib", "MC_GPU_VRAM_MIB", 1, MACHINE_INT_ANY),
    [KEY_GPU_BUDGET_VOXELS_MIB] = KEY_INT("gpu.budget.voxels_mib", "MC_GPU_BUDGET_VOXELS_MIB", 1, MACHINE_INT_ANY),
    [KEY_GPU_BUDGET_TEXTURES_MIB] =
        KEY_INT("gpu.budget.textures_mib", "MC_GPU_BUDGET_TEXTURES_MIB", 1, MACHINE_INT_ANY),
    [KEY_GPU_BUDGET_FRAMES_MIB] = KEY_INT("gpu.budget.frames_mib", "MC_GPU_BUDGET_FRAMES_MIB", 1, MACHINE_INT_ANY),
    [KEY_DISPLAY_BACKEND] = KEY_CHOICE("display.backend", "MC_DISPLAY_BACKEND", "kms"),
    [KEY_DISPLAY_DEVICE] = KEY_TEXT("display.device", "MC_DISPLAY_DEVICE"),
    [KEY_DISPLAY_CONNECTOR] = KEY_TEXT("display.connector", "MC_DISPLAY_CONNECTOR"),
    [KEY_DISPLAY_WIDTH] = KEY_INT("display.width", "MC_DISPLAY_WIDTH", 1, 65535),
    [KEY_DISPLAY_HEIGHT] = KEY_INT("display.height", "MC_DISPLAY_HEIGHT", 1, 65535),
    [KEY_DISPLAY_REFRESH_HZ] = KEY_INT("display.refresh_hz", "MC_DISPLAY_REFRESH_HZ", 1, 10000),
    [KEY_MEMORY_HUGE_PAGE_BYTES] =
        KEY_INT("memory.huge_page_bytes", "MC_MEMORY_HUGE_PAGE_BYTES", 4096, MACHINE_INT_ANY),
    [KEY_MEMORY_PROCESS_BUDGET_MIB] =
        KEY_INT("memory.process_budget_mib", "MC_MEMORY_PROCESS_BUDGET_MIB", 1, MACHINE_INT_ANY),
    [KEY_GAME_RENDER_DISTANCE_DEFAULT] =
        KEY_INT("game.render_distance_default", "MC_GAME_RENDER_DISTANCE_DEFAULT", 2, 4096),
    [KEY_GAME_RENDER_DISTANCE_MAX] = KEY_INT("game.render_distance_max", "MC_GAME_RENDER_DISTANCE_MAX", 2, 4096),
    [KEY_GAME_TICK_HZ] = KEY_INT("game.tick_hz", "MC_GAME_TICK_HZ", 1, 1000),
    [KEY_WORLD_Z_MIN] = KEY_INT("world.z_min", "MC_WORLD_Z_MIN", -1048576, 1048576),
    [KEY_WORLD_Z_SEA] = KEY_INT("world.z_sea", "MC_WORLD_Z_SEA", -1048576, 1048576),
    [KEY_WORLD_Z_MAX] = KEY_INT("world.z_max", "MC_WORLD_Z_MAX", -1048576, 1048576),
    [KEY_WORLD_CHUNK_SIZE] = KEY_INT("world.chunk_size", "MC_WORLD_CHUNK_SIZE", 1, 4096),
    [KEY_WORLD_SECTION_SIZE] = KEY_INT("world.section_size", "MC_WORLD_SECTION_SIZE", 1, 4096),
};

#define MACHINE_GENERATED_NOTE "Generated by compile/src/machine_config from config/machine.yaml. Do not edit."

enum : uint32_t { MACHINE_FORMAT_COUNT = 4u };

static const machine_format_t machine_formats[MACHINE_FORMAT_COUNT] = {
    {.file_name = "index.h",
     .preamble = "/* " MACHINE_GENERATED_NOTE " */\n#ifndef MC_MACHINE_INDEX_H\n#define MC_MACHINE_INDEX_H\n\n",
     .closing = "\n#endif\n",
     .line_before = "#define ",
     .line_middle = " ",
     .line_after = "\n",
     .wrap_negative = 1,
     ._pad0 = {0}},
    {.file_name = "index.hu",
     .preamble = "/* " MACHINE_GENERATED_NOTE " */\n#ifndef MC_MACHINE_INDEX_HU\n#define MC_MACHINE_INDEX_HU\n\n",
     .closing = "\n#endif\n",
     .line_before = "#define ",
     .line_middle = " ",
     .line_after = "\n",
     .wrap_negative = 1,
     ._pad0 = {0}},
    {.file_name = "index.inc",
     .preamble = "; " MACHINE_GENERATED_NOTE "\n%ifndef MC_MACHINE_INDEX_INC\n%define MC_MACHINE_INDEX_INC\n\n",
     .closing = "\n%endif\n",
     .line_before = "%define ",
     .line_middle = " ",
     .line_after = "\n",
     .wrap_negative = 1,
     ._pad0 = {0}},
    {.file_name = "index.cmake",
     .preamble = "# " MACHINE_GENERATED_NOTE "\n\n",
     .closing = "",
     .line_before = "set(",
     .line_middle = " ",
     .line_after = ")\n",
     .wrap_negative = 0,
     ._pad0 = {0}},
};

static bool machine_is_power_of_two(int64_t value) {
    return value > 0 && (value & (value - 1)) == 0;
}

static const machine_key_t *machine_find_key(const char *path, uint32_t length) {
    for (uint32_t i = 0; i < KEY_COUNT; i++) {
        if (strncmp(machine_keys[i].path, path, length) == 0 && machine_keys[i].path[length] == '\0') {
            return &machine_keys[i];
        }
    }
    return nullptr;
}

/* True if some key lives under this path, which makes it a section and not a value. */
static bool machine_is_section(const char *path, uint32_t length) {
    for (uint32_t i = 0; i < KEY_COUNT; i++) {
        if (strncmp(machine_keys[i].path, path, length) == 0 && machine_keys[i].path[length] == '.') {
            return true;
        }
    }
    return false;
}

/*
 * Refuses any key the table does not list. Walks with a small fixed stack: only sections are entered, and the table
 * is at most two sections deep, so MACHINE_CONFIG_MAX_NESTING cannot be reached by anything the yaml says.
 */
static bool machine_check_no_unknown_keys(const yaml_document_t *document, yaml_error_t *error) {
    const yaml_node_t *cursor[MACHINE_CONFIG_MAX_NESTING] = {yaml_first_child(document, yaml_root(document))};
    uint32_t base_length[MACHINE_CONFIG_MAX_NESTING] = {0u};
    char path[MACHINE_CONFIG_PATH_BYTES];
    uint32_t depth = 1u;

    while (depth > 0u) {
        const yaml_node_t *node = cursor[depth - 1u];
        if (node == nullptr) {
            depth--;
            continue;
        }
        cursor[depth - 1u] = yaml_next_sibling(document, node);

        uint32_t length = base_length[depth - 1u];
        if ((uint64_t)length + 1u + node->key_length + 1u > sizeof(path)) {
            return yaml_fail_at_node(node, error, "this key is too long to be one this tool knows");
        }
        if (length > 0u) {
            path[length] = '.';
            length++;
        }
        memcpy(path + length, node->borrowed_key, node->key_length);
        length += node->key_length;
        path[length] = '\0';

        if (machine_is_section(path, length)) {
            if (node->kind != YAML_KIND_MAP) {
                return yaml_fail_at_node(node, error, "'%s' must be a mapping", path);
            }
            if (depth >= MACHINE_CONFIG_MAX_NESTING) {
                return yaml_fail_at_node(node, error, "'%s' is nested too deeply", path);
            }
            cursor[depth] = yaml_first_child(document, node);
            base_length[depth] = length;
            depth++;
        } else if (machine_find_key(path, length) == nullptr) {
            return yaml_fail_at_node(node, error, "unknown key '%s'", path);
        }
    }
    return true;
}

static bool machine_text_is_clean(const uint8_t *text, uint32_t length, bool word_only, uint8_t *out_bad) {
    for (uint32_t i = 0; i < length; i++) {
        const uint8_t c = text[i];
        bool ok;
        if (word_only) {
            ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                 c == '-';
        } else {
            /* Anything here is plain text inside "..." in C, NASM and CMake, so nothing needs escaping. */
            ok = c >= 0x20u && c <= 0x7Eu && c != '"' && c != '\\' && c != '`' && c != '$' && c != ';';
        }
        if (!ok) {
            *out_bad = c;
            return false;
        }
    }
    return true;
}

/* The length of the first word in a comma-separated list. Returns whether another word follows it. */
static bool machine_choice_length(const char *choice, uint32_t *out_length) {
    const char *end = strchr(choice, ',');
    *out_length = (uint32_t)(end != nullptr ? (uint64_t)(end - choice) : strlen(choice));
    return end != nullptr;
}

static bool machine_read_text_value(const machine_key_t *key, const yaml_node_t *node, machine_value_t *value,
                                    yaml_error_t *error) {
    if (!yaml_get_text(node, &value->borrowed_text, &value->text_length, error)) {
        return false;
    }
    if (value->text_length == 0u) {
        return yaml_fail_at_node(node, error, "%s may not be empty", key->path);
    }
    uint8_t bad = 0u;
    if (!machine_text_is_clean(value->borrowed_text, value->text_length, key->kind != MACHINE_KIND_TEXT, &bad)) {
        return yaml_fail_at_node(node, error, "%s holds byte 0x%02x, which this key does not allow", key->path, bad);
    }
    if (key->kind == MACHINE_KIND_CHOICE) {
        const char *choice = key->choices;
        bool more = true;
        while (more) {
            uint32_t length = 0u;
            more = machine_choice_length(choice, &length);
            if (length == value->text_length && memcmp(choice, value->borrowed_text, length) == 0) {
                return true;
            }
            choice += length + 1u;
        }
        return yaml_fail_at_node(node, error, "%s must be one of: %s", key->path, key->choices);
    }
    return true;
}

static bool machine_read_values(const yaml_document_t *document, machine_value_t *values, yaml_error_t *error) {
    const yaml_node_t *root = yaml_root(document);
    for (uint32_t i = 0; i < KEY_COUNT; i++) {
        const machine_key_t *key = &machine_keys[i];
        const yaml_node_t *node = yaml_path_get(document, root, key->path);
        if (node == nullptr) {
            return yaml_fail_at_node(root, error, "missing key '%s'", key->path);
        }
        values[i].borrowed_node = node;
        if (key->kind == MACHINE_KIND_BOOL) {
            bool truth = false;
            if (!yaml_get_bool(node, &truth, error)) {
                return false;
            }
            values[i].number = truth ? 1 : 0;
        } else if (key->kind == MACHINE_KIND_INT) {
            if (!yaml_get_int64(node, &values[i].number, error)) {
                return false;
            }
            if (values[i].number < key->min || values[i].number > key->max) {
                return yaml_fail_at_node(node, error, "%s is %" PRId64 ", and must be %" PRId64 " to %" PRId64,
                                         key->path, values[i].number, key->min, key->max);
            }
        } else if (!machine_read_text_value(key, node, &values[i], error)) {
            return false;
        }
    }
    return true;
}

/* Rules that span keys. Each names both sides, because the line it points at is only one of them. */
static bool machine_check_consistency(const machine_value_t *values, yaml_error_t *error) {
#define NUMBER(id) (values[id].number)
#define NODE(id) (values[id].borrowed_node)
    /* Threads are pinned one to a logical CPU (plan section 2), so asking for more than exist cannot work. */
    const int64_t threads = NUMBER(KEY_THREADS_MAIN) + NUMBER(KEY_THREADS_SIM) + NUMBER(KEY_THREADS_GPU) +
                            NUMBER(KEY_THREADS_WORKERS) + NUMBER(KEY_THREADS_AUDIO) + NUMBER(KEY_THREADS_RESERVED_OS);
    if (threads > NUMBER(KEY_CPU_LOGICAL_THREADS)) {
        return yaml_fail_at_node(NODE(KEY_THREADS_WORKERS), error,
                                 "threads add up to %" PRId64 ", past cpu.logical_threads of %" PRId64, threads,
                                 NUMBER(KEY_CPU_LOGICAL_THREADS));
    }
    if (NUMBER(KEY_CPU_PHYSICAL_CORES) > NUMBER(KEY_CPU_LOGICAL_THREADS)) {
        return yaml_fail_at_node(NODE(KEY_CPU_PHYSICAL_CORES), error, "cpu.physical_cores is past cpu.logical_threads");
    }
    if (!machine_is_power_of_two(NUMBER(KEY_CPU_CACHE_LINE_BYTES))) {
        return yaml_fail_at_node(NODE(KEY_CPU_CACHE_LINE_BYTES), error, "cpu.cache_line_bytes must be a power of two");
    }
    if (!machine_is_power_of_two(NUMBER(KEY_MEMORY_HUGE_PAGE_BYTES))) {
        return yaml_fail_at_node(NODE(KEY_MEMORY_HUGE_PAGE_BYTES), error,
                                 "memory.huge_page_bytes must be a power of two");
    }
    /* Each SIMD level is encoded on top of the one it names here, so the build flags cannot have one alone. */
    if (NUMBER(KEY_CPU_HAS_AVX) != 0 && NUMBER(KEY_CPU_HAS_SSE42) == 0) {
        return yaml_fail_at_node(NODE(KEY_CPU_HAS_AVX), error, "cpu.simd.avx needs cpu.simd.sse42");
    }
    if (NUMBER(KEY_CPU_HAS_AVX2) != 0 && NUMBER(KEY_CPU_HAS_AVX) == 0) {
        return yaml_fail_at_node(NODE(KEY_CPU_HAS_AVX2), error, "cpu.simd.avx2 needs cpu.simd.avx");
    }
    if (NUMBER(KEY_CPU_HAS_FMA3) != 0 && NUMBER(KEY_CPU_HAS_AVX) == 0) {
        return yaml_fail_at_node(NODE(KEY_CPU_HAS_FMA3), error, "cpu.simd.fma3 needs cpu.simd.avx");
    }
    if (NUMBER(KEY_CPU_HAS_F16C) != 0 && NUMBER(KEY_CPU_HAS_AVX) == 0) {
        return yaml_fail_at_node(NODE(KEY_CPU_HAS_F16C), error, "cpu.simd.f16c needs cpu.simd.avx");
    }
    if (NUMBER(KEY_CPU_HAS_AVX512) != 0 && NUMBER(KEY_CPU_HAS_AVX2) == 0) {
        return yaml_fail_at_node(NODE(KEY_CPU_HAS_AVX512), error, "cpu.simd.avx512 needs cpu.simd.avx2");
    }
    const int64_t budget =
        NUMBER(KEY_GPU_BUDGET_VOXELS_MIB) + NUMBER(KEY_GPU_BUDGET_TEXTURES_MIB) + NUMBER(KEY_GPU_BUDGET_FRAMES_MIB);
    if (budget > NUMBER(KEY_GPU_VRAM_MIB)) {
        return yaml_fail_at_node(NODE(KEY_GPU_BUDGET_VOXELS_MIB), error,
                                 "gpu.budget adds up to %" PRId64 " MiB, past gpu.vram_mib of %" PRId64, budget,
                                 NUMBER(KEY_GPU_VRAM_MIB));
    }
    /* game starts its gpu thread whenever a world opens, and asking where to pin one there is no room for stops the program. */
    if (NUMBER(KEY_GPU_ENABLED) != 0 && NUMBER(KEY_THREADS_GPU) == 0) {
        return yaml_fail_at_node(NODE(KEY_THREADS_GPU), error, "threads.gpu must be 1 while gpu.enabled is true");
    }
    if (NUMBER(KEY_GAME_RENDER_DISTANCE_DEFAULT) > NUMBER(KEY_GAME_RENDER_DISTANCE_MAX)) {
        return yaml_fail_at_node(NODE(KEY_GAME_RENDER_DISTANCE_DEFAULT), error,
                                 "game.render_distance_default is past game.render_distance_max");
    }
    if (NUMBER(KEY_WORLD_Z_MIN) >= NUMBER(KEY_WORLD_Z_MAX)) {
        return yaml_fail_at_node(NODE(KEY_WORLD_Z_MIN), error, "world.z_min must be below world.z_max");
    }
    if (NUMBER(KEY_WORLD_Z_SEA) < NUMBER(KEY_WORLD_Z_MIN) || NUMBER(KEY_WORLD_Z_SEA) > NUMBER(KEY_WORLD_Z_MAX)) {
        return yaml_fail_at_node(NODE(KEY_WORLD_Z_SEA), error, "world.z_sea must be from world.z_min to world.z_max");
    }
    /* Block coordinates split into chunk and local by shifting and masking, which only works on powers of two. */
    if (!machine_is_power_of_two(NUMBER(KEY_WORLD_CHUNK_SIZE))) {
        return yaml_fail_at_node(NODE(KEY_WORLD_CHUNK_SIZE), error, "world.chunk_size must be a power of two");
    }
    if (!machine_is_power_of_two(NUMBER(KEY_WORLD_SECTION_SIZE))) {
        return yaml_fail_at_node(NODE(KEY_WORLD_SECTION_SIZE), error, "world.section_size must be a power of two");
    }
#undef NUMBER
#undef NODE
    return true;
}

static void machine_output_bytes(machine_output_t *output, const void *bytes, uint64_t length) {
    if (output->length + length > MACHINE_CONFIG_OUTPUT_BYTES) {
        output->overflowed = 1u;
        return;
    }
    memcpy(output->borrowed_bytes + output->length, bytes, length);
    output->length += length;
}

static void machine_output_text(machine_output_t *output, const char *text) {
    machine_output_bytes(output, text, strlen(text));
}

static void machine_output_number(machine_output_t *output, const machine_format_t *format, int64_t number) {
    char digits[32];
    int32_t length;
    if (number < 0 && format->wrap_negative != 0u) {
        length = snprintf(digits, sizeof(digits), "(%" PRId64 ")", number);
    } else {
        length = snprintf(digits, sizeof(digits), "%" PRId64, number);
    }
    machine_output_bytes(output, digits, (uint64_t)length);
}

/* The key's macro plus _<WORD>, upper-cased, set to whether this is the chosen word. */
static void machine_output_choices(machine_output_t *output, const machine_format_t *format, const machine_key_t *key,
                                   const machine_value_t *value) {
    const char *choice = key->choices;
    bool more = true;
    while (more) {
        uint32_t length = 0u;
        more = machine_choice_length(choice, &length);
        machine_output_text(output, format->line_before);
        machine_output_text(output, key->macro);
        machine_output_text(output, "_");
        for (uint32_t i = 0; i < length; i++) {
            uint8_t letter = (uint8_t)choice[i];
            if (letter >= 'a' && letter <= 'z') {
                letter = (uint8_t)(letter - 'a' + 'A');
            }
            machine_output_bytes(output, &letter, 1u);
        }
        machine_output_text(output, format->line_middle);
        const bool chosen = length == value->text_length && memcmp(choice, value->borrowed_text, length) == 0;
        machine_output_text(output, chosen ? "1" : "0");
        machine_output_text(output, format->line_after);
        choice += length + 1u;
    }
}

static void machine_build_output(machine_output_t *output, const machine_format_t *format,
                                 const machine_value_t *values) {
    machine_output_text(output, format->preamble);
    for (uint32_t i = 0; i < KEY_COUNT; i++) {
        const machine_key_t *key = &machine_keys[i];
        machine_output_text(output, format->line_before);
        machine_output_text(output, key->macro);
        machine_output_text(output, format->line_middle);
        if (key->kind == MACHINE_KIND_BOOL || key->kind == MACHINE_KIND_INT) {
            machine_output_number(output, format, values[i].number);
        } else {
            machine_output_text(output, "\"");
            machine_output_bytes(output, values[i].borrowed_text, values[i].text_length);
            machine_output_text(output, "\"");
        }
        machine_output_text(output, format->line_after);
        if (key->kind == MACHINE_KIND_CHOICE) {
            machine_output_choices(output, format, key, &values[i]);
        }
    }
    machine_output_text(output, format->closing);
}

static bool machine_write_all(int32_t descriptor, const uint8_t *bytes, uint64_t length) {
    uint64_t done = 0u;
    while (done < length) {
        const int64_t wrote = write(descriptor, bytes + done, length - done);
        if (wrote <= 0) {
            return false;
        }
        done += (uint64_t)wrote;
    }
    return true;
}

/* True if the file exists and already holds exactly these bytes. Any failure to read just means "write it". */
static bool machine_file_holds(const char *path, const uint8_t *bytes, uint64_t length, uint8_t *scratch) {
    const int32_t descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return false;
    }
    uint64_t total = 0u;
    bool ok = true;
    while (total <= MACHINE_CONFIG_OUTPUT_BYTES) {
        const int64_t got = read(descriptor, scratch + total, MACHINE_CONFIG_OUTPUT_BYTES + 1u - total);
        if (got < 0) {
            ok = false;
        }
        if (got <= 0) {
            break;
        }
        total += (uint64_t)got;
    }
    close(descriptor);
    return ok && total == length && memcmp(scratch, bytes, length) == 0;
}

/* Through a temporary and a rename, so a concurrent reader or a crash never sees half a header. */
static bool machine_write_output(const char *directory, const char *file_name, const machine_output_t *output,
                                 uint8_t *scratch) {
    char path[4096];
    char temporary[4096 + 32];
    const int32_t path_length = snprintf(path, sizeof(path), "%s/%s", directory, file_name);
    if (path_length < 0 || (uint64_t)path_length >= sizeof(path)) {
        fprintf(stderr, "machine_config: output path is too long: %s\n", directory);
        return false;
    }
    if (machine_file_holds(path, output->borrowed_bytes, output->length, scratch)) {
        return true;
    }
    snprintf(temporary, sizeof(temporary), "%s.tmp.%" PRId64, path, (int64_t)getpid());
    const int32_t descriptor = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (descriptor < 0) {
        fprintf(stderr, "machine_config: cannot create %s: %s\n", temporary, strerror(errno));
        return false;
    }
    const bool wrote = machine_write_all(descriptor, output->borrowed_bytes, output->length);
    const bool closed = close(descriptor) == 0;
    if (!wrote || !closed || rename(temporary, path) != 0) {
        fprintf(stderr, "machine_config: cannot write %s: %s\n", path, strerror(errno));
        unlink(temporary);
        return false;
    }
    return true;
}

/* Sizes the region from the file, maps it, and reads the file in. Reports its own failures. */
static bool machine_config_init(machine_config_t *config, const char *input_path) {
    memset(config, 0, sizeof(*config));
    bool ok = false;
    const int32_t descriptor = open(input_path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        fprintf(stderr, "machine_config: cannot open %s: %s\n", input_path, strerror(errno));
        return false;
    }
    struct stat status;
    if (fstat(descriptor, &status) != 0) {
        fprintf(stderr, "machine_config: cannot stat %s: %s\n", input_path, strerror(errno));
        goto cleanup;
    }
    if (!S_ISREG(status.st_mode) || (uint64_t)status.st_size > MACHINE_CONFIG_MAX_INPUT_BYTES) {
        fprintf(stderr, "machine_config: %s must be a regular file of at most %u bytes\n", input_path,
                MACHINE_CONFIG_MAX_INPUT_BYTES);
        goto cleanup;
    }
    const uint64_t text_bytes = (uint64_t)status.st_size;
    /* Text length + 1 nodes and text length string bytes can never run out: see yaml_storage_t. */
    const uint64_t node_count = text_bytes + 1u;
    const uint64_t node_bytes = node_count * sizeof(yaml_node_t);
    const uint64_t scratch_bytes = MACHINE_CONFIG_OUTPUT_BYTES + 1u;
    const uint64_t output_bytes = (uint64_t)MACHINE_FORMAT_COUNT * MACHINE_CONFIG_OUTPUT_BYTES;
    const uint64_t region_bytes = node_bytes + (text_bytes + 1u) * 2u + scratch_bytes + output_bytes;
    void *region = mmap(nullptr, region_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED) {
        fprintf(stderr, "machine_config: cannot map %" PRIu64 " bytes: %s\n", region_bytes, strerror(errno));
        goto cleanup;
    }
    /* Nodes come first because they alone need alignment, and a mapping starts page-aligned. */
    config->owned_region = region;
    config->region_bytes = region_bytes;
    config->storage.nodes = region;
    config->storage.node_capacity = (uint32_t)node_count;
    config->borrowed_text = config->owned_region + node_bytes;
    config->storage.strings = config->borrowed_text + text_bytes + 1u;
    config->storage.string_capacity = (uint32_t)text_bytes + 1u;
    config->borrowed_scratch = config->storage.strings + text_bytes + 1u;
    config->borrowed_outputs = config->borrowed_scratch + scratch_bytes;

    while (config->text_length < text_bytes) {
        const int64_t got =
            read(descriptor, config->borrowed_text + config->text_length, text_bytes - config->text_length);
        if (got < 0) {
            fprintf(stderr, "machine_config: cannot read %s: %s\n", input_path, strerror(errno));
            goto cleanup;
        }
        if (got == 0) {
            break; /* It shrank while being read. The parser judges what is there. */
        }
        config->text_length += (uint64_t)got;
    }
    ok = true;

cleanup:
    close(descriptor);
    return ok;
}

static void machine_config_deinit(machine_config_t *config) {
    if (config->owned_region != nullptr) {
        munmap(config->owned_region, config->region_bytes);
        config->owned_region = nullptr;
    }
}

/* Checks everything and only then writes. Returns the exit status. */
static int32_t machine_config_run(machine_config_t *config, const char *input_path, const char *output_directory) {
    yaml_document_t document;
    yaml_error_t error;
    machine_value_t values[KEY_COUNT] = {0};

    bool valid = yaml_parse(&document, config->borrowed_text, config->text_length, config->storage, &error);
    if (valid && yaml_root(&document)->kind != YAML_KIND_MAP) {
        valid = yaml_fail_at_node(yaml_root(&document), &error, "machine.yaml must be a mapping");
    }
    valid = valid && machine_check_no_unknown_keys(&document, &error);
    valid = valid && machine_read_values(&document, values, &error);
    valid = valid && machine_check_consistency(values, &error);
    if (!valid) {
        fprintf(stderr, "%s:%" PRIu32 ":%" PRIu32 ": %s\n", input_path, error.line, error.column, error.message);
        return MACHINE_CONFIG_EXIT_BAD_CONFIG;
    }

    machine_output_t outputs[MACHINE_FORMAT_COUNT];
    for (uint32_t i = 0; i < MACHINE_FORMAT_COUNT; i++) {
        outputs[i] = (machine_output_t){
            .borrowed_bytes = config->borrowed_outputs + (uint64_t)i * MACHINE_CONFIG_OUTPUT_BYTES,
            .length = 0u,
            .overflowed = 0u,
            ._pad0 = {0},
        };
        machine_build_output(&outputs[i], &machine_formats[i], values);
        if (outputs[i].overflowed != 0u) {
            fprintf(stderr, "machine_config: %s does not fit in %u bytes\n", machine_formats[i].file_name,
                    MACHINE_CONFIG_OUTPUT_BYTES);
            return MACHINE_CONFIG_EXIT_CANNOT_RUN;
        }
    }

    if (mkdir(output_directory, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "machine_config: cannot create %s: %s\n", output_directory, strerror(errno));
        return MACHINE_CONFIG_EXIT_CANNOT_RUN;
    }
    for (uint32_t i = 0; i < MACHINE_FORMAT_COUNT; i++) {
        if (!machine_write_output(output_directory, machine_formats[i].file_name, &outputs[i],
                                  config->borrowed_scratch)) {
            return MACHINE_CONFIG_EXIT_CANNOT_RUN;
        }
    }
    return MACHINE_CONFIG_EXIT_OK;
}

int32_t main(int32_t argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: machine_config <machine.yaml> <output directory>\n");
        return MACHINE_CONFIG_EXIT_CANNOT_RUN;
    }
    machine_config_t config;
    int32_t status = MACHINE_CONFIG_EXIT_CANNOT_RUN;
    if (machine_config_init(&config, argv[1])) {
        status = machine_config_run(&config, argv[1], argv[2]);
    }
    machine_config_deinit(&config);
    return status;
}
