/*
 * The YAML subset parser. The subset, and why each refusal is an error rather than a guess, is in README.md.
 *
 * One pass over the text, with one explicit stack of open collections, because recursion is banned. Block
 * structure works line by line: a key with no value yet is "pending", and the next line's indentation decides
 * whether it gets a value or stays null.
 */
#include "yaml/index.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct yaml_frame_t {
    uint32_t node;       /* 0: the collection this frame holds open */
    uint32_t last_child; /* 4: for appending in source order */
    int32_t indent;      /* 8: column of its entries, in block style */
    uint8_t after_value; /* 12: flow style only: the next thing must be a comma or the closer */
    uint8_t _pad0[3];    /* 13 */
} yaml_frame_t;
static_assert(sizeof(yaml_frame_t) == 16);

typedef struct yaml_parser_t {
    const uint8_t *text;                 /* 0 */
    yaml_node_t *nodes;                  /* 8 */
    uint8_t *strings;                    /* 16 */
    yaml_error_t *error;                 /* 24 */
    uint32_t length;                     /* 32 */
    uint32_t pos;                        /* 36 */
    uint32_t line;                       /* 40: 1-based */
    uint32_t line_start;                 /* 44 */
    uint32_t node_count;                 /* 48 */
    uint32_t node_capacity;              /* 52 */
    uint32_t string_used;                /* 56 */
    uint32_t string_capacity;            /* 60 */
    uint32_t depth;                      /* 64 */
    uint32_t pending;                    /* 68: the node still waiting for a value, or YAML_NO_NODE */
    int32_t pending_indent;              /* 72: column of the block holding pending; -1 for the root */
    uint8_t _pad0[4];                    /* 76 */
    yaml_frame_t frames[YAML_MAX_DEPTH]; /* 80 */
} yaml_parser_t;
static_assert(sizeof(yaml_parser_t) == 80 + 16 * YAML_MAX_DEPTH);

/* A scalar as scanned, before it is known to be a key or a value. */
typedef struct yaml_scalar_t {
    const uint8_t *text; /* 0 */
    uint32_t length;     /* 8 */
    uint32_t line;       /* 12 */
    uint32_t column;     /* 16 */
    uint8_t quoted;      /* 20 */
    uint8_t _pad0[3];    /* 21 */
} yaml_scalar_t;
static_assert(sizeof(yaml_scalar_t) == 24);

/* ---- Errors ---------------------------------------------------------------------------------------- */

static void yaml_error_write(yaml_error_t *error, uint32_t line, uint32_t column, const char *format, va_list args) {
    /* Every failure returns at once, so the first error written is the one that caused it. */
    if (error == nullptr || error->line != 0) {
        return;
    }
    error->line = line;
    error->column = column;
    vsnprintf(error->message, sizeof(error->message), format, args);
}

[[gnu::format(printf, 4, 5)]]
static bool yaml_fail_at(yaml_parser_t *parser, uint32_t line, uint32_t column, const char *format, ...) {
    va_list args;
    va_start(args, format);
    yaml_error_write(parser->error, line, column, format, args);
    va_end(args);
    return false;
}

[[gnu::format(printf, 2, 3)]]
static bool yaml_fail(yaml_parser_t *parser, const char *format, ...) {
    va_list args;
    va_start(args, format);
    yaml_error_write(parser->error, parser->line, parser->pos - parser->line_start + 1u, format, args);
    va_end(args);
    return false;
}

bool yaml_fail_at_node(const yaml_node_t *node, yaml_error_t *error, const char *format, ...) {
    va_list args;
    va_start(args, format);
    yaml_error_write(error, node != nullptr ? node->line : 1u, node != nullptr ? node->column : 1u, format, args);
    va_end(args);
    return false;
}

/* ---- Reading bytes ---------------------------------------------------------------------------------- */

/* The text was validated up front to hold no NUL, so 0 can only mean the end. */
static uint8_t yaml_peek_at(const yaml_parser_t *parser, uint32_t offset) {
    uint32_t at = parser->pos + offset;
    return at < parser->length ? parser->text[at] : 0u;
}

static uint8_t yaml_peek(const yaml_parser_t *parser) {
    return yaml_peek_at(parser, 0u);
}

static bool yaml_is_blank(uint8_t byte) {
    return byte == ' ' || byte == '\t';
}

/* The validator guarantees that \r comes only as part of \r\n. */
static bool yaml_is_line_end(uint8_t byte) {
    return byte == '\n' || byte == '\r' || byte == 0u;
}

static bool yaml_is_space_or_end(uint8_t byte) {
    return yaml_is_blank(byte) || yaml_is_line_end(byte);
}

static bool yaml_is_flow_indicator(uint8_t byte) {
    return byte == ',' || byte == '[' || byte == ']' || byte == '{' || byte == '}';
}

static void yaml_skip_blanks(yaml_parser_t *parser) {
    while (yaml_is_blank(yaml_peek(parser))) {
        parser->pos++;
    }
}

static void yaml_skip_to_line_end(yaml_parser_t *parser) {
    while (!yaml_is_line_end(yaml_peek(parser))) {
        parser->pos++;
    }
}

/* Call only at a line end that is not the end of the text. */
static void yaml_next_line(yaml_parser_t *parser) {
    parser->pos += yaml_peek(parser) == '\r' ? 2u : 1u;
    parser->line++;
    parser->line_start = parser->pos;
}

/* A # only starts a comment after whitespace, so leftovers such as `"a"#b` are not silently dropped. */
static bool yaml_at_comment(const yaml_parser_t *parser) {
    return yaml_peek(parser) == '#' &&
           (parser->pos == parser->line_start || yaml_is_blank(parser->text[parser->pos - 1u]));
}

/* Accepts only blanks and a comment up to the line end, and stops at the line end. */
static bool yaml_finish_line(yaml_parser_t *parser) {
    yaml_skip_blanks(parser);
    uint8_t byte = yaml_peek(parser);
    if (yaml_is_line_end(byte)) {
        return true;
    }
    if (yaml_at_comment(parser)) {
        yaml_skip_to_line_end(parser);
        return true;
    }
    return yaml_fail(parser, "unexpected content '%c' after the value", byte);
}

