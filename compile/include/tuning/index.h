/*
 * The tuning tool: compile/config/<path>/index.yaml in, a header of MC_TUNE_* defines out for each. See
 * compile/src/tuning/README.md. Only the tool's own index.c includes this; the game reads the generated
 * build/<preset>/generated/tuning/<path>/index.{h,hu} and tuning/index.h.
 */
#ifndef TUNING_INDEX_H
#define TUNING_INDEX_H

#include "yaml/index.h"

#include <stdint.h>

/* Exit status, the same convention as machine_config and game_tables. */
#define TUNING_EXIT_OK 0
#define TUNING_EXIT_REFUSED 1    /* a file is wrong; every problem was printed as path:line:column: message */
#define TUNING_EXIT_CANNOT_RUN 2 /* a bad command line, an unreadable input or an unwritable output */

/* Limits that keep a wrong --config, such as a home directory, from reading the world. Running into one is refused. */
#define TUNING_MAX_FILES 256u
#define TUNING_MAX_KEYS 2048u
#define TUNING_MAX_INPUT_BYTES (1u << 20) /* every file together */
#define TUNING_MAX_DEPTH 8u               /* directories below --config */
#define TUNING_PATH_BYTES 256u            /* a file's directory below --config, such as render/shade */
#define TUNING_DEFINE_BYTES 160u          /* MC_TUNE_<PATH>_<KEY> */
#define TUNING_NUMBER_BYTES 48u           /* a value, min or max as written back: 16, 3.0, (-2), true */
#define TUNING_OUTPUT_BYTES (1u << 20)    /* one generated file */

typedef enum tuning_type_t : uint8_t {
    TUNING_TYPE_U32 = 0,
    TUNING_TYPE_I32 = 1,
    TUNING_TYPE_F32 = 2,
    TUNING_TYPE_BOOL = 3,
} tuning_type_t;

/* One index.yaml, read and parsed. */
typedef struct tuning_file_t {
    char path[TUNING_PATH_BYTES]; /* 0: its directory below --config, NUL-terminated: render/shade */
    uint8_t *borrowed_text;       /* 256: into the region */
    uint64_t text_length;         /* 264 */
    yaml_storage_t storage;       /* 272: its slice of the region's nodes and strings */
    yaml_document_t document;     /* 296 */
    uint32_t first_key;           /* 312: its keys in tuning_t.borrowed_keys */
    uint32_t key_count;           /* 316 */
    uint8_t parsed;               /* 320: 1 if the yaml parsed */
    uint8_t has_f32;              /* 321: its .h includes types/index.h for F32 */
    uint8_t _pad0[6];             /* 322 */
} tuning_file_t;
static_assert(sizeof(tuning_file_t) == 328);

/* One key, checked. The texts are as the outputs and the hash write them, NUL-terminated. */
typedef struct tuning_key_t {
    const yaml_node_t *borrowed_node;  /* 0: the key itself, for its line */
    char define[TUNING_DEFINE_BYTES];  /* 8: MC_TUNE_RENDER_SHADE_AO_TURNS */
    char value[TUNING_NUMBER_BYTES];   /* 168: 4, 3.0, -2, true */
    char minimum[TUNING_NUMBER_BYTES]; /* 216: empty for bool */
    char maximum[TUNING_NUMBER_BYTES]; /* 264: empty for bool */
    const uint8_t *borrowed_name;      /* 312: the key as written, into the source */
    const uint8_t *borrowed_unit;      /* 320 */
    uint32_t name_length;              /* 328 */
    uint32_t unit_length;              /* 332 */
    uint32_t file;                     /* 336: which tuning_file_t */
    uint8_t type;                      /* 340: a tuning_type_t */
    uint8_t _pad0[3];                  /* 341 */
} tuning_key_t;
static_assert(sizeof(tuning_key_t) == 344);

/* An output under construction. */
typedef struct tuning_output_t {
    uint8_t *borrowed_bytes; /* 0 */
    uint64_t length;         /* 8 */
    uint8_t overflowed;      /* 16 */
    uint8_t _pad0[7];        /* 17 */
} tuning_output_t;
static_assert(sizeof(tuning_output_t) == 24);

/* The tool's one process region and what is carved out of it. Lives on main's stack. */
typedef struct tuning_t {
    uint8_t *owned_region;         /* 0: the only mapping, released by tuning_deinit */
    tuning_file_t *borrowed_files; /* 8: TUNING_MAX_FILES */
    tuning_key_t *borrowed_keys;   /* 16: TUNING_MAX_KEYS */
    uint8_t *borrowed_text;        /* 24: TUNING_MAX_INPUT_BYTES, every file one after another */
    yaml_node_t *borrowed_nodes;   /* 32: one more than a byte of text, per file */
    uint8_t *borrowed_strings;     /* 40: as many as text bytes */
    uint8_t *borrowed_output;      /* 48: TUNING_OUTPUT_BYTES */
    uint8_t *borrowed_scratch;     /* 56: TUNING_OUTPUT_BYTES + 1, to read back an existing output */
    uint64_t region_bytes;         /* 64 */
    uint64_t text_used;            /* 72 */
    uint64_t nodes_used;           /* 80 */
    uint32_t file_count;           /* 88 */
    uint32_t key_count;            /* 92 */
    uint32_t error_count;          /* 96 */
    uint8_t _pad0[4];              /* 100 */
} tuning_t;
static_assert(sizeof(tuning_t) == 104);

#endif
