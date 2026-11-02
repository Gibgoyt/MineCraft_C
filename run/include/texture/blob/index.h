/* See run/src/texture/blob/README.md. */
#ifndef MC_RUN_TEXTURE_BLOB_INDEX_H
#define MC_RUN_TEXTURE_BLOB_INDEX_H

#include "types/texture_blob/index.h"

#include <stdint.h>

/* A checked blob. Borrows its bytes, and must not outlive them: the embedded ones last as long as the process. */
typedef struct texture_blob_view_t {
    const texture_blob_header_t *borrowed_header; /* 0 */
    const texture_blob_entry_t *borrowed_entries; /* 8 */
    const uint8_t *borrowed_names;                /* 16 */
    const uint8_t *borrowed_pixels;               /* 24: on a 64-byte boundary */
    const uint8_t *borrowed_vectors;              /* 32: on a 32-byte boundary */
} texture_blob_view_t;
static_assert(sizeof(texture_blob_view_t) == 40);

typedef enum texture_blob_status_t : uint8_t {
    TEXTURE_BLOB_OK = 0,
    TEXTURE_BLOB_TRUNCATED = 1,     /* shorter than its header, or than its header says */
    TEXTURE_BLOB_BAD_ALIGNMENT = 2, /* not on a 64-byte boundary */
    TEXTURE_BLOB_BAD_MAGIC = 3,
    TEXTURE_BLOB_BAD_VERSION = 4,
    TEXTURE_BLOB_TRAILING = 5, /* bytes after what its header says */
    TEXTURE_BLOB_BAD_HASH = 6,
    TEXTURE_BLOB_BAD_LAYOUT = 7,  /* a section is not where the format puts it */
    TEXTURE_BLOB_BAD_NAME = 8,    /* out of bounds, not NUL-terminated, out of order, or not what its hash says */
    TEXTURE_BLOB_BAD_SIZE = 9,    /* a dimension, frame count, level count or palette count out of range */
    TEXTURE_BLOB_BAD_OFFSET = 10, /* a texture's pixels or vectors are not where they must be, or not that long */
    TEXTURE_BLOB_BAD_CLASS = 11,  /* an unknown class, or an emissive level over 15, or padding that is not zero */
} texture_blob_status_t;

/* FNV-1a 64: the same function as mctex_hash in compile/, written again because run/ does not include from compile/. */
uint64_t texture_blob_hash(const uint8_t *bytes, uint64_t length);

/* Checks every field and the hash, and fills view. On failure returns the status and empties view. Reads every byte once, so
 * call it at startup, not in a hot path. */
texture_blob_status_t texture_blob_parse(const uint8_t *bytes, uint64_t length, texture_blob_view_t *view);

/* One sentence about a status. Never nullptr, for any value. */
const char *texture_blob_status_text(texture_blob_status_t status);

#endif