static bool yaml_is_marker(const yaml_parser_t *parser, char marker) {
    return yaml_peek_at(parser, 0u) == (uint8_t)marker && yaml_peek_at(parser, 1u) == (uint8_t)marker &&
           yaml_peek_at(parser, 2u) == (uint8_t)marker && yaml_is_space_or_end(yaml_peek_at(parser, 3u));
}

/* ---- Validating the text ---------------------------------------------------------------------------- */

/* The bytes a UTF-8 sequence starting with lead must have after it, or 0 if lead cannot start one. */
static uint32_t yaml_utf8_tail_bytes(uint8_t lead) {
    if (lead >= 0xC2u && lead <= 0xDFu) {
        return 1u;
    }
    if (lead >= 0xE0u && lead <= 0xEFu) {
        return 2u;
    }
    if (lead >= 0xF0u && lead <= 0xF4u) {
        return 3u;
    }
    return 0u;
}

/* The second byte's range is what rules out overlong forms, surrogates and values above U+10FFFF. */
static bool yaml_utf8_second_byte_ok(uint8_t lead, uint8_t second) {
    uint8_t low = lead == 0xE0u ? 0xA0u : lead == 0xF0u ? 0x90u : 0x80u;
    uint8_t high = lead == 0xEDu ? 0x9Fu : lead == 0xF4u ? 0x8Fu : 0xBFu;
    return second >= low && second <= high;
}

static bool yaml_validate_text(yaml_parser_t *parser) {
    while (parser->pos < parser->length) {
        uint8_t byte = parser->text[parser->pos];
        if (byte == '\n') {
            yaml_next_line(parser);
        } else if (byte == '\r') {
            if (yaml_peek_at(parser, 1u) != '\n') {
                return yaml_fail(parser, "a carriage return must be followed by a line feed");
            }
            yaml_next_line(parser);
        } else if (byte < 0x20u && byte != '\t') {
            return yaml_fail(parser, "control character 0x%02X is not allowed", byte);
        } else if (byte == 0x7Fu) {
            return yaml_fail(parser, "control character 0x7F is not allowed");
        } else if (byte < 0x80u) {
            parser->pos++;
        } else {
            uint32_t tail = yaml_utf8_tail_bytes(byte);
            if (tail == 0u) {
                return yaml_fail(parser, "invalid UTF-8");
            }
            for (uint32_t i = 1u; i <= tail; i++) {
                uint8_t next = yaml_peek_at(parser, i);
                bool in_range = i == 1u ? yaml_utf8_second_byte_ok(byte, next) : (next >= 0x80u && next <= 0xBFu);
                if (!in_range) {
                    return yaml_fail(parser, "invalid UTF-8");
                }
            }
            parser->pos += tail + 1u;
        }
    }
    return true;
}

/* ---- Building nodes --------------------------------------------------------------------------------- */

static bool yaml_new_node(yaml_parser_t *parser, uint32_t *out_node) {
    if (parser->node_count >= parser->node_capacity) {
        return yaml_fail(parser, "out of node storage (capacity %u)", parser->node_capacity);
    }
    uint32_t node = parser->node_count++;
    parser->nodes[node] = (yaml_node_t){
        .first_child = YAML_NO_NODE,
        .next_sibling = YAML_NO_NODE,
        .line = parser->line,
        .column = parser->pos - parser->line_start + 1u,
        .kind = YAML_KIND_NULL,
    };
    *out_node = node;
    return true;
}

/* Appends a new child to the collection on top of the stack. */
static bool yaml_add_child(yaml_parser_t *parser, uint32_t *out_child) {
    yaml_frame_t *frame = &parser->frames[parser->depth - 1u];
    if (!yaml_new_node(parser, out_child)) {
        return false;
    }
    yaml_node_t *parent = &parser->nodes[frame->node];
    if (frame->last_child == YAML_NO_NODE) {
        parent->first_child = *out_child;
    } else {
        parser->nodes[frame->last_child].next_sibling = *out_child;
    }
    frame->last_child = *out_child;
    parent->child_count++;
    return true;
}

/* Turns a node into a collection and makes it the top of the stack. */
static bool yaml_open_collection(yaml_parser_t *parser, uint32_t node, yaml_kind_t kind, int32_t indent) {
    if (parser->depth >= YAML_MAX_DEPTH) {
        return yaml_fail(parser, "nesting is deeper than the limit of %u", YAML_MAX_DEPTH);
    }
    parser->nodes[node].kind = kind;
    parser->frames[parser->depth] = (yaml_frame_t){
        .node = node,
        .last_child = YAML_NO_NODE,
        .indent = indent,
    };
    parser->depth++;
    return true;
}

static uint8_t yaml_top_kind(const yaml_parser_t *parser) {
    return parser->nodes[parser->frames[parser->depth - 1u].node].kind;
}

static bool yaml_bytes_equal(const uint8_t *a, uint32_t a_length, const uint8_t *b, uint32_t b_length) {
    return a_length == b_length && (a_length == 0u || memcmp(a, b, a_length) == 0);
}

/* Adds a key to the mapping on top of the stack. The new child is positioned at the key. */
static bool yaml_add_key(yaml_parser_t *parser, const yaml_scalar_t *key, uint32_t *out_child) {
    const yaml_node_t *map = &parser->nodes[parser->frames[parser->depth - 1u].node];
    for (uint32_t child = map->first_child; child != YAML_NO_NODE; child = parser->nodes[child].next_sibling) {
        const yaml_node_t *other = &parser->nodes[child];
        if (yaml_bytes_equal(other->borrowed_key, other->key_length, key->text, key->length)) {
            return yaml_fail_at(parser, key->line, key->column, "duplicate key '%.*s', first used on line %u",
                                (int32_t)(key->length > 40u ? 40u : key->length), (const char *)key->text, other->line);
        }
    }
    if (!yaml_add_child(parser, out_child)) {
        return false;
    }
    yaml_node_t *child = &parser->nodes[*out_child];
    child->borrowed_key = key->text;
    child->key_length = key->length;
    child->line = key->line;
    child->column = key->column;
    return true;
}

