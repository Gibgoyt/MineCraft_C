/* A call graph and its cycle check, fed by GCC's -fcallgraph-info files and by the CUDA token scan. */
#ifndef MC_STANDARDS_LINT_GRAPH_INDEX_H
#define MC_STANDARDS_LINT_GRAPH_INDEX_H

#include "standards_lint/support/index.h"

#include <stdint.h>

typedef struct lint_graph_edge_t {
    struct lint_graph_edge_t *next;
    const char *where;
    uint32_t target;
    uint32_t line;
} lint_graph_edge_t;
static_assert(sizeof(lint_graph_edge_t) == 24);

typedef struct lint_graph_node_t {
    const char *name;
    lint_graph_edge_t *edges;
    const char *unit;           /* the .c file GCC was compiling when it defined this function; nullptr if none did */
    const char *source;         /* the file the definition is written in: unit, or a header for a static inline */
    const char *indirect_where; /* where it first calls through a pointer; nullptr if it never does */
} lint_graph_node_t;
static_assert(sizeof(lint_graph_node_t) == 40);

typedef struct lint_graph_t {
    lint_arena_t *borrowed_arena;
    uint32_t *slots; /* name hash to node index + 1; 0 is empty */
    lint_graph_node_t *nodes;
    const char **units; /* the source of each .ci file read, as its first line names it */
    uint32_t node_count;
    uint32_t unit_count;
} lint_graph_t;
static_assert(sizeof(lint_graph_t) == 40);

/* No such node. */
#define LINT_GRAPH_NONE UINT32_MAX

void lint_graph_begin(lint_graph_t *graph, lint_arena_t *arena);

/* where is the call's source location, kept for the report. */
void lint_graph_add_edge(lint_graph_t *graph, const char *from, uint64_t from_length, const char *to,
                         uint64_t to_length, const char *where, uint32_t line);

/* The node with this name, or LINT_GRAPH_NONE. */
uint32_t lint_graph_find(const lint_graph_t *graph, const char *name, uint64_t length);

/* Adds the edges of one GCC .ci file. GCC writes a call through a pointer as an edge to "__indirect_call", which has no
 * target to follow, so that edge is not added: it sets the caller's indirect_where, for standards_lint/indirect. */
bool lint_graph_read_ci(lint_graph_t *graph, lint_arena_t *arena_file, lint_report_t *report, const char *path);

/* Reports each cycle once under rule, and returns how many it found. */
uint32_t lint_graph_report_cycles(const lint_graph_t *graph, lint_arena_t *arena_file, lint_report_t *report,
                                  const char *rule);

#endif
