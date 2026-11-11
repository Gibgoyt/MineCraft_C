/* The standards lint: fails the build on a breach of plans/CODING_STANDARD.md. See README.md.
 *
 *   standards_lint --root <repository>            every rule on every directory
 *   standards_lint --callgraph <list file>        recursion, from GCC -fcallgraph-info output
 *       [--indirect-calls <yaml>]                 with the reviewed calls through pointers, indirect_calls.yaml
 *   standards_lint --list-rules                   every rule id, one per line
 *
 * The first two can be combined. Exit status: 0 clean, 1 a breach, 2 the lint could not do its job. */
#include "standards_lint/catalog/index.h"
#include "standards_lint/graph/index.h"
#include "standards_lint/indirect/index.h"
#include "standards_lint/rules_tree/index.h"
#include "standards_lint/support/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* Reserved, not used: only touched pages cost anything, and each file's scratch space is given back after it. */
enum : uint64_t {
    LINT_WALK_BYTES = 1ull << 30,
    LINT_FILE_BYTES = 3ull << 30,
};

/* The list holds one object path per line, and each object's .ci file sits next to it with the extension replaced,
 * which is where GCC puts it. A missing .ci is an error: silence must not read as "no recursion". */
static void lint_check_callgraph(const char *list_path, const char *allowlist_path, lint_arena_t *arena_walk,
                                 lint_arena_t *arena_file, lint_report_t *report) {
    const char *text = nullptr;
    uint64_t length = 0;
    if (!lint_read_file(arena_walk, report, list_path, &text, &length)) {
        return;
    }
    lint_graph_t graph;
    lint_graph_begin(&graph, arena_walk);
    const char *const end = text + length;
    const char *line = text;
    while (line < end) {
        const char *line_end = memchr(line, '\n', (uint64_t)(end - line));
        if (line_end == nullptr) {
            line_end = end;
        }
        const uint64_t line_length = (uint64_t)(line_end - line);
        if (line_length > 2u) {
            const uint64_t mark = arena_file->used;
            const char *object = lint_arena_text(arena_file, line, line_length);
            if (lint_ends_with(object, ".o")) {
                const char *stem = lint_arena_text(arena_file, line, line_length - 2u);
                lint_graph_read_ci(&graph, arena_file, report, lint_arena_join(arena_file, stem, ".ci"));
            } else {
                lint_error(report, "%s: '%s' is not an object file", list_path, object);
            }
            arena_file->used = mark;
        }
        line = line_end + 1;
    }
    /* Calls through pointers: job functions become edges first, so the cycle check sees them. */
    lint_indirect_check(&graph, allowlist_path, arena_walk, arena_file, report);
    lint_graph_report_cycles(&graph, arena_file, report, "recursion");
}

int32_t main(int32_t argc, char **argv) {
    const char *root = nullptr;
    const char *callgraph = nullptr;
    const char *indirect_calls = nullptr;
    bool list_rules = false;
    bool usage_ok = argc > 1;
    for (int32_t i = 1; i < argc && usage_ok; i++) {
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            i++;
            root = argv[i];
        } else if (strcmp(argv[i], "--callgraph") == 0 && i + 1 < argc) {
            i++;
            callgraph = argv[i];
        } else if (strcmp(argv[i], "--indirect-calls") == 0 && i + 1 < argc) {
            i++;
            indirect_calls = argv[i];
        } else if (strcmp(argv[i], "--list-rules") == 0) {
            list_rules = true;
        } else {
            usage_ok = false;
        }
    }
    usage_ok = usage_ok && (indirect_calls == nullptr || callgraph != nullptr);
    if (!usage_ok) {
        fprintf(stderr,
                "usage: standards_lint [--root <repository>] [--callgraph <list file> [--indirect-calls <yaml>]] "
                "[--list-rules]\n");
        return 2;
    }
    if (list_rules) {
        const uint32_t count = lint_catalog_rule_count();
        for (uint32_t i = 0; i < count; i++) {
            puts(lint_catalog_rule_at(i));
        }
        return 0;
    }

    const uint64_t total = LINT_WALK_BYTES + LINT_FILE_BYTES;
    void *owned_region =
        mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (owned_region == MAP_FAILED) {
        perror("standards_lint: mmap");
        return 2;
    }
    lint_arena_t arena_walk = {.borrowed_base = owned_region, .capacity = LINT_WALK_BYTES, .used = 0};
    lint_arena_t arena_file = {
        .borrowed_base = (uint8_t *)owned_region + LINT_WALK_BYTES, .capacity = LINT_FILE_BYTES, .used = 0};
    lint_report_t report = {0, 0};

    uint64_t file_count = 0;
    if (root != nullptr) {
        file_count = lint_rules_tree_check(root, &arena_walk, &arena_file, &report);
    }
    if (callgraph != nullptr) {
        lint_check_callgraph(callgraph, indirect_calls, &arena_walk, &arena_file, &report);
    }
    const uint64_t breach_count = report.breach_count;
    const uint64_t error_count = report.error_count;
    munmap(owned_region, total);

    if (error_count > 0) {
        fprintf(stderr, "standards_lint: %" PRIu64 " errors, %" PRIu64 " breaches\n", error_count, breach_count);
        return 2;
    }
    if (breach_count > 0) {
        fprintf(stderr, "standards_lint: %" PRIu64 " breaches\n", breach_count);
        return 1;
    }
    if (root != nullptr) {
        printf("standards_lint: clean, %" PRIu64 " source files\n", file_count);
    } else {
        printf("standards_lint: no recursion\n");
    }
    return 0;
}