static void yaml_set_scalar(yaml_parser_t *parser, uint32_t node, const yaml_scalar_t *scalar) {
    yaml_node_t *target = &parser->nodes[node];
    bool is_null = false;
    if (scalar->quoted == 0u) {
        is_null = yaml_bytes_equal(scalar->text, scalar->length, (const uint8_t *)"~", 1u) ||
                  yaml_bytes_equal(scalar->text, scalar->length, (const uint8_t *)"null", 4u);
    }
    target->kind = is_null ? YAML_KIND_NULL : YAML_KIND_SCALAR;
    target->quoted = scalar->quoted;
    target->borrowed_text = scalar->text;
    target->text_length = scalar->length;
    target->line = scalar->line;
    target->column = scalar->column;
}

/* ---- Scanning scalars ------------------------------------------------------------------------------- */

static bool yaml_reserve_string(yaml_parser_t *parser, uint32_t bytes, uint8_t **out) {
    if (bytes > parser->string_capacity - parser->string_used) {
        return yaml_fail(parser, "out of string storage (capacity %u)", parser->string_capacity);
    }
    *out = parser->strings + parser->string_used;
    return true;
}

static bool yaml_scan_single_quoted(yaml_parser_t *parser, yaml_scalar_t *out) {
    uint32_t start = parser->pos + 1u;
    uint32_t cursor = start;
    uint32_t doubled_quotes = 0u;
    for (;;) {
        uint8_t byte = cursor < parser->length ? parser->text[cursor] : 0u;
        if (yaml_is_line_end(byte)) {
            return yaml_fail_at(parser, out->line, out->column,
                                "unterminated quoted scalar; scalars cannot span lines");
        }
        if (byte == '\'') {
            if (cursor + 1u < parser->length && parser->text[cursor + 1u] == '\'') {
                doubled_quotes++;
                cursor += 2u;
                continue;
            }
            break;
        }
        cursor++;
    }
    out->quoted = 1u;
    out->length = cursor - start - doubled_quotes;
    if (doubled_quotes == 0u) {
        out->text = parser->text + start;
    } else {
        uint8_t *decoded;
        if (!yaml_reserve_string(parser, out->length, &decoded)) {
            return false;
        }
        uint32_t written = 0u;
        for (uint32_t at = start; at < cursor; at++) {
            decoded[written++] = parser->text[at];
            at += parser->text[at] == '\'' ? 1u : 0u;
        }
        out->text = decoded;
        parser->string_used += written;
    }
    parser->pos = cursor + 1u;
    return true;
}

static bool yaml_hex_value(uint8_t byte, uint32_t *value) {
    if (byte >= '0' && byte <= '9') {
        *value = (uint32_t)byte - '0';
    } else if (byte >= 'a' && byte <= 'f') {
        *value = (uint32_t)byte - 'a' + 10u;
    } else if (byte >= 'A' && byte <= 'F') {
        *value = (uint32_t)byte - 'A' + 10u;
    } else {
        return false;
    }
    return true;
}

static uint32_t yaml_utf8_encode(uint32_t code_point, uint8_t *out) {
    if (code_point < 0x80u) {
        out[0] = (uint8_t)code_point;
        return 1u;
    }
    if (code_point < 0x800u) {
        out[0] = (uint8_t)(0xC0u | (code_point >> 6));
        out[1] = (uint8_t)(0x80u | (code_point & 0x3Fu));
        return 2u;
    }
    if (code_point < 0x10000u) {
        out[0] = (uint8_t)(0xE0u | (code_point >> 12));
        out[1] = (uint8_t)(0x80u | ((code_point >> 6) & 0x3Fu));
        out[2] = (uint8_t)(0x80u | (code_point & 0x3Fu));
        return 3u;
    }
    out[0] = (uint8_t)(0xF0u | (code_point >> 18));
    out[1] = (uint8_t)(0x80u | ((code_point >> 12) & 0x3Fu));
    out[2] = (uint8_t)(0x80u | ((code_point >> 6) & 0x3Fu));
    out[3] = (uint8_t)(0x80u | (code_point & 0x3Fu));
    return 4u;
}

/* Reads a \x, \u or \U escape's digits, with parser->pos at the backslash. Leaves pos after the digits. */
static bool yaml_read_numeric_escape(yaml_parser_t *parser, uint32_t digits, uint32_t *out_code_point) {
    uint32_t code_point = 0u;
    for (uint32_t i = 0u; i < digits; i++) {
        uint32_t value;
        if (!yaml_hex_value(yaml_peek_at(parser, 2u + i), &value)) {
            return yaml_fail(parser, "escape needs %u hex digits", digits);
        }
        code_point = (code_point << 4) | value;
    }
    if (code_point == 0u) {
        /* Tools print this text with C strings and into generated headers, where NUL could only do harm. */
        return yaml_fail(parser, "an escape may not produce a NUL");
    }
    if (code_point > 0x10FFFFu || (code_point >= 0xD800u && code_point <= 0xDFFFu)) {
        return yaml_fail(parser, "escape is not a valid Unicode scalar value");
    }
    *out_code_point = code_point;
    parser->pos += 2u + digits;
    return true;
}

