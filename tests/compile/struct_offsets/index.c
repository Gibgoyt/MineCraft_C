/*
 * ctest compile_struct_offsets. See README.md. Arguments come from test.cmake:
 *   tool work_dir generated_dir source_dir gcc nasm
 */
#include "harness/index.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum : uint64_t {
    SO_PATH_BYTES = 1024u,
    SO_FILE_BYTES = 1u << 18,
};
enum : uint32_t {
    SO_TIMEOUT_TOOL_MS = 20000u,
    SO_TIMEOUT_COMPILER_MS = 120000u,
};

typedef struct so_bad_case_t {
    const char *name;
    const char *text;
    const char *expected;
    uint32_t line;
    uint8_t _pad0[4];
} so_bad_case_t;
static_assert(sizeof(so_bad_case_t) == 32);

/* The numbers on the right are what the layout must come to. */
static const char SO_GOOD_HEADER[] = "/* Comments, with typedef struct in them { */\n"
                                     "#ifndef GOOD_H\n"
                                     "#define GOOD_H\n"
                                     "#define NOT_A_STRUCT(x) \\\n"
                                     "    typedef struct broken_t { nonsense; } broken_t\n"
                                     "#include \"types/index.h\"\n"
                                     "#include <stdint.h>\n"
                                     "#include <stddef.h>\n"
                                     "enum : uint32_t { SOME_NUMBER = 7u };\n"
                                     "typedef float32_t a_float_t; // typedef struct {\n"
                                     "static const char so_text[] = \"typedef struct { not code\";\n"
                                     "typedef struct forward_t forward_t;\n"
                                     "typedef struct inner_t {\n"
                                     "    float32_t x, y; /* 0, 4 */\n"
                                     "    uint16_t tag;   /* 8 */\n"
                                     "    uint8_t _pad0[2];\n"
                                     "} inner_t;\n"
                                     "typedef struct outer_t {\n"
                                     "    alignas(32) float32_t m[16];    /* 0 */\n"
                                     "    inner_t first;                  /* 64 */\n"
                                     "    inner_t many[3];                /* 76 */\n"
                                     "    uint8_t bytes[2][0x10u];        /* 112 */\n"
                                     "    const char *const borrowed_name;/* 144 */\n"
                                     "    forward_t *borrowed_next;       /* 152 */\n"
                                     "    uint64_t count;                 /* 160 */\n"
                                     "    int32_t delta;                  /* 168 */\n"
                                     "    uint8_t _pad1[20];\n"
                                     "} outer_t;\n"
                                     "#endif\n";

static const so_bad_case_t so_bad_cases[] = {
    {"unknown_type", "\n\ntypedef struct a_t {\n    mystery_t value;\n} a_t;\n", "unknown type 'mystery_t'", 4u, {0}},
    {"bitfield", "typedef struct a_t {\n    uint32_t low : 4;\n    uint32_t high : 28;\n} a_t;\n", "bitfield", 2u, {0}},
    {"function_pointer", "typedef struct a_t {\n\n    void (*call)(void);\n} a_t;\n", "function pointer", 3u, {0}},
    {"named_dimension", "typedef struct a_t {\n    uint8_t bytes[SOME_COUNT];\n} a_t;\n", "plain number", 2u, {0}},
    {"zero_dimension", "typedef struct a_t {\n    uint8_t bytes[0];\n} a_t;\n", "from 1 to", 2u, {0}},
    {"interior_padding", "typedef struct a_t {\n    uint8_t tag;\n    uint32_t value;\n} a_t;\n", "padding", 3u, {0}},
    {"trailing_padding",
     "typedef struct a_t {\n    uint32_t value;\n    uint8_t tag;\n} a_t;\n",
     "trailing padding",
     4u,
     {0}},
    {"nested_union",
     "typedef struct a_t {\n    union { uint32_t a; float32_t b; } value;\n} a_t;\n",
     "'union'",
     2u,
     {0}},
    {"nested_struct", "typedef struct a_t {\n    struct b_t value;\n} a_t;\n", "'struct'", 2u, {0}},
    {"bad_alignas", "typedef struct a_t {\n    alignas(3) uint32_t value;\n} a_t;\n", "not a power of two", 2u, {0}},
    {"named_alignas",
     "typedef struct a_t {\n    alignas(CACHE_LINE) uint32_t value;\n} a_t;\n",
     "plain number",
     2u,
     {0}},
    {"empty", "typedef struct a_t {\n} a_t;\n", "no fields", 2u, {0}},
    {"cut_short", "typedef struct a_t {\n    uint32_t value;\n", "ends inside a struct", 3u, {0}},
    {"twice",
     "typedef struct a_t {\n    uint32_t value;\n} a_t;\ntypedef struct b_t {\n    uint32_t value;\n} a_t;\n",
     "already read",
     6u,
     {0}},
    {"no_typedef_name", "typedef struct a_t {\n    uint32_t value;\n};\n", "expected a name", 3u, {0}},
};

