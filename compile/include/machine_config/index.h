/*
 * The machine_config tool: config/machine.yaml in, one generated header per language out. See
 * compile/src/machine_config/README.md. Only the tool's own index.c includes this; every other consumer reads
 * the generated build/<preset>/generated/machine/index.{h,hu,inc,cmake}.
 */
#ifndef MACHINE_CONFIG_INDEX_H
#define MACHINE_CONFIG_INDEX_H

#include "yaml/index.h"

#include <stdint.h>

/* machine.yaml is about 1 KiB. A limit keeps a wrong path, such as a disk image, from mapping gigabytes of nodes. */
#define MACHINE_CONFIG_MAX_INPUT_BYTES (1u << 20)
/* Every generated file is a few KiB, bounded by the key table and the input. Running out is reported, not truncated. */
#define MACHINE_CONFIG_OUTPUT_BYTES (1u << 18)
/* The longest dotted key, such as gpu.budget.textures_mib, with room to report a wrong one. */
#define MACHINE_CONFIG_PATH_BYTES 128u
#define MACHINE_CONFIG_MAX_NESTING 4u

/* Exit status, the same convention as standards_lint. */
#define MACHINE_CONFIG_EXIT_OK 0
#define MACHINE_CONFIG_EXIT_BAD_CONFIG 1
#define MACHINE_CONFIG_EXIT_CANNOT_RUN 2

typedef enum machine_kind_t : uint8_t {
    MACHINE_KIND_BOOL = 0,   /* 1 or 0 in every language, so #if and if() both work */
    MACHINE_KIND_INT = 1,    /* checked against min and max */
    MACHINE_KIND_TEXT = 2,   /* printable ASCII that needs no escaping in C, NASM or CMake */
    MACHINE_KIND_WORD = 3,   /* letters, digits, . _ and -, because it may end up in a compiler flag */
    MACHINE_KIND_CHOICE = 4, /* a word from a fixed list, which also gets a MC_<KEY>_<WORD> define for #if */
} machine_kind_t;

/* One row of the table of keys machine.yaml must hold. */
typedef struct machine_key_t {
    const char *path;    /* 0: dotted, such as "threads.workers" */
    const char *macro;   /* 8: written in full, so a grep for the define finds this row */
    const char *choices; /* 16: comma-separated, for MACHINE_KIND_CHOICE; otherwise nullptr */
    int64_t min;         /* 24 */
    int64_t max;         /* 32 */
    uint8_t kind;        /* 40: a machine_kind_t */
    uint8_t _pad0[7];    /* 41 */
} machine_key_t;
static_assert(sizeof(machine_key_t) == 48);

/* One key's checked value. */
typedef struct machine_value_t {
    const yaml_node_t *borrowed_node; /* 0: for later complaints, with the line */
    const uint8_t *borrowed_text;     /* 8: into the source or the yaml strings; not NUL-terminated */
    int64_t number;                   /* 16: bool and int values */
    uint32_t text_length;             /* 24 */
    uint8_t _pad0[4];                 /* 28 */
} machine_value_t;
static_assert(sizeof(machine_value_t) == 32);

/* The syntax of one output language. */
typedef struct machine_format_t {
    const char *file_name;   /* 0 */
    const char *preamble;    /* 8 */
    const char *closing;     /* 16 */
    const char *line_before; /* 24: before the macro name */
    const char *line_middle; /* 32: between name and value */
    const char *line_after;  /* 40 */
    uint8_t wrap_negative;   /* 48: write -5 as (-5), as a C macro must */
    uint8_t _pad0[7];        /* 49 */
} machine_format_t;
static_assert(sizeof(machine_format_t) == 56);

/* An output under construction, in the tool's region. */
typedef struct machine_output_t {
    uint8_t *borrowed_bytes; /* 0 */
    uint64_t length;         /* 8 */
    uint8_t overflowed;      /* 16 */
    uint8_t _pad0[7];        /* 17 */
} machine_output_t;
static_assert(sizeof(machine_output_t) == 24);

/* The tool's one process region and what is carved out of it. Lives on main's stack. */
typedef struct machine_config_t {
    uint8_t *owned_region;     /* 0: the only mapping, released by machine_config_deinit */
    uint8_t *borrowed_text;    /* 8 */
    uint8_t *borrowed_scratch; /* 16: reads back an existing output to compare it */
    uint8_t *borrowed_outputs; /* 24 */
    uint64_t region_bytes;     /* 32 */
    uint64_t text_length;      /* 40 */
    yaml_storage_t storage;    /* 48 */
} machine_config_t;
static_assert(sizeof(machine_config_t) == 72);

#endif