/* Decodes one escape into decoded, with pos at the backslash. Leaves pos just after the escape. */
static bool yaml_decode_escape(yaml_parser_t *parser, uint8_t *decoded, uint32_t *written) {
    uint8_t name = yaml_peek_at(parser, 1u);
    uint8_t simple = 0u;
    switch (name) {
    case '\\':
        simple = '\\';
        break;
    case '"':
        simple = '"';
        break;
    case '/':
        simple = '/';
        break;
    case ' ':
        simple = ' ';
        break;
    case 'a':
        simple = 0x07u;
        break;
    case 'b':
        simple = 0x08u;
        break;
    case 'e':
        simple = 0x1Bu;
        break;
    case 'f':
        simple = 0x0Cu;
        break;
    case 'n':
        simple = 0x0Au;
        break;
    case 'r':
        simple = 0x0Du;
        break;
    case 't':
        simple = 0x09u;
        break;
    case 'v':
        simple = 0x0Bu;
        break;
    default:
        break;
    }
    if (simple != 0u) {
        decoded[(*written)++] = simple;
        parser->pos += 2u;
        return true;
    }
    uint32_t digits = name == 'x' ? 2u : name == 'u' ? 4u : name == 'U' ? 8u : 0u;
    if (digits == 0u) {
        return yaml_fail(parser, "unknown escape '\\%c'", name);
    }
    uint32_t code_point;
    if (!yaml_read_numeric_escape(parser, digits, &code_point)) {
        return false;
    }
    *written += yaml_utf8_encode(code_point, decoded + *written);
    return true;
}

static bool yaml_scan_escaped_quoted(yaml_parser_t *parser, yaml_scalar_t *out) {
    uint32_t start = parser->pos + 1u;
    uint32_t cursor = start;
    bool has_escape = false;
    for (;;) {
        uint8_t byte = cursor < parser->length ? parser->text[cursor] : 0u;
        if (yaml_is_line_end(byte)) {
            return yaml_fail_at(parser, out->line, out->column,
                                "unterminated quoted scalar; scalars cannot span lines");
        }
        if (byte == '"') {
            break;
        }
        if (byte == '\\') {
            has_escape = true;
            /* An escaped line end must still stop this scan, or it would run on into the next line. */
            uint8_t escaped = cursor + 1u < parser->length ? parser->text[cursor + 1u] : 0u;
            cursor += yaml_is_line_end(escaped) ? 1u : 2u;
            continue;
        }
        cursor++;
    }
    out->quoted = 1u;
    if (!has_escape) {
        out->text = parser->text + start;
        out->length = cursor - start;
        parser->pos = cursor + 1u;
        return true;
    }
    uint8_t *decoded;
    if (!yaml_reserve_string(parser, cursor - start, &decoded)) {
        return false;
    }
    uint32_t written = 0u;
    parser->pos = start;
    while (parser->pos < cursor) {
        uint8_t byte = parser->text[parser->pos];
        if (byte == '\\') {
            if (!yaml_decode_escape(parser, decoded, &written)) {
                return false;
            }
        } else {
            decoded[written++] = byte;
            parser->pos++;
        }
    }
    out->text = decoded;
    out->length = written;
    parser->string_used += written;
    parser->pos = cursor + 1u;
    return true;
}

/* Rejects what cannot start a plain scalar, naming the YAML feature when it is one we chose not to support. */
static bool yaml_check_plain_start(yaml_parser_t *parser, bool in_flow) {
    uint8_t byte = yaml_peek(parser);
    uint8_t next = yaml_peek_at(parser, 1u);
    switch (byte) {
    case '&':
        return yaml_fail(parser, "anchors are not supported");
    case '*':
        return yaml_fail(parser, "aliases are not supported");
    case '!':
        return yaml_fail(parser, "tags are not supported");
    case '|':
    case '>':
        return yaml_fail(parser, "block scalars are not supported");
    case '%':
    case '@':
    case '`':
        return yaml_fail(parser, "'%c' cannot start a plain scalar; quote it", byte);
    case '#':
        return yaml_fail(parser, "a comment needs whitespace before it");
    default:
        break;
    }
    if (yaml_is_flow_indicator(byte)) {
        return yaml_fail(parser, "unexpected '%c'", byte);
    }
    if (byte == '?' && yaml_is_space_or_end(next)) {
        return yaml_fail(parser, "complex keys are not supported");
    }
    if (byte == ':' && (yaml_is_space_or_end(next) || (in_flow && yaml_is_flow_indicator(next)))) {
        return yaml_fail(parser, "missing key before ':'");
    }
    if (byte == '-' && yaml_is_space_or_end(next)) {
        return yaml_fail(parser, "a block sequence item is not allowed here");
    }
    return true;
}

static bool yaml_scan_scalar(yaml_parser_t *parser, bool in_flow, yaml_scalar_t *out) {
    *out = (yaml_scalar_t){
        .line = parser->line,
        .column = parser->pos - parser->line_start + 1u,
    };
    uint8_t first = yaml_peek(parser);
    if (first == '\'') {
        return yaml_scan_single_quoted(parser, out);
    }
    if (first == '"') {
        return yaml_scan_escaped_quoted(parser, out);
    }
    if (!yaml_check_plain_start(parser, in_flow)) {
        return false;
    }
    uint32_t start = parser->pos;
    uint32_t end = start; /* just past the last byte that is not a blank */
    for (;;) {
        uint8_t byte = yaml_peek(parser);
        uint8_t next = yaml_peek_at(parser, 1u);
        if (yaml_is_line_end(byte)) {
            break;
        }
        if (byte == ':' && (yaml_is_space_or_end(next) || (in_flow && yaml_is_flow_indicator(next)))) {
            break;
        }
        if (byte == '#' && parser->pos > start && yaml_is_blank(parser->text[parser->pos - 1u])) {
            break;
        }
        if (in_flow && yaml_is_flow_indicator(byte)) {
            break;
        }
        parser->pos++;
        if (!yaml_is_blank(byte)) {
            end = parser->pos;
        }
    }
    out->text = parser->text + start;
    out->length = end - start;
    return true;
}

