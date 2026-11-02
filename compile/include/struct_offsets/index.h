/*
 * The struct_offsets tool: C headers in, the layout of their structs out, for NASM. See
 * compile/src/struct_offsets/README.md. Only the tool's own index.c includes this; every other consumer reads the
 * generated build/<preset>/generated/struct_offsets/index.{inc,h}.
 */
#ifndef STRUCT_OFFSETS_INDEX_H
#define STRUCT_OFFSETS_INDEX_H

#include <stdint.h>

enum : uint64_t {
    /* A header is a few KiB. A limit keeps a wrong path from filling the arena. */
    STRUCT_OFFSETS_MAX_INPUT_BYTES = 1u << 20,
    /* Each generated file is a line or two per field. Running out is reported, not truncated. */
    STRUCT_OFFSETS_OUTPUT_BYTES = 1u << 20,
    STRUCT_OFFSETS_MAX_STRUCTS = 256u,
    STRUCT_OFFSETS_MAX_FIELDS = 256u,
    STRUCT_OFFSETS_NAME_BYTES = 64u,
    STRUCT_OFFSETS_POINTER_BYTES = 8u,
    /* Larger than one struct can sensibly be, and small enough that no sum here can overflow. */
    STRUCT_OFFSETS_MAX_ARRAY = 1u << 24,
    STRUCT_OFFSETS_MAX_ALIGN = 4096u,
    STRUCT_OFFSETS_ARENA_BYTES = 1u << 30, /* reserved, not used: only touched pages cost anything */
};

/* Exit status, the same convention as standards_lint and machine_config. */
enum {
    STRUCT_OFFSETS_EXIT_OK = 0,
    STRUCT_OFFSETS_EXIT_REFUSED = 1,
    STRUCT_OFFSETS_EXIT_CANNOT_RUN = 2,
};

typedef struct offsets_field_t {
    char name[STRUCT_OFFSETS_NAME_BYTES];
    uint64_t offset;
} offsets_field_t;
static_assert(sizeof(offsets_field_t) == 72);

typedef struct offsets_struct_t {
    char name[STRUCT_OFFSETS_NAME_BYTES];
    offsets_field_t *borrowed_fields; /* in the tool's arena */
    uint64_t size;
    uint64_t align;
    uint64_t field_count;
} offsets_struct_t;
static_assert(sizeof(offsets_struct_t) == 96);

typedef enum offsets_token_kind_t : uint8_t {
    OFFSETS_TOKEN_END = 0,
    OFFSETS_TOKEN_WORD = 1,   /* an identifier or keyword */
    OFFSETS_TOKEN_NUMBER = 2, /* a whole number, with any u, l or similar suffix */
    OFFSETS_TOKEN_MARK = 3,   /* one character of punctuation */
} offsets_token_kind_t;

/* A place in the header being read, and the token just taken from it. */
typedef struct offsets_scanner_t {
    const char *borrowed_path;
    const char *borrowed_cursor;
    const char *borrowed_end;
    const char *borrowed_token; /* not NUL-terminated; token_length long */
    uint64_t token_length;
    uint64_t token_number; /* the value, for OFFSETS_TOKEN_NUMBER */
    uint32_t line;         /* of the cursor */
    uint32_t token_line;
    uint8_t token_kind;    /* an offsets_token_kind_t */
    uint8_t at_line_start; /* only blanks since the last newline, so a # here is a preprocessor line */
    uint8_t _pad0[6];
} offsets_scanner_t;
static_assert(sizeof(offsets_scanner_t) == 64);

/* The process arena, and everything found so far. */
typedef struct offsets_tool_t {
    uint8_t *owned_arena;
    offsets_struct_t *borrowed_structs;
    uint64_t arena_used;
    uint64_t struct_count;
} offsets_tool_t;
static_assert(sizeof(offsets_tool_t) == 32);

/* An output being built. Nothing reaches the disk until every header has been accepted. */
typedef struct offsets_output_t {
    char *borrowed_text;
    uint64_t length;
    uint8_t overflowed;
    uint8_t _pad0[7];
} offsets_output_t;
static_assert(sizeof(offsets_output_t) == 24);

/* What one declaration line says: its alignas and its type, shared by each name declared on the line. */
typedef struct offsets_declaration_t {
    uint64_t type_size;
    uint64_t type_align;
    uint64_t alignas_value; /* 1 if none */
    uint8_t type_known;     /* otherwise only a pointer to it can be laid out */
    uint8_t _pad0[7];
    char type_name[STRUCT_OFFSETS_NAME_BYTES];
} offsets_declaration_t;
static_assert(sizeof(offsets_declaration_t) == 96);

#endif
