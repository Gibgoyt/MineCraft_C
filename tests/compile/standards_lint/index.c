/* Proves the lint and the compiler flags reject what they must, one fixture per banned construct.
 *
 *   index --tool <standards_lint> --fixtures <dir> --tmp <dir> --cc <gcc> -- <the real C flags>
 *
 * fixtures/lint/<name>/       a small repository. EXPECT lists the exact rule ids a run must report; empty means clean.
 * fixtures/callgraph/<name>/  .c files compiled with -fcallgraph-info, then checked. EXPECT as above. An
 *                             indirect_calls.yaml there is passed as the list of reviewed calls through pointers.
 * fixtures/compiler/<name>/   index.c compiled with the real flags. EXPECT is "pass", or "fail <text from the error>". */
#include "harness/index.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    FIXTURE_MAX_NAMES = 4096,
    FIXTURE_MAX_ARGS = 128,
    FIXTURE_MAX_RULES = 512,
    FIXTURE_TIMEOUT_MS = 60000,
};

typedef struct fixture_options_t {
    char *tool;
    char *fixtures;
    char *tmp;
    char *cc;
    char **flags;
    int32_t flag_count;
    uint32_t _pad0;
} fixture_options_t;
static_assert(sizeof(fixture_options_t) == 48);

static bool fixture_options(test_context_t *t, fixture_options_t *out) {
    *out = (fixture_options_t){0};
    char **args = t->borrowed_args;
    for (int32_t i = 0; i < t->arg_count; i++) {
        if (strcmp(args[i], "--") == 0) {
            out->flags = args + i + 1;
            out->flag_count = t->arg_count - i - 1;
            break;
        }
        if (i + 1 >= t->arg_count) {
            return false;
        }
        if (strcmp(args[i], "--tool") == 0) {
            out->tool = args[i + 1];
        } else if (strcmp(args[i], "--fixtures") == 0) {
            out->fixtures = args[i + 1];
        } else if (strcmp(args[i], "--tmp") == 0) {
            out->tmp = args[i + 1];
        } else if (strcmp(args[i], "--cc") == 0) {
            out->cc = args[i + 1];
        } else {
            return false;
        }
        i++;
    }
    return out->tool != nullptr && out->fixtures != nullptr && out->tmp != nullptr && out->cc != nullptr;
}

static char *fixture_join(test_context_t *t, const char *left, const char *right) {
    const uint64_t left_length = strlen(left);
    const uint64_t right_length = strlen(right);
    char *joined = test_alloc(t, left_length + right_length + 2u);
    memcpy(joined, left, left_length);
    joined[left_length] = '/';
    memcpy(joined + left_length + 1u, right, right_length + 1u);
    return joined;
}

static int32_t fixture_compare_names(const void *left, const void *right) {
    return strcmp(*(char *const *)left, *(char *const *)right);
}

/* The entries of a directory, sorted. A fixtures directory that cannot be read fails the test. */
static uint32_t fixture_list(test_context_t *t, const char *directory, char **names) {
    DIR *handle = opendir(directory);
    if (!CHECK(handle != nullptr)) {
        return 0;
    }
    uint32_t count = 0;
    const struct dirent *entry = readdir(handle);
    while (entry != nullptr) {
        if (entry->d_name[0] != '.' && count < FIXTURE_MAX_NAMES) {
            const uint64_t bytes = strlen(entry->d_name) + 1u;
            names[count] = test_alloc(t, bytes);
            memcpy(names[count], entry->d_name, bytes);
            count++;
        }
        entry = readdir(handle);
    }
    closedir(handle);
    qsort(names, count, sizeof(char *), fixture_compare_names);
    return count;
}

/* The whole file as text. */
static char *fixture_read(test_context_t *t, const char *path) {
    const int32_t fd = open(path, O_RDONLY);
    if (fd < 0) {
        return nullptr;
    }
    struct stat info;
    if (fstat(fd, &info) != 0) {
        close(fd);
        return nullptr;
    }
    const uint64_t size = (uint64_t)info.st_size;
    char *text = test_alloc(t, size + 1u);
    const int64_t got = read(fd, text, size);
    close(fd);
    if (got != (int64_t)size) {
        return nullptr;
    }
    text[size] = '\0';
    return text;
}

