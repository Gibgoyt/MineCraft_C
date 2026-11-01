/* See README.md. The cycle check walks with an explicit stack: the lint obeys the rule it enforces. */
#include "standards_lint/graph/index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    GRAPH_SLOT_COUNT = 1u << 20, /* far more functions than this repository will ever have */
    GRAPH_MAX_NODES = GRAPH_SLOT_COUNT / 2u,
};

void lint_graph_begin(lint_graph_t *graph, lint_arena_t *arena) {
    graph->borrowed_arena = arena;
    graph->slots = lint_arena_alloc(arena, GRAPH_SLOT_COUNT * sizeof(uint32_t));
    memset(graph->slots, 0, GRAPH_SLOT_COUNT * sizeof(uint32_t));
    graph->nodes = lint_arena_alloc(arena, GRAPH_MAX_NODES * sizeof(lint_graph_node_t));
    graph->node_count = 0;
    graph->_pad0 = 0;
}

static uint32_t graph_node(lint_graph_t *graph, const char *name, uint64_t length) {
    uint32_t hash = 2166136261u;
    for (uint64_t i = 0; i < length; i++) {
        hash = (hash ^ (uint8_t)name[i]) * 16777619u;
    }
    uint32_t slot = hash & (GRAPH_SLOT_COUNT - 1u);
    while (graph->slots[slot] != 0) {
        const uint32_t index = graph->slots[slot] - 1u;
        if (lint_text_equal(name, length, graph->nodes[index].name)) {
            return index;
        }
        slot = (slot + 1u) & (GRAPH_SLOT_COUNT - 1u);
    }
    if (graph->node_count >= GRAPH_MAX_NODES) {
        fprintf(stderr, "standards_lint: more than %u functions in the call graph\n", GRAPH_MAX_NODES);
        exit(2);
    }
    const uint32_t index = graph->node_count;
    graph->node_count++;
    graph->nodes[index].name = lint_arena_text(graph->borrowed_arena, name, length);
    graph->nodes[index].edges = nullptr;
    graph->slots[slot] = index + 1u;
    return index;
}

void lint_graph_add_edge(lint_graph_t *graph, const char *from, uint64_t from_length, const char *to,
                         uint64_t to_length, const char *where, uint32_t line) {
    const uint32_t source = graph_node(graph, from, from_length);
    const uint32_t target = graph_node(graph, to, to_length);
    lint_graph_edge_t *edge = lint_arena_alloc(graph->borrowed_arena, sizeof(lint_graph_edge_t));
    edge->next = graph->nodes[source].edges;
    edge->where = where;
    edge->target = target;
    edge->line = line;
    graph->nodes[source].edges = edge;
}

/* Finds key: "value" in one line of a .ci file. */
static bool graph_ci_field(const char *line, const char *line_end, const char *key, const char **out_value,
                           uint64_t *out_length) {
    const uint64_t key_length = strlen(key);
    for (const char *p = line; p + key_length < line_end; p++) {
        if (memcmp(p, key, key_length) == 0) {
            const char *value = p + key_length;
            const char *end = value;
            while (end < line_end && *end != '"') {
                end++;
            }
            if (end >= line_end) {
                return false;
            }
            *out_value = value;
            *out_length = (uint64_t)(end - value);
            return true;
        }
    }
    return false;
}