static const char *so_arg(const test_context_t *t, int32_t index) {
    return t->borrowed_args[index];
}

static const char *so_tool(const test_context_t *t) {
    return so_arg(t, 0);
}
static const char *so_work(const test_context_t *t) {
    return so_arg(t, 1);
}
static const char *so_generated(const test_context_t *t) {
    return so_arg(t, 2);
}
static const char *so_source(const test_context_t *t) {
    return so_arg(t, 3);
}
static const char *so_gcc(const test_context_t *t) {
    return so_arg(t, 4);
}
static const char *so_nasm(const test_context_t *t) {
    return so_arg(t, 5);
}

/* Makes each directory on the way to path. */
static void so_make_directories(test_context_t *t, const char *path) {
    char partial[SO_PATH_BYTES];
    const uint64_t length = strlen(path);
    REQUIRE(length < sizeof(partial));
    memcpy(partial, path, length + 1u);
    for (uint64_t i = 1; i <= length; i++) {
        if (partial[i] == '/' || partial[i] == '\0') {
            const char saved = partial[i];
            partial[i] = '\0';
            CHECK(mkdir(partial, 0755) == 0 || errno == EEXIST);
            partial[i] = saved;
        }
    }
}

static const char *so_join(test_context_t *t, const char *directory, const char *name) {
    char *joined = test_alloc(t, SO_PATH_BYTES);
    const int32_t wrote = snprintf(joined, SO_PATH_BYTES, "%s/%s", directory, name);
    test_check(t, wrote > 0 && (uint64_t)wrote < SO_PATH_BYTES, __FILE__, __LINE__, "the path fits");
    return joined;
}

static void so_write_file(test_context_t *t, const char *path, const char *text) {
    const int32_t fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    REQUIRE(fd >= 0);
    const uint64_t length = strlen(text);
    CHECK_EQ_U64((uint64_t)write(fd, text, length), length);
    close(fd);
}

/* Nullptr if it cannot be read, as a missing file. */
static char *so_read_file(test_context_t *t, const char *path) {
    const int32_t fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return nullptr;
    }
    char *text = test_alloc(t, SO_FILE_BYTES);
    const int64_t length = read(fd, text, SO_FILE_BYTES - 1u);
    close(fd);
    return length >= 0 ? text : nullptr;
}

/* A directory of its own for each test, wiped of what an earlier run of this test wrote. */
static const char *so_case_dir(test_context_t *t, const char *name) {
    const char *directory = so_join(t, so_work(t), name);
    so_make_directories(t, so_join(t, directory, "include/fixture"));
    (void)unlink(so_join(t, directory, "out/index.inc"));
    (void)unlink(so_join(t, directory, "out/index.h"));
    return directory;
}

/* Writes the fixture and runs the tool on it. */
static void so_run_tool(test_context_t *t, const char *directory, const char *text, test_process_t *out) {
    const char *header = so_join(t, directory, "include/fixture/index.h");
    so_write_file(t, header, text);
    const char *output = so_join(t, directory, "out");
    char *const argv[] = {(char *)so_tool(t), (char *)output, (char *)header, nullptr};
    test_process_run(t, argv, SO_TIMEOUT_TOOL_MS, out);
}

TEST(lays_out_a_header_as_the_compiler_would) {
    const char *directory = so_case_dir(t, "good");
    test_process_t process;
    so_run_tool(t, directory, SO_GOOD_HEADER, &process);
    CHECK_EQ_I64(process.exit_code, 0);
    const char *inc = so_read_file(t, so_join(t, directory, "out/index.inc"));
    REQUIRE(inc != nullptr);
    static const char *const lines[] = {
        "%define INNER_T_SIZEOF 12\n",
        "%define INNER_T_ALIGNOF 4\n",
        "%define INNER_T_X 0\n",
        "%define INNER_T_Y 4\n",
        "%define INNER_T_TAG 8\n",
        "%define OUTER_T_SIZEOF 192\n",
        "%define OUTER_T_ALIGNOF 32\n",
        "%define OUTER_T_M 0\n",
        "%define OUTER_T_FIRST 64\n",
        "%define OUTER_T_MANY 76\n",
        "%define OUTER_T_BYTES 112\n",
        "%define OUTER_T_BORROWED_NAME 144\n",
        "%define OUTER_T_BORROWED_NEXT 152\n",
        "%define OUTER_T_COUNT 160\n",
        "%define OUTER_T_DELTA 168\n",
    };
    for (uint64_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        CHECK_CONTAINS(inc, lines[i]);
    }
    CHECK(strstr(inc, "_PAD") == nullptr);
    CHECK(strstr(inc, "BROKEN") == nullptr);
    CHECK(strstr(inc, "FORWARD") == nullptr);
}