typedef struct fixture_rules_t {
    char *rules[FIXTURE_MAX_RULES];
    uint32_t count;
    uint32_t _pad0;
} fixture_rules_t;
static_assert(sizeof(fixture_rules_t) == 8u * FIXTURE_MAX_RULES + 8u);

static void fixture_rules_add(test_context_t *t, fixture_rules_t *set, const char *rule, uint64_t length) {
    for (uint32_t i = 0; i < set->count; i++) {
        if (strlen(set->rules[i]) == length && memcmp(set->rules[i], rule, length) == 0) {
            return;
        }
    }
    if (set->count < FIXTURE_MAX_RULES) {
        char *copy = test_alloc(t, length + 1u);
        memcpy(copy, rule, length);
        copy[length] = '\0';
        set->rules[set->count] = copy;
        set->count++;
    }
}

/* Every "[rule]" in the lint's output. */
static void fixture_rules_from_output(test_context_t *t, fixture_rules_t *set, const char *output) {
    const char *line = output;
    while (line != nullptr && *line != '\0') {
        const char *line_end = strchr(line, '\n');
        const char *open = strstr(line, ": [");
        if (open != nullptr && (line_end == nullptr || open < line_end)) {
            const char *close = strchr(open, ']');
            if (close != nullptr) {
                fixture_rules_add(t, set, open + 3, (uint64_t)(close - open - 3));
            }
        }
        line = line_end != nullptr ? line_end + 1 : nullptr;
    }
}

static void fixture_rules_from_expect(test_context_t *t, fixture_rules_t *set, const char *text) {
    const char *line = text;
    while (*line != '\0') {
        const char *line_end = strchr(line, '\n');
        const uint64_t length = line_end != nullptr ? (uint64_t)(line_end - line) : strlen(line);
        if (length > 0) {
            fixture_rules_add(t, set, line, length);
        }
        if (line_end == nullptr) {
            break;
        }
        line = line_end + 1;
    }
}

static bool fixture_rules_has(const fixture_rules_t *set, const char *rule) {
    for (uint32_t i = 0; i < set->count; i++) {
        if (strcmp(set->rules[i], rule) == 0) {
            return true;
        }
    }
    return false;
}

/* Exit status and the exact set of rules. An extra rule means a fixture proves less than it claims. */
static void fixture_judge(test_context_t *t, const char *name, const fixture_rules_t *expected,
                          const test_process_t *process) {
    fixture_rules_t *got = test_alloc(t, sizeof(fixture_rules_t));
    fixture_rules_from_output(t, got, process->output);
    const int32_t want_exit = expected->count > 0 ? 1 : 0;
    bool ok = process->exit_code == want_exit && expected->count == got->count;
    for (uint32_t i = 0; i < expected->count && ok; i++) {
        ok = fixture_rules_has(got, expected->rules[i]);
    }
    if (!ok) {
        fprintf(stderr, "fixture %s: expected exit %d and %u rules, got exit %d. Output:\n%s\n", name, want_exit,
                expected->count, process->exit_code, process->output);
    }
    CHECK(ok);
}

static void fixture_run_lint(test_context_t *t, const fixture_options_t *options, const char *name,
                             const char *directory, fixture_rules_t *covered) {
    const char *expect = fixture_read(t, fixture_join(t, directory, "EXPECT"));
    if (expect == nullptr) {
        fprintf(stderr, "fixture %s has no EXPECT file\n", name);
        CHECK(expect != nullptr);
        return;
    }
    fixture_rules_t *expected = test_alloc(t, sizeof(fixture_rules_t));
    fixture_rules_from_expect(t, expected, expect);

    char root_flag[] = "--root";
    char *const argv[] = {options->tool, root_flag, (char *)directory, nullptr};
    test_process_t process;
    test_process_run(t, argv, FIXTURE_TIMEOUT_MS, &process);
    fixture_judge(t, name, expected, &process);
    for (uint32_t i = 0; i < expected->count; i++) {
        fixture_rules_add(t, covered, expected->rules[i], strlen(expected->rules[i]));
    }
}

static void fixture_run_all_lint(test_context_t *t, const fixture_options_t *options, fixture_rules_t *covered) {
    const char *directory = fixture_join(t, options->fixtures, "lint");
    char **names = test_alloc(t, FIXTURE_MAX_NAMES * sizeof(char *));
    const uint32_t count = fixture_list(t, directory, names);
    CHECK(count > 0);
    for (uint32_t i = 0; i < count; i++) {
        fixture_run_lint(t, options, names[i], fixture_join(t, directory, names[i]), covered);
    }
}

