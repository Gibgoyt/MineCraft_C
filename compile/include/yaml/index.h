/*
 * Our YAML subset parser, used by the build-time tools. See compile/src/yaml/README.md for the exact
 * subset. Parsing never allocates: the caller supplies all storage, and the result borrows from it.
 */
#ifndef YAML_INDEX_H
#define YAML_INDEX_H

#include <stdint.h>

/* The deepest nesting accepted. Parsing keeps its open blocks in a fixed array rather than recursing. */
#define YAML_MAX_DEPTH 64u
/* Offsets are 32 bits, and indentation has to be able to go negative for the root. */
#define YAML_MAX_TEXT_BYTES 0x7FFFFFFFu
/* Marks "no such node" in first_child and next_sibling. */
#define YAML_NO_NODE UINT32_MAX
#define YAML_ERROR_MESSAGE_BYTES 120u

typedef enum yaml_kind_t : uint8_t {
    YAML_KIND_NULL = 0, /* `~`, `null`, or a key with no value */
    YAML_KIND_SCALAR = 1,
    YAML_KIND_MAP = 2,
    YAML_KIND_SEQ = 3,
} yaml_kind_t;

/* Text is never NUL-terminated, since most of it is a slice of the source. Use the lengths. */
typedef struct yaml_node_t {
    const uint8_t *borrowed_key;  /* 0: the key inside a mapping, otherwise nullptr */
    const uint8_t *borrowed_text; /* 8: scalar text, into the source or the string storage */
    uint32_t key_length;          /* 16 */
    uint32_t text_length;         /* 20 */
    uint32_t first_child;         /* 24 */
    uint32_t next_sibling;        /* 28 */
    uint32_t child_count;         /* 32 */
    uint32_t line;                /* 36: 1-based; the scalar's own line, or else the key or dash */
    uint32_t column;              /* 40: 1-based, counted in bytes */
    uint8_t kind;                 /* 44: a yaml_kind_t */
    uint8_t quoted;               /* 45: 1 if a quoted scalar, which is always text */
    uint8_t _pad0[2];             /* 46 */
} yaml_node_t;
static_assert(sizeof(yaml_node_t) == 48);

typedef struct yaml_error_t {
    uint32_t line;                          /* 0: 1-based; 0 means no error */
    uint32_t column;                        /* 4: 1-based, counted in bytes */
    char message[YAML_ERROR_MESSAGE_BYTES]; /* 8: NUL-terminated */
} yaml_error_t;
static_assert(sizeof(yaml_error_t) == 128);

/*
 * Memory for one parse. A node capacity of text length + 1 and a string capacity of text length can never
 * run out: every node but the root uses at least one source byte, and decoding never makes text grow.
 */
typedef struct yaml_storage_t {
    yaml_node_t *nodes;       /* 0 */
    uint8_t *strings;         /* 8: decoded quoted text; may be nullptr when string_capacity is 0 */
    uint32_t node_capacity;   /* 16 */
    uint32_t string_capacity; /* 20 */
} yaml_storage_t;
static_assert(sizeof(yaml_storage_t) == 24);

/* A parsed document. Borrows the source text and the storage, and must not outlive either. */
typedef struct yaml_document_t {
    const yaml_node_t *borrowed_nodes; /* 0 */
    uint32_t node_count;               /* 8 */
    uint8_t _pad0[4];                  /* 12 */
} yaml_document_t;
static_assert(sizeof(yaml_document_t) == 16);

/* An exact decimal number: mantissa * 10^exponent. Exact, so tools can emit it with no rounding. */
typedef struct yaml_decimal_t {
    int64_t mantissa; /* 0 */
    int32_t exponent; /* 8 */
    uint8_t _pad0[4]; /* 12 */
} yaml_decimal_t;
static_assert(sizeof(yaml_decimal_t) == 16);

/* Parses text. On failure returns false, with the first problem in error and document emptied. */
bool yaml_parse(yaml_document_t *document, const uint8_t *text, uint64_t length, yaml_storage_t storage,
                yaml_error_t *error);

/* The top node. Null kind for an empty document, and nullptr only for a document whose parse failed. */
const yaml_node_t *yaml_root(const yaml_document_t *document);
/* Walks a node's children in source order. Both return nullptr at the end. */
const yaml_node_t *yaml_first_child(const yaml_document_t *document, const yaml_node_t *node);
const yaml_node_t *yaml_next_sibling(const yaml_document_t *document, const yaml_node_t *node);

/* The value of one key in a mapping, or nullptr if node is no mapping or lacks the key. */
const yaml_node_t *yaml_map_get(const yaml_document_t *document, const yaml_node_t *map, const char *key);
/* A sequence item by position, or nullptr if node is no sequence or position is out of range. */
const yaml_node_t *yaml_seq_at(const yaml_document_t *document, const yaml_node_t *seq, uint32_t position);
/* Follows dotted keys through mappings, such as "cpu.simd.avx2". nullptr if any step is missing. */
const yaml_node_t *yaml_path_get(const yaml_document_t *document, const yaml_node_t *map, const char *path);
/* yaml_path_get, except that a missing path is an error reported at the line of map. */
const yaml_node_t *yaml_path_require(const yaml_document_t *document, const yaml_node_t *map, const char *path,
                                     yaml_error_t *error);

/* True if node is a scalar whose text is exactly this. */
bool yaml_text_equals(const yaml_node_t *node, const char *text);

/*
 * Typed reads. A wrong type is an error at the node's own line. Only the plain forms count, so "true" in
 * quotes is text and not a truth value. nullptr for node is reported as an error too, at line 1.
 *   bool:    exactly true or false. `yes`, `no`, `on`, `off` and `True` are text, as in YAML 1.2.
 *   int64:   [-+]?digits, or 0x and hex digits, checked for overflow.
 *   decimal: [-+]?digits[.digits][e[-+]?digits]. `.inf` and `.nan` are refused.
 *   text:    any scalar, as written or decoded. Not NUL-terminated.
 */
/* These keep the first problem by writing only while error->line is 0, so the error must start zeroed: yaml_parse clears
 * its own, and these do not. */
bool yaml_get_bool(const yaml_node_t *node, bool *out, yaml_error_t *error);
bool yaml_get_int64(const yaml_node_t *node, int64_t *out, yaml_error_t *error);
bool yaml_get_decimal(const yaml_node_t *node, yaml_decimal_t *out, yaml_error_t *error);
bool yaml_get_text(const yaml_node_t *node, const uint8_t **out_text, uint32_t *out_length, yaml_error_t *error);

/* Reports a tool's own complaint about a node, such as an unknown key, with that node's position. Returns false. */
[[gnu::format(printf, 3, 4)]]
bool yaml_fail_at_node(const yaml_node_t *node, yaml_error_t *error, const char *format, ...);

#endif