TEST(gcc_agrees_with_every_number_and_nasm_reads_them) {
    const char *directory = so_case_dir(t, "compilers");
    test_process_t process;
    so_run_tool(t, directory, SO_GOOD_HEADER, &process);
    CHECK_EQ_I64(process.exit_code, 0);

    const char *c_file = so_join(t, directory, "out/use.c");
    so_write_file(t, c_file, "#include \"index.h\"\nint32_t main(void) {\n    return 0;\n}\n");
    const char *include_fixture = so_join(t, directory, "include");
    const char *include_run = so_join(t, so_source(t), "run/include");
    char include_fixture_flag[SO_PATH_BYTES + 2u];
    char include_run_flag[SO_PATH_BYTES + 2u];
    snprintf(include_fixture_flag, sizeof(include_fixture_flag), "-I%s", include_fixture);
    snprintf(include_run_flag, sizeof(include_run_flag), "-I%s", include_run);
    char *const gcc[] = {(char *)so_gcc(t),
                         (char *)"-std=c23",
                         (char *)"-fsyntax-only",
                         include_fixture_flag,
                         include_run_flag,
                         (char *)c_file,
                         nullptr};
    test_process_run(t, gcc, SO_TIMEOUT_COMPILER_MS, &process);
    CHECK_EQ_I64(process.exit_code, 0);

    /* The compiler has to be able to say no, or the agreement above means nothing. */
    const char *wrong_file = so_join(t, directory, "out/index.h");
    char *generated = so_read_file(t, wrong_file);
    REQUIRE(generated != nullptr);
    char *number = strstr(generated, "offsetof(outer_t, first) == 64");
    REQUIRE(number != nullptr);
    memcpy(number + strlen("offsetof(outer_t, first) == "), "60", 2u);
    so_write_file(t, wrong_file, generated);
    test_process_run(t, gcc, SO_TIMEOUT_COMPILER_MS, &process);
    CHECK(process.exit_code != 0);
    CHECK_CONTAINS(process.output, "static assertion failed");

    const char *asm_file = so_join(t, directory, "out/use.asm");
    so_write_file(t, asm_file,
                  "%include \"index.inc\"\n"
                  "%if OUTER_T_SIZEOF != 192 || OUTER_T_DELTA != 168 || INNER_T_TAG != 8\n"
                  "    %fatal the defines are wrong\n"
                  "%endif\n");
    const char *object = so_join(t, directory, "out/use.o");
    char include_out_flag[SO_PATH_BYTES + 2u];
    snprintf(include_out_flag, sizeof(include_out_flag), "-I%s/", so_join(t, directory, "out"));
    char *const nasm[] = {(char *)so_nasm(t), (char *)"-f", (char *)"elf64", include_out_flag,
                          (char *)asm_file,   (char *)"-o", (char *)object,  nullptr};
    test_process_run(t, nasm, SO_TIMEOUT_COMPILER_MS, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

TEST(refuses_what_it_cannot_lay_out_on_the_right_line_and_writes_nothing) {
    for (uint64_t i = 0; i < sizeof(so_bad_cases) / sizeof(so_bad_cases[0]); i++) {
        const so_bad_case_t *bad = &so_bad_cases[i];
        const char *directory = so_case_dir(t, bad->name);
        /* An earlier good output has to stay as it was. */
        so_make_directories(t, so_join(t, directory, "out"));
        const char *stale = so_join(t, directory, "out/index.inc");
        so_write_file(t, stale, "stale\n");
        test_process_t process;
        so_run_tool(t, directory, bad->text, &process);
        CHECK_EQ_I64(process.exit_code, 1);
        CHECK_CONTAINS(process.output, bad->expected);
        char at_line[64];
        snprintf(at_line, sizeof(at_line), "index.h:%u:", (uint32_t)bad->line);
        CHECK_CONTAINS(process.output, at_line);
        const char *after = so_read_file(t, stale);
        REQUIRE(after != nullptr);
        CHECK_STR_EQ(after, "stale\n");
    }
}

TEST(a_struct_may_use_a_struct_from_an_earlier_header_but_not_a_later_one) {
    const char *directory = so_case_dir(t, "order");
    const char *first = so_join(t, directory, "include/fixture/first.h");
    const char *second = so_join(t, directory, "include/fixture/second.h");
    so_write_file(t, first, "typedef struct a_t {\n    uint64_t value;\n} a_t;\n");
    so_write_file(t, second, "typedef struct b_t {\n    a_t two[2];\n} b_t;\n");
    const char *output = so_join(t, directory, "out");
    test_process_t process;
    char *const right_way[] = {(char *)so_tool(t), (char *)output, (char *)first, (char *)second, nullptr};
    test_process_run(t, right_way, SO_TIMEOUT_TOOL_MS, &process);
    CHECK_EQ_I64(process.exit_code, 0);
    const char *inc = so_read_file(t, so_join(t, directory, "out/index.inc"));
    REQUIRE(inc != nullptr);
    CHECK_CONTAINS(inc, "%define B_T_SIZEOF 16\n");
    char *const wrong_way[] = {(char *)so_tool(t), (char *)output, (char *)second, (char *)first, nullptr};
    test_process_run(t, wrong_way, SO_TIMEOUT_TOOL_MS, &process);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "unknown type 'a_t'");
}

TEST(wrong_arguments_and_paths_are_told_apart) {
    const char *directory = so_case_dir(t, "arguments");
    const char *output = so_join(t, directory, "out");
    test_process_t process;
    char *const no_headers[] = {(char *)so_tool(t), (char *)output, nullptr};
    test_process_run(t, no_headers, SO_TIMEOUT_TOOL_MS, &process);
    CHECK_EQ_I64(process.exit_code, 2);
    CHECK_CONTAINS(process.output, "usage:");

    char *const missing[] = {(char *)so_tool(t), (char *)output, (char *)"/nonexistent/include/x/index.h", nullptr};
    test_process_run(t, missing, SO_TIMEOUT_TOOL_MS, &process);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "cannot open");

    char *const outside[] = {(char *)so_tool(t), (char *)output, (char *)"/etc/hostname", nullptr};
    test_process_run(t, outside, SO_TIMEOUT_TOOL_MS, &process);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "not under an include/ directory");

    const char *header = so_join(t, directory, "include/fixture/index.h");
    so_write_file(t, header, "typedef struct a_t {\n    uint64_t value;\n} a_t;\n");
    char *const no_directory[] = {(char *)so_tool(t), (char *)"/nonexistent/out", (char *)header, nullptr};
    test_process_run(t, no_directory, SO_TIMEOUT_TOOL_MS, &process);
    CHECK_EQ_I64(process.exit_code, 2);
    CHECK_CONTAINS(process.output, "cannot create");
}