/* If a key's colon comes next, stops on it. Otherwise the position does not move. */
static bool yaml_key_colon_ahead(yaml_parser_t *parser, bool in_flow, bool quoted_key) {
    uint32_t before = parser->pos;
    yaml_skip_blanks(parser);
    if (yaml_peek(parser) == ':') {
        uint8_t next = yaml_peek_at(parser, 1u);
        /* In flow style, `"a":1` is valid since the quote already ended the key. */
        if (yaml_is_space_or_end(next) || (in_flow && (quoted_key || yaml_is_flow_indicator(next)))) {
            return true;
        }
    }
    parser->pos = before;
    return false;
}

/* ---- Flow collections ------------------------------------------------------------------------------- */

/* Skips whitespace, comments and line ends inside a flow collection. */
static bool yaml_skip_flow_space(yaml_parser_t *parser, int32_t parent_indent) {
    bool crossed_line = false;
    for (;;) {
        yaml_skip_blanks(parser);
        if (yaml_at_comment(parser)) {
            yaml_skip_to_line_end(parser);
        }
        uint8_t byte = yaml_peek(parser);
        if (byte == 0u || !yaml_is_line_end(byte)) {
            break;
        }
        yaml_next_line(parser);
        crossed_line = true;
    }
    if (crossed_line && parser->pos < parser->length) {
        int32_t column = (int32_t)(parser->pos - parser->line_start);
        if (column == 0 && (yaml_is_marker(parser, '-') || yaml_is_marker(parser, '.'))) {
            return yaml_fail(parser, "document marker inside a flow collection");
        }
        if (column <= parent_indent) {
            return yaml_fail(parser, "a flow collection's lines must be indented deeper than its parent block");
        }
    }
    return true;
}

/* A flow map's key and its colon. Leaves the position after the colon and any space. */
static bool yaml_parse_flow_key(yaml_parser_t *parser, int32_t parent_indent, uint32_t *out_child) {
    uint8_t byte = yaml_peek(parser);
    if (byte == '{' || byte == '[') {
        return yaml_fail(parser, "a collection cannot be a key");
    }
    yaml_scalar_t key;
    if (!yaml_scan_scalar(parser, true, &key)) {
        return false;
    }
    if (!yaml_key_colon_ahead(parser, true, key.quoted != 0u)) {
        return yaml_fail_at(parser, key.line, key.column, "a flow mapping entry needs 'key: value'");
    }
    if (!yaml_add_key(parser, &key, out_child)) {
        return false;
    }
    parser->pos++;
    return yaml_skip_flow_space(parser, parent_indent);
}

/* Parses one flow collection into node, with the position on its opening bracket. */
static bool yaml_parse_flow(yaml_parser_t *parser, uint32_t node, int32_t parent_indent) {
    uint32_t base_depth = parser->depth;
    uint32_t open_line = parser->line;
    uint32_t open_column = parser->pos - parser->line_start + 1u;
    yaml_kind_t kind = yaml_peek(parser) == '{' ? YAML_KIND_MAP : YAML_KIND_SEQ;
    if (!yaml_open_collection(parser, node, kind, parent_indent)) {
        return false;
    }
    parser->pos++;
    while (parser->depth > base_depth) {
        if (!yaml_skip_flow_space(parser, parent_indent)) {
            return false;
        }
        uint8_t byte = yaml_peek(parser);
        if (byte == 0u) {
            return yaml_fail_at(parser, open_line, open_column, "unterminated flow collection");
        }
        yaml_frame_t *frame = &parser->frames[parser->depth - 1u];
        bool in_map = yaml_top_kind(parser) == YAML_KIND_MAP;
        uint8_t closer = in_map ? '}' : ']';
        if (byte == closer) {
            parser->pos++;
            parser->depth--;
            continue;
        }
        if (frame->after_value != 0u) {
            if (byte != ',') {
                return yaml_fail(parser, "expected ',' or '%c'", closer);
            }
            parser->pos++;
            frame->after_value = 0u;
            continue;
        }
        if (byte == ',') {
            return yaml_fail(parser, "empty entry before ','");
        }

        uint32_t child;
        if (in_map) {
            if (!yaml_parse_flow_key(parser, parent_indent, &child)) {
                return false;
            }
            byte = yaml_peek(parser);
            /* The frame may have gained a child, but a parsed key never moves the frame itself. */
            frame->after_value = 1u;
            if (byte == 0u) {
                return yaml_fail_at(parser, open_line, open_column, "unterminated flow collection");
            }
            if (byte == ',' || byte == closer) {
                continue; /* a key with no value is null */
            }
        } else {
            frame->after_value = 1u;
            if (!yaml_add_child(parser, &child)) {
                return false;
            }
        }

        if (byte == '{' || byte == '[') {
            if (!yaml_open_collection(parser, child, byte == '{' ? YAML_KIND_MAP : YAML_KIND_SEQ, parent_indent)) {
                return false;
            }
            parser->pos++;
        } else {
            yaml_scalar_t scalar;
            if (!yaml_scan_scalar(parser, true, &scalar)) {
                return false;
            }
            yaml_set_scalar(parser, child, &scalar);
        }
    }
    return true;
}

/* ---- Block structure -------------------------------------------------------------------------------- */

/* A value on the same line as its key or dash: scalar or flow collection, then nothing else. */
static bool yaml_parse_inline_value(yaml_parser_t *parser, uint32_t node, int32_t parent_indent) {
    uint8_t byte = yaml_peek(parser);
    if (byte == '{' || byte == '[') {
        if (!yaml_parse_flow(parser, node, parent_indent)) {
            return false;
        }
    } else {
        yaml_scalar_t scalar;
        if (!yaml_scan_scalar(parser, false, &scalar)) {
            return false;
        }
        if (yaml_key_colon_ahead(parser, false, scalar.quoted != 0u)) {
            return yaml_fail_at(parser, scalar.line, scalar.column, "a mapping cannot start on the same line as a key");
        }
        yaml_set_scalar(parser, node, &scalar);
    }
    return yaml_finish_line(parser);
}