bool lint_graph_read_ci(lint_graph_t *graph, lint_arena_t *arena_file, lint_report_t *report, const char *path) {
    const char *text = nullptr;
    uint64_t length = 0;
    if (!lint_read_file(arena_file, report, path, &text, &length)) {
        return false;
    }
    if (!lint_starts_with(text, "graph:")) {
        /* A format change in a future GCC must not read as "no recursion". */
        lint_error(report, "%s does not look like -fcallgraph-info output", path);
        return false;
    }
    const char *const file_end = text + length;
    const char *line = text;
    while (line < file_end) {
        const char *line_end = memchr(line, '\n', (uint64_t)(file_end - line));
        if (line_end == nullptr) {
            line_end = file_end;
        }
        if ((uint64_t)(line_end - line) > 6u && memcmp(line, "edge: ", 6u) == 0) {
            const char *from = nullptr;
            const char *to = nullptr;
            const char *label = nullptr;
            uint64_t from_length = 0;
            uint64_t to_length = 0;
            uint64_t label_length = 0;
            if (!graph_ci_field(line, line_end, "sourcename: \"", &from, &from_length) ||
                !graph_ci_field(line, line_end, "targetname: \"", &to, &to_length)) {
                lint_error(report, "%s: an edge without a source or a target", path);
                return false;
            }
            /* Some edges, such as the one to __stack_chk_fail, have no call site. */
            const char *where = "(no call site)";
            if (graph_ci_field(line, line_end, "label: \"", &label, &label_length)) {
                where = lint_arena_text(graph->borrowed_arena, label, label_length);
            }
            lint_graph_add_edge(graph, from, from_length, to, to_length, where, 0);
        }
        line = line_end + 1;
    }
    return true;
}

enum { GRAPH_WHITE = 0, GRAPH_GRAY = 1, GRAPH_BLACK = 2 };

typedef struct graph_frame_t {
    const lint_graph_edge_t *next_edge;
    uint32_t node;
    uint32_t _pad0;
} graph_frame_t;
static_assert(sizeof(graph_frame_t) == 16);

static void graph_report_cycle(const lint_graph_t *graph, lint_report_t *report, const char *rule,
                               const graph_frame_t *stack, uint32_t depth, const lint_graph_edge_t *closing,
                               char *chain, uint64_t chain_capacity) {
    uint32_t start = depth;
    while (start > 0 && stack[start - 1u].node != closing->target) {
        start--;
    }
    uint64_t used = 0;
    for (uint32_t i = start > 0 ? start - 1u : 0; i < depth; i++) {
        const int32_t wrote = snprintf(chain + used, chain_capacity - used, "%s -> ", graph->nodes[stack[i].node].name);
        if (wrote < 0 || (uint64_t)wrote >= chain_capacity - used) {
            break;
        }
        used += (uint64_t)wrote;
    }
    lint_breach(report, closing->where, closing->line, rule,
                "%s%s calls back into itself; use an explicit stack from an arena", chain,
                graph->nodes[closing->target].name);
}

uint32_t lint_graph_report_cycles(const lint_graph_t *graph, lint_arena_t *arena_file, lint_report_t *report,
                                  const char *rule) {
    const uint64_t node_count = graph->node_count;
    uint8_t *colors = lint_arena_alloc(arena_file, node_count + 1u);
    memset(colors, GRAPH_WHITE, node_count + 1u);
    graph_frame_t *stack = lint_arena_alloc(arena_file, (node_count + 1u) * sizeof(graph_frame_t));
    enum { GRAPH_CHAIN_BYTES = 4096 };
    char *chain = lint_arena_alloc(arena_file, GRAPH_CHAIN_BYTES);
    uint32_t cycles = 0;

    for (uint32_t root = 0; root < graph->node_count; root++) {
        if (colors[root] != GRAPH_WHITE) {
            continue;
        }
        uint32_t depth = 1;
        stack[0] = (graph_frame_t){.next_edge = graph->nodes[root].edges, .node = root, ._pad0 = 0};
        colors[root] = GRAPH_GRAY;
        while (depth > 0) {
            graph_frame_t *frame = &stack[depth - 1u];
            const lint_graph_edge_t *edge = frame->next_edge;
            if (edge == nullptr) {
                colors[frame->node] = GRAPH_BLACK;
                depth--;
                continue;
            }
            frame->next_edge = edge->next;
            if (colors[edge->target] == GRAPH_GRAY) {
                graph_report_cycle(graph, report, rule, stack, depth, edge, chain, GRAPH_CHAIN_BYTES);
                cycles++;
            } else if (colors[edge->target] == GRAPH_WHITE) {
                colors[edge->target] = GRAPH_GRAY;
                stack[depth] =
                    (graph_frame_t){.next_edge = graph->nodes[edge->target].edges, .node = edge->target, ._pad0 = 0};
                depth++;
            }
        }
    }
    return cycles;
}