/* The real thing, read again: the same numbers as the build used, and what the kernels depend on. */
TEST(the_build_output_is_what_the_real_headers_give) {
    const char *generated = so_read_file(t, so_join(t, so_generated(t), "index.inc"));
    REQUIRE(generated != nullptr);
    CHECK_CONTAINS(generated, "%define VEC3_T_SIZEOF 12\n");
    CHECK_CONTAINS(generated, "%define VEC3_T_Z 8\n");
    CHECK_CONTAINS(generated, "%define VEC4_T_SIZEOF 16\n");
    CHECK_CONTAINS(generated, "%define VEC4_T_W 12\n");
    CHECK_CONTAINS(generated, "%define MAT4_T_SIZEOF 64\n");
    CHECK_CONTAINS(generated, "%define MAT4_T_ALIGNOF 32\n");
    CHECK_CONTAINS(generated, "%define MAT4_T_M 0\n");

    const char *directory = so_case_dir(t, "real");
    const char *output = so_join(t, directory, "out");
    const char *vec3 = so_join(t, so_source(t), "run/include/math/vec3/index.h");
    const char *vec4 = so_join(t, so_source(t), "run/include/math/vec4/index.h");
    const char *mat4 = so_join(t, so_source(t), "run/include/math/mat4/index.h");
    test_process_t process;
    char *const argv[] = {(char *)so_tool(t), (char *)output, (char *)vec3, (char *)vec4, (char *)mat4, nullptr};
    test_process_run(t, argv, SO_TIMEOUT_TOOL_MS, &process);
    CHECK_EQ_I64(process.exit_code, 0);
    const char *again = so_read_file(t, so_join(t, output, "index.inc"));
    REQUIRE(again != nullptr);
    CHECK_STR_EQ(again, generated);
}

TEST_MAIN()