/* Adds an entry to the mapping on top of the stack, with the position on the key's colon. */
static bool yaml_parse_map_entry(yaml_parser_t *parser, const yaml_scalar_t *key, int32_t column) {
    uint32_t child;
    if (!yaml_add_key(parser, key, &child)) {
        return false;
    }
    parser->pos++;
    yaml_skip_blanks(parser);
    if (yaml_is_line_end(yaml_peek(parser)) || yaml_peek(parser) == '#') {
        parser->pending = child;
        parser->pending_indent = column;
        return yaml_finish_line(parser);
    }
    return yaml_parse_inline_value(parser, child, column);
}

/*
 * Closes every block the column has left. A sequence may sit at the same column as its parent key, so a
 * non-dash line at that column ends the sequence and belongs to the mapping below it.
 */
static void yaml_close_blocks(yaml_parser_t *parser, int32_t column, bool is_seq_item) {
    while (parser->depth > 0u) {
        const yaml_frame_t *top = &parser->frames[parser->depth - 1u];
        bool left_shared_column = top->indent == column && !is_seq_item && yaml_top_kind(parser) == YAML_KIND_SEQ &&
                                  parser->depth >= 2u && parser->frames[parser->depth - 2u].indent == column;
        if (top->indent <= column && !left_shared_column) {
            return;
        }
        parser->depth--;
    }
}

/* Parses the content of one line, starting at its first byte that is not a space. Stops at the line end. */
static bool yaml_parse_block_line(yaml_parser_t *parser) {
    int32_t column = (int32_t)(parser->pos - parser->line_start);
    for (;;) { /* one turn per dash, so `- - a` takes three */
        uint8_t byte = yaml_peek(parser);
        bool is_seq_item = byte == '-' && yaml_is_space_or_end(yaml_peek_at(parser, 1u));
        bool have_key = false;
        yaml_scalar_t key = {0};

        if (parser->pending != YAML_NO_NODE) {
            uint32_t pending = parser->pending;
            int32_t pending_indent = parser->pending_indent;
            bool parent_is_map = parser->depth > 0u && yaml_top_kind(parser) == YAML_KIND_MAP;
            parser->pending = YAML_NO_NODE;
            if (column > pending_indent || (is_seq_item && column == pending_indent && parent_is_map)) {
                /* This line is the pending node's value. */
                if (is_seq_item) {
                    if (!yaml_open_collection(parser, pending, YAML_KIND_SEQ, column)) {
                        return false;
                    }
                } else if (byte == '{' || byte == '[') {
                    return yaml_parse_flow(parser, pending, pending_indent) && yaml_finish_line(parser);
                } else {
                    if (!yaml_scan_scalar(parser, false, &key)) {
                        return false;
                    }
                    if (!yaml_key_colon_ahead(parser, false, key.quoted != 0u)) {
                        yaml_set_scalar(parser, pending, &key);
                        return yaml_finish_line(parser);
                    }
                    if (!yaml_open_collection(parser, pending, YAML_KIND_MAP, column)) {
                        return false;
                    }
                    have_key = true;
                }
            }
        }

        yaml_close_blocks(parser, column, is_seq_item);
        if (parser->depth == 0u) {
            return yaml_fail(parser, "content after the end of the document's root value");
        }
        if (parser->frames[parser->depth - 1u].indent != column) {
            return yaml_fail(parser, "indentation does not match any open block; scalars cannot span lines");
        }

        if (is_seq_item) {
            if (yaml_top_kind(parser) != YAML_KIND_SEQ) {
                return yaml_fail(parser, "a sequence item where a mapping key was expected");
            }
            uint32_t child;
            if (!yaml_add_child(parser, &child)) {
                return false;
            }
            parser->pos++;
            yaml_skip_blanks(parser);
            parser->pending = child;
            parser->pending_indent = column;
            if (yaml_is_line_end(yaml_peek(parser)) || yaml_peek(parser) == '#') {
                return yaml_finish_line(parser);
            }
            column = (int32_t)(parser->pos - parser->line_start);
            continue;
        }

        if (yaml_top_kind(parser) != YAML_KIND_MAP) {
            return yaml_fail(parser, "expected a '- ' sequence item");
        }
        if (!have_key) {
            if (byte == '{' || byte == '[') {
                return yaml_fail(parser, "a collection cannot be a key");
            }
            if (!yaml_scan_scalar(parser, false, &key)) {
                return false;
            }
            if (!yaml_key_colon_ahead(parser, false, key.quoted != 0u)) {
                return yaml_fail_at(parser, key.line, key.column, "expected 'key: value'");
            }
        }
        return yaml_parse_map_entry(parser, &key, column);
    }
}

static bool yaml_parse_document(yaml_parser_t *parser) {
    uint32_t root;
    if (!yaml_new_node(parser, &root)) {
        return false;
    }
    parser->pending = root;
    parser->pending_indent = -1;
    bool seen_content = false;
    bool ended = false;

    for (;;) {
        while (yaml_peek(parser) == ' ') {
            parser->pos++;
        }
        uint8_t byte = yaml_peek(parser);
        if (byte == '\t') {
            uint32_t tab_at = parser->pos;
            yaml_skip_blanks(parser);
            byte = yaml_peek(parser);
            if (!yaml_is_line_end(byte) && byte != '#') {
                parser->pos = tab_at;
                return yaml_fail(parser, "tabs are not allowed in indentation");
            }
        }
        if (byte == '#') {
            yaml_skip_to_line_end(parser);
        } else if (!yaml_is_line_end(byte)) {
            bool at_line_start = parser->pos == parser->line_start;
            if (at_line_start && yaml_is_marker(parser, '-')) {
                if (seen_content) {
                    return yaml_fail(parser, "only one document per file is supported");
                }
                seen_content = true;
                parser->pos += 3u;
                if (!yaml_finish_line(parser)) {
                    return false;
                }
            } else if (at_line_start && yaml_is_marker(parser, '.')) {
                parser->pos += 3u;
                if (!yaml_finish_line(parser)) {
                    return false;
                }
                ended = true;
            } else if (ended) {
                return yaml_fail(parser, "content after the '...' end marker");
            } else if (at_line_start && byte == '%') {
                return yaml_fail(parser, "directives are not supported");
            } else {
                seen_content = true;
                if (!yaml_parse_block_line(parser)) {
                    return false;
                }
            }
        }
        if (yaml_peek(parser) == 0u) {
            return true;
        }
        yaml_next_line(parser);
    }
}

