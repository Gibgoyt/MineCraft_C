/*
 * textures/import.yaml: which pack, at which commit, under which licence, and what becomes of each of its files. See
 * compile/src/texture_pack/manifest/README.md. Everything here is checked at load, with a line number on any complaint.
 */
#ifndef TEXTURE_PACK_MANIFEST_INDEX_H
#define TEXTURE_PACK_MANIFEST_INDEX_H

#include "texture_pack/support/index.h"

#include <stdint.h>

typedef struct tp_entry_t {
    const char *borrowed_from;  /* 0: file in the pack */
    const char *borrowed_over;  /* 8: another file in the pack to composite this on top of, or nullptr */
    const char *borrowed_group; /* 16: blocks, items, mobs or ui */
    const char *borrowed_name;  /* 24: [a-z0-9_]+ */
    const char *borrowed_to;    /* 32: group/name */
    uint32_t frames;            /* 40: 1 unless the image is a vertical strip of animation frames */
    uint32_t line;              /* 44: in the manifest */
} tp_entry_t;
static_assert(sizeof(tp_entry_t) == 48);

/* A file in the pack that must contain this text, or the licence is no longer what we reviewed. */
typedef struct tp_evidence_t {
    const char *borrowed_file;     /* 0 */
    const char *borrowed_contains; /* 8 */
} tp_evidence_t;
static_assert(sizeof(tp_evidence_t) == 16);

/* A file deliberately not imported, and why. */
typedef struct tp_skip_t {
    const char *borrowed_file; /* 0 */
    const char *borrowed_why;  /* 8 */
} tp_skip_t;
static_assert(sizeof(tp_skip_t) == 16);

typedef struct tp_manifest_t {
    const char *borrowed_path;         /* 0 */
    const char *borrowed_id;           /* 8: pixel_perfection */
    const char *borrowed_title;        /* 16 */
    const char *borrowed_url;          /* 24 */
    const char *borrowed_commit;       /* 32: 40 lowercase hex digits */
    const char *borrowed_licence;      /* 40: an SPDX id from our own allowlist */
    const char *borrowed_licence_name; /* 48: from the allowlist */
    const char *borrowed_licence_url;  /* 56: from the allowlist */
    const char **borrowed_authors;     /* 64 */
    tp_evidence_t *borrowed_evidence;  /* 72 */
    tp_entry_t *borrowed_entries;      /* 80 */
    tp_skip_t *borrowed_skips;         /* 88 */
    uint32_t author_count;             /* 96 */
    uint32_t evidence_count;           /* 100 */
    uint32_t entry_count;              /* 104 */
    uint32_t skip_count;               /* 108 */
} tp_manifest_t;
static_assert(sizeof(tp_manifest_t) == 112);

/* Reads and checks the file, with everything taken from arena. On failure prints the reason it stopped. */
bool tp_manifest_load(tp_arena_t *arena, const char *path, tp_manifest_t *out);

#endif
