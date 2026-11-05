/*
 * The .mcsnd format: one recorded sound, as sounds/<family>/<name>/index.mcsnd. See compile/src/sound_pack/mcsnd/README.md.
 *
 * A 32-byte header, then sample_count mono int16_t at 48000 Hz, little-endian. Nothing else, and no bytes after the samples.
 */
#ifndef SOUND_PACK_MCSND_INDEX_H
#define SOUND_PACK_MCSND_INDEX_H

#include <stddef.h>
#include <stdint.h>

#define MCSND_MAGIC 0x4E53434Du /* "MCSN" in file order */
#define MCSND_VERSION 1u

typedef struct mcsnd_header_t {
    uint32_t magic;        /* 0 */
    uint32_t version;      /* 4 */
    uint32_t sample_rate;  /* 8: always 48000 */
    uint32_t sample_count; /* 12 */
    uint64_t sample_hash;  /* 16: FNV-1a 64 of the sample bytes */
    uint64_t _pad0;        /* 24: zero */
} mcsnd_header_t;
static_assert(sizeof(mcsnd_header_t) == 32);
static_assert(alignof(mcsnd_header_t) == 8);
static_assert(offsetof(mcsnd_header_t, magic) == 0);
static_assert(offsetof(mcsnd_header_t, version) == 4);
static_assert(offsetof(mcsnd_header_t, sample_rate) == 8);
static_assert(offsetof(mcsnd_header_t, sample_count) == 12);
static_assert(offsetof(mcsnd_header_t, sample_hash) == 16);
static_assert(offsetof(mcsnd_header_t, _pad0) == 24);

/* A parsed file. Borrows the file's bytes, and must not outlive them. The samples have no alignment promise. */
typedef struct mcsnd_view_t {
    const uint8_t *borrowed_samples; /* 0 */
    uint32_t sample_count;           /* 8 */
    uint8_t _pad0[4];                /* 12 */
} mcsnd_view_t;
static_assert(sizeof(mcsnd_view_t) == 16);

typedef enum mcsnd_status_t : uint8_t {
    MCSND_OK = 0,
    MCSND_TRUNCATED = 1, /* shorter than a header, or than its samples */
    MCSND_BAD_MAGIC = 2,
    MCSND_BAD_VERSION = 3,
    MCSND_BAD_RATE = 4,
    MCSND_BAD_SIZE = 5, /* no samples, an odd byte, or over SOUND_BLOB_MAX_SAMPLES */
    MCSND_BAD_PADDING = 6,
    MCSND_TRAILING = 7, /* bytes after the samples */
    MCSND_BAD_HASH = 8, /* the samples are not what was written */
    MCSND_NO_ROOM = 9,  /* mcsnd_encode's buffer is too small */
} mcsnd_status_t;

/* Writes a whole file to out, which needs sizeof(mcsnd_header_t) + sample_bytes. Takes no alignment. */
mcsnd_status_t mcsnd_encode(uint8_t *out, uint64_t capacity, const uint8_t *samples, uint64_t sample_bytes);

/* Checks every field and the hash. On failure returns the status and empties view. */
mcsnd_status_t mcsnd_parse(const uint8_t *file, uint64_t length, mcsnd_view_t *view);

/* One sentence about a status. Never nullptr, for any value. */
const char *mcsnd_status_text(mcsnd_status_t status);

#endif