bool yaml_parse(yaml_document_t *document, const uint8_t *text, uint64_t length, yaml_storage_t storage,
                yaml_error_t *error) {
    *document = (yaml_document_t){0};
    *error = (yaml_error_t){0};
    yaml_parser_t parser = {
        .text = text,
        .nodes = storage.nodes,
        .strings = storage.strings,
        .error = error,
        .line = 1u,
        .node_capacity = storage.node_capacity,
        .string_capacity = storage.string_capacity,
        .pending = YAML_NO_NODE,
    };
    if (length > YAML_MAX_TEXT_BYTES) {
        return yaml_fail(&parser, "text is larger than the limit of %u bytes", YAML_MAX_TEXT_BYTES);
    }
    parser.length = (uint32_t)length;
    if (!yaml_validate_text(&parser)) {
        return false;
    }
    /* A byte order mark is legal at the very start. It does not count towards the first line's columns. */
    parser.pos = 0u;
    parser.line = 1u;
    parser.line_start = 0u;
    if (parser.length >= 3u && text[0] == 0xEFu && text[1] == 0xBBu && text[2] == 0xBFu) {
        parser.pos = 3u;
        parser.line_start = 3u;
    }
    if (!yaml_parse_document(&parser)) {
        return false;
    }
    document->borrowed_nodes = storage.nodes;
    document->node_count = parser.node_count;
    return true;
}

/* ---- Walking the tree ------------------------------------------------------------------------------- */

const yaml_node_t *yaml_root(const yaml_document_t *document) {
    return document->node_count > 0u ? &document->borrowed_nodes[0] : nullptr;
}

const yaml_node_t *yaml_first_child(const yaml_document_t *document, const yaml_node_t *node) {
    return node->first_child != YAML_NO_NODE ? &document->borrowed_nodes[node->first_child] : nullptr;
}

const yaml_node_t *yaml_next_sibling(const yaml_document_t *document, const yaml_node_t *node) {
    return node->next_sibling != YAML_NO_NODE ? &document->borrowed_nodes[node->next_sibling] : nullptr;
}

static const yaml_node_t *yaml_map_get_bytes(const yaml_document_t *document, const yaml_node_t *map,
                                             const uint8_t *key, uint32_t key_length) {
    if (map == nullptr || map->kind != YAML_KIND_MAP) {
        return nullptr;
    }
    for (const yaml_node_t *child = yaml_first_child(document, map); child != nullptr;
         child = yaml_next_sibling(document, child)) {
        if (yaml_bytes_equal(child->borrowed_key, child->key_length, key, key_length)) {
            return child;
        }
    }
    return nullptr;
}

const yaml_node_t *yaml_map_get(const yaml_document_t *document, const yaml_node_t *map, const char *key) {
    uint64_t key_length = strlen(key);
    if (key_length > YAML_MAX_TEXT_BYTES) {
        return nullptr;
    }
    return yaml_map_get_bytes(document, map, (const uint8_t *)key, (uint32_t)key_length);
}

const yaml_node_t *yaml_seq_at(const yaml_document_t *document, const yaml_node_t *seq, uint32_t position) {
    if (seq == nullptr || seq->kind != YAML_KIND_SEQ || position >= seq->child_count) {
        return nullptr;
    }
    const yaml_node_t *child = yaml_first_child(document, seq);
    for (uint32_t i = 0u; i < position; i++) {
        child = yaml_next_sibling(document, child);
    }
    return child;
}

const yaml_node_t *yaml_path_get(const yaml_document_t *document, const yaml_node_t *map, const char *path) {
    const yaml_node_t *node = map;
    const char *segment = path;
    while (node != nullptr) {
        const char *dot = strchr(segment, '.');
        uint64_t segment_length = dot != nullptr ? (uint64_t)(dot - segment) : strlen(segment);
        if (segment_length > YAML_MAX_TEXT_BYTES) {
            return nullptr;
        }
        node = yaml_map_get_bytes(document, node, (const uint8_t *)segment, (uint32_t)segment_length);
        if (dot == nullptr) {
            return node;
        }
        segment = dot + 1;
    }
    return nullptr;
}

const yaml_node_t *yaml_path_require(const yaml_document_t *document, const yaml_node_t *map, const char *path,
                                     yaml_error_t *error) {
    const yaml_node_t *node = yaml_path_get(document, map, path);
    if (node == nullptr) {
        yaml_fail_at_node(map, error, "missing key '%s'", path);
    }
    return node;
}

/* ---- Typed reads ------------------------------------------------------------------------------------ */

bool yaml_text_equals(const yaml_node_t *node, const char *text) {
    uint64_t length = strlen(text);
    if (node == nullptr || node->kind != YAML_KIND_SCALAR || length != node->text_length) {
        return false;
    }
    return yaml_bytes_equal(node->borrowed_text, node->text_length, (const uint8_t *)text, node->text_length);
}

static bool yaml_fail_type(const yaml_node_t *node, yaml_error_t *error, const char *expected) {
    if (node == nullptr) {
        return yaml_fail_at_node(node, error, "expected %s, but there is no value", expected);
    }
    int32_t key_length = (int32_t)(node->key_length > 32u ? 32u : node->key_length);
    const char *key = node->borrowed_key != nullptr ? (const char *)node->borrowed_key : "";
    const char *key_prefix = node->borrowed_key != nullptr ? "key '" : "";
    const char *key_suffix = node->borrowed_key != nullptr ? "': " : "";
    if (node->kind == YAML_KIND_SCALAR) {
        return yaml_fail_at_node(node, error, "%s%.*s%sexpected %s, got '%.*s'", key_prefix, key_length, key,
                                 key_suffix, expected, (int32_t)(node->text_length > 32u ? 32u : node->text_length),
                                 (const char *)node->borrowed_text);
    }
    const char *actual = node->kind == YAML_KIND_MAP   ? "a mapping"
                         : node->kind == YAML_KIND_SEQ ? "a sequence"
                                                       : "null";
    return yaml_fail_at_node(node, error, "%s%.*s%sexpected %s, got %s", key_prefix, key_length, key, key_suffix,
                             expected, actual);
}