/* Compiles one fixture source with the real flags. What comes after them wins: -O0 and no LTO, so code generation
 * happens here and the output reflects the source as written, on every preset. */
static void fixture_compile(test_context_t *t, const fixture_options_t *options, const char *source, const char *object,
                            const char *extra_flag, test_process_t *out) {
    char **argv = test_alloc(t, (FIXTURE_MAX_ARGS + 1u) * sizeof(char *));
    uint32_t count = 0;
    argv[count] = options->cc;
    count++;
    for (int32_t i = 0; i < options->flag_count && count + 8u < FIXTURE_MAX_ARGS; i++) {
        argv[count] = options->flags[i];
        count++;
    }
    char std_flag[] = "-std=c23";
    char optimize_flag[] = "-O0";
    char lto_flag[] = "-fno-lto";
    char compile_flag[] = "-c";
    char output_flag[] = "-o";
    argv[count] = std_flag;
    count++;
    argv[count] = optimize_flag;
    count++;
    argv[count] = lto_flag;
    count++;
    if (extra_flag != nullptr) {
        argv[count] = (char *)extra_flag;
        count++;
    }
    argv[count] = compile_flag;
    count++;
    argv[count] = (char *)source;
    count++;
    argv[count] = output_flag;
    count++;
    argv[count] = (char *)object;
    count++;
    argv[count] = nullptr;
    test_process_run(t, argv, FIXTURE_TIMEOUT_MS, out);
}

TEST(lint_fixtures_report_exactly_their_rules) {
    fixture_options_t options;
    REQUIRE(fixture_options(t, &options));

    /* Every rule the lint can report needs a fixture. This is what keeps "one failing fixture per banned construct"
     * true as rules are added. */
    fixture_rules_t *covered = test_alloc(t, sizeof(fixture_rules_t));
    fixture_run_all_lint(t, &options, covered);

    /* The call graph rules need real GCC output, so their fixtures are the ones callgraph_fixtures_find_recursion
     * runs, which judges them. Here they only count as covered. */
    const char *callgraph = fixture_join(t, options.fixtures, "callgraph");
    char **callgraph_names = test_alloc(t, FIXTURE_MAX_NAMES * sizeof(char *));
    const uint32_t callgraph_count = fixture_list(t, callgraph, callgraph_names);
    for (uint32_t i = 0; i < callgraph_count; i++) {
        const char *expect = fixture_read(t, fixture_join(t, fixture_join(t, callgraph, callgraph_names[i]), "EXPECT"));
        if (expect != nullptr) {
            fixture_rules_from_expect(t, covered, expect);
        }
    }

    char list_flag[] = "--list-rules";
    char *const argv[] = {options.tool, list_flag, nullptr};
    test_process_t process;
    test_process_run(t, argv, FIXTURE_TIMEOUT_MS, &process);
    CHECK_EQ_I64(process.exit_code, 0);

    char *line = process.output;
    uint32_t rule_count = 0;
    while (*line != '\0') {
        char *line_end = strchr(line, '\n');
        REQUIRE(line_end != nullptr);
        *line_end = '\0';
        const bool found = fixture_rules_has(covered, line);
        if (!found) {
            fprintf(stderr, "rule %s has no fixture under fixtures/lint/\n", line);
        }
        CHECK(found);
        rule_count++;
        line = line_end + 1;
    }
    CHECK(rule_count > 100u);
    printf("harness: %u rules, each with a failing fixture\n", rule_count);
}

