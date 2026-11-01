# compile/src/yaml

**Purpose:** our own YAML-subset parser for the build-time tools, with line-numbered errors.
**Tier:** Owner (depth 1 of `compile/src`), though it allocates nothing.
**Owns:** nothing. The caller gives it all node and string storage, and the document borrows from that and from the source text.
**Lifetime:** a parsed document is valid while the caller keeps its source text and storage alive.
**Threads:** any single thread. There is no shared state.
**Public functions** (`compile/include/yaml/index.h`):

- `yaml_parse`: parses one document into caller storage, or reports the first error.
- `yaml_root`, `yaml_first_child`, `yaml_next_sibling`: walk the tree.
- `yaml_map_get`, `yaml_seq_at`, `yaml_path_get`, `yaml_path_require`: look up a value.
- `yaml_text_equals`: compares a scalar's text.
- `yaml_get_bool`, `yaml_get_int64`, `yaml_get_decimal`, `yaml_get_text`: typed reads, with a line-numbered error on a wrong type.
- `yaml_fail_at_node`: reports a tool's own complaint at a node's position.

**May be called by:** tools under `compile/src/`, such as `machine_config` and `game_tables`. Never by `run/`.
**Depends on:** glibc (`string.h`, `stdio.h`, `stdarg.h`).

## Using it

```c
yaml_storage_t storage = { nodes, strings, node_capacity, string_capacity };
yaml_document_t document;
yaml_error_t error;
if (!yaml_parse(&document, text, length, storage, &error)) {
    fprintf(stderr, "%s:%u:%u: %s\n", path, error.line, error.column, error.message);
    return false;
}
const yaml_node_t *workers = yaml_path_require(&document, yaml_root(&document), "threads.workers", &error);
int64_t count;
if (workers == nullptr || !yaml_get_int64(workers, &count, &error)) { /* print error the same way */ }
```

A node capacity of text length + 1 and a string capacity of text length are always enough. Smaller values work if the
text fits, and running out is an ordinary error.

## The subset

Accepted:

- Block mappings (`key: value`) and block sequences (`- value`), nested by indentation with spaces. This includes
  `- a: 1` with more keys below it, `- - x`, and a sequence at the same indentation as its parent key.
- Flow mappings and sequences (`{ a: 1, b: [x, y] }`), nested, across lines if more indented than the parent block,
  with optional trailing commas. `{ a: }` is a null value.
- Plain scalars, single-quoted scalars (`''` is a quote), and scalars in `"` quotes with the escapes `\\ \" \/ \a \b \e
  \f \n \r \t \v`, `\` then a space, `\xHH`, `\uHHHH` and `\UHHHHHHHH`.
- Comments, a leading `---` on its own line, a final `...`, a leading byte order mark, and LF or CRLF line ends.

Refused, each with a line-numbered error rather than a misread value:

- Anchors, aliases, tags, block scalars (`|`, `>`), complex keys (`? `), directives (`%`).
- Scalars spanning lines, and more than one document.
- Tabs in indentation, indentation matching no open block, duplicate keys in one mapping.
- Mappings on a single line in block style (`a: b: c`), `[a: 1]` (write `[{ a: 1 }]`), and `{ a }` (write `{ a: }`).
- Invalid UTF-8, control characters, a carriage return not followed by a line feed, and a NUL from an escape.
- Nesting deeper than `YAML_MAX_DEPTH`, and text larger than `YAML_MAX_TEXT_BYTES`.

Types are decided on reading rather than at parse time, under the YAML 1.2 core rules: `yes` and `no` are text. Only
`~` and `null` (plain) or a missing value are null. The decimal reader gives an exact mantissa and exponent instead of
a rounded binary value, so generated tables hold what the file says, and same input means same output on any machine.

Error positions are the problem's line and byte column, both 1-based. The one exception is an unterminated quote or
flow collection, which points at where it opened, because the end of the file would not help anyone.

Duplicate-key checks compare against the mapping's earlier keys one at a time. That is fine for config files, and the
tools are not exposed to anything else; the node capacity bounds the work.
