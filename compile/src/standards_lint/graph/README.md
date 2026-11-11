# compile/src/standards_lint/graph

**Purpose:** a call graph and its cycle check, fed by GCC `-fcallgraph-info` files and by the CUDA scan.
**Tier:** Arena. Allocates only from the `lint_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions:**
- `lint_graph_begin`, `lint_graph_add_edge`: build the graph.
- `lint_graph_read_ci`: adds one `.ci` file. For each function it notes the source GCC compiled and the file the
  definition is written in. An edge to `__indirect_call` is not added: it marks its caller for `standards_lint/indirect`.
- `lint_graph_find`: a node by name.
- `lint_graph_report_cycles`: reports each cycle once, walking with an explicit stack.
**May be called by:** `standards_lint/rules_c`, `standards_lint/rules_tree`, `standards_lint/indirect` and the tool.
**Depends on:** `standards_lint/support`.