TEST(callgraph_fixtures_find_recursion) {
    fixture_options_t options;
    REQUIRE(fixture_options(t, &options));
    const char *directory = fixture_join(t, options.fixtures, "callgraph");
    char **names = test_alloc(t, FIXTURE_MAX_NAMES * sizeof(char *));
    const uint32_t count = fixture_list(t, directory, names);
    CHECK(count >= 3u);

    for (uint32_t i = 0; i < count; i++) {
        const char *fixture = fixture_join(t, directory, names[i]);
        const char *expect = fixture_read(t, fixture_join(t, fixture, "EXPECT"));
        REQUIRE(expect != nullptr);
        fixture_rules_t *expected = test_alloc(t, sizeof(fixture_rules_t));
        fixture_rules_from_expect(t, expected, expect);

        char **sources = test_alloc(t, FIXTURE_MAX_NAMES * sizeof(char *));
        const uint32_t source_count = fixture_list(t, fixture, sources);
        const char *tmp = fixture_join(t, options.tmp, names[i]);
        mkdir(tmp, 0755);
        const char *list_path = fixture_join(t, tmp, "objects.list");
        FILE *list = fopen(list_path, "w");
        REQUIRE(list != nullptr);
        for (uint32_t s = 0; s < source_count; s++) {
            const uint64_t name_length = strlen(sources[s]);
            if (name_length < 3u || strcmp(sources[s] + name_length - 2u, ".c") != 0) {
                continue;
            }
            const char *object = fixture_join(t, tmp, sources[s]);
            const uint64_t object_length = strlen(object);
            char *object_o = test_alloc(t, object_length + 3u);
            memcpy(object_o, object, object_length);
            memcpy(object_o + object_length, ".o", 3u);
            test_process_t compile;
            fixture_compile(t, &options, fixture_join(t, fixture, sources[s]), object_o, "-fcallgraph-info=su",
                            &compile);
            if (compile.exit_code != 0) {
                fprintf(stderr, "fixture %s did not compile:\n%s\n", names[i], compile.output);
            }
            CHECK_EQ_I64(compile.exit_code, 0);
            fprintf(list, "%s\n", object_o);
        }
        fclose(list);

        char graph_flag[] = "--callgraph";
        char list_flag[] = "--indirect-calls";
        char *allowlist = fixture_join(t, fixture, "indirect_calls.yaml");
        char *argv[] = {options.tool, graph_flag, (char *)list_path, list_flag, allowlist, nullptr};
        if (access(allowlist, R_OK) != 0) {
            argv[3] = nullptr;
        }
        test_process_t process;
        test_process_run(t, argv, FIXTURE_TIMEOUT_MS, &process);
        fixture_judge(t, names[i], expected, &process);
    }

    /* A .ci file that is not there must be an error and not a clean pass. */
    const char *missing = fixture_join(t, options.tmp, "missing.list");
    FILE *list = fopen(missing, "w");
    REQUIRE(list != nullptr);
    fprintf(list, "%s/no_such_object.o\n", options.tmp);
    fclose(list);
    char graph_flag[] = "--callgraph";
    char *const argv[] = {options.tool, graph_flag, (char *)missing, nullptr};
    test_process_t process;
    test_process_run(t, argv, FIXTURE_TIMEOUT_MS, &process);
    CHECK_EQ_I64(process.exit_code, 2);
}

TEST(compiler_fixtures_are_judged_by_the_real_flags) {
    fixture_options_t options;
    REQUIRE(fixture_options(t, &options));
    const char *directory = fixture_join(t, options.fixtures, "compiler");
    char **names = test_alloc(t, FIXTURE_MAX_NAMES * sizeof(char *));
    const uint32_t count = fixture_list(t, directory, names);
    CHECK(count >= 2u);

    for (uint32_t i = 0; i < count; i++) {
        const char *fixture = fixture_join(t, directory, names[i]);
        char *expect = fixture_read(t, fixture_join(t, fixture, "EXPECT"));
        REQUIRE(expect != nullptr);
        char *newline = strchr(expect, '\n');
        if (newline != nullptr) {
            *newline = '\0';
        }
        const char *object = fixture_join(t, options.tmp, names[i]);
        test_process_t compile;
        fixture_compile(t, &options, fixture_join(t, fixture, "index.c"), object, nullptr, &compile);
        if (strcmp(expect, "pass") == 0) {
            if (compile.exit_code != 0) {
                fprintf(stderr, "fixture %s should compile:\n%s\n", names[i], compile.output);
            }
            CHECK_EQ_I64(compile.exit_code, 0);
        } else {
            REQUIRE(strncmp(expect, "fail ", 5u) == 0);
            if (compile.exit_code == 0) {
                fprintf(stderr, "fixture %s should not compile\n", names[i]);
            }
            CHECK(compile.exit_code != 0);
            CHECK_CONTAINS(compile.output, expect + 5);
        }
    }
}

TEST_MAIN()