static bool yaml_is_plain_scalar(const yaml_node_t *node) {
    return node != nullptr && node->kind == YAML_KIND_SCALAR && node->quoted == 0u;
}

bool yaml_get_bool(const yaml_node_t *node, bool *out, yaml_error_t *error) {
    if (yaml_is_plain_scalar(node)) {
        if (yaml_text_equals(node, "true")) {
            *out = true;
            return true;
        }
        if (yaml_text_equals(node, "false")) {
            *out = false;
            return true;
        }
    }
    return yaml_fail_type(node, error, "true or false");
}

/* Accumulates a magnitude, refusing to pass limit. Returns false on overflow. */
static bool yaml_accumulate(uint64_t *magnitude, uint32_t base, uint32_t digit, uint64_t limit) {
    if (*magnitude > (limit - digit) / base) {
        return false;
    }
    *magnitude = *magnitude * base + digit;
    return true;
}

/* -(2^63) has no positive twin, so the limit depends on the sign. */
static int64_t yaml_apply_sign(uint64_t magnitude, bool negative) {
    if (!negative) {
        return (int64_t)magnitude;
    }
    return magnitude == (uint64_t)INT64_MAX + 1u ? INT64_MIN : -(int64_t)magnitude;
}

bool yaml_get_int64(const yaml_node_t *node, int64_t *out, yaml_error_t *error) {
    const char *expected = "an integer in the 64-bit range";
    if (!yaml_is_plain_scalar(node)) {
        return yaml_fail_type(node, error, expected);
    }
    const uint8_t *text = node->borrowed_text;
    uint32_t length = node->text_length;
    uint32_t at = 0u;
    bool negative = false;
    if (at < length && (text[at] == '-' || text[at] == '+')) {
        negative = text[at] == '-';
        at++;
    }
    uint32_t base = 10u;
    if (length - at > 2u && text[at] == '0' && text[at + 1u] == 'x') {
        base = 16u;
        at += 2u;
    }
    if (at >= length) {
        return yaml_fail_type(node, error, expected);
    }
    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1u : (uint64_t)INT64_MAX;
    uint64_t magnitude = 0u;
    for (; at < length; at++) {
        uint32_t digit;
        if (!yaml_hex_value(text[at], &digit) || digit >= base || !yaml_accumulate(&magnitude, base, digit, limit)) {
            return yaml_fail_type(node, error, expected);
        }
    }
    *out = yaml_apply_sign(magnitude, negative);
    return true;
}

static bool yaml_is_digit(uint8_t byte) {
    return byte >= '0' && byte <= '9';
}

bool yaml_get_decimal(const yaml_node_t *node, yaml_decimal_t *out, yaml_error_t *error) {
    const char *expected = "a decimal number";
    if (!yaml_is_plain_scalar(node)) {
        return yaml_fail_type(node, error, expected);
    }
    const uint8_t *text = node->borrowed_text;
    uint32_t length = node->text_length;
    uint32_t at = 0u;
    bool negative = false;
    if (at < length && (text[at] == '-' || text[at] == '+')) {
        negative = text[at] == '-';
        at++;
    }
    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1u : (uint64_t)INT64_MAX;
    uint64_t magnitude = 0u;
    int64_t exponent = 0;
    uint32_t digits = 0u;
    bool in_fraction = false;
    for (; at < length; at++) {
        uint8_t byte = text[at];
        if (byte == '.' && !in_fraction) {
            in_fraction = true;
            continue;
        }
        if (!yaml_is_digit(byte)) {
            break;
        }
        if (!yaml_accumulate(&magnitude, 10u, (uint32_t)byte - '0', limit)) {
            return yaml_fail_type(node, error, expected);
        }
        exponent -= in_fraction ? 1 : 0;
        digits++;
    }
    if (digits == 0u) {
        return yaml_fail_type(node, error, expected);
    }
    if (at < length && (text[at] == 'e' || text[at] == 'E')) {
        at++;
        bool exponent_negative = false;
        if (at < length && (text[at] == '-' || text[at] == '+')) {
            exponent_negative = text[at] == '-';
            at++;
        }
        if (at >= length) {
            return yaml_fail_type(node, error, expected);
        }
        uint64_t written_exponent = 0u;
        for (; at < length; at++) {
            if (!yaml_is_digit(text[at]) ||
                !yaml_accumulate(&written_exponent, 10u, (uint32_t)text[at] - '0', 1000000u)) {
                return yaml_fail_type(node, error, expected);
            }
        }
        exponent += exponent_negative ? -(int64_t)written_exponent : (int64_t)written_exponent;
    }
    if (at != length) {
        return yaml_fail_type(node, error, expected);
    }
    /* At most 2^31 fraction digits from the text limit plus a capped exponent, but it must still fit the field. */
    if (exponent < INT32_MIN || exponent > INT32_MAX) {
        return yaml_fail_type(node, error, expected);
    }
    *out = (yaml_decimal_t){.mantissa = yaml_apply_sign(magnitude, negative), .exponent = (int32_t)exponent};
    return true;
}

bool yaml_get_text(const yaml_node_t *node, const uint8_t **out_text, uint32_t *out_length, yaml_error_t *error) {
    if (node == nullptr || node->kind != YAML_KIND_SCALAR) {
        return yaml_fail_type(node, error, "text");
    }
    *out_text = node->borrowed_text;
    *out_length = node->text_length;
    return true;
}
