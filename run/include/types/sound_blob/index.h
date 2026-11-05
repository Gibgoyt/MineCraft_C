/*
 * The sound blob's format: what compile/src/sound_pack writes and run/src/audio reads. It is here because
 * plans/REPO_STRUCTURE.md section 11 lets compile/ include from run/ only under types/, and one definition cannot drift. See
 * run/include/types/sound_blob/README.md.
 *
 * In file order, little-endian, starting on a 64-byte boundary, with zeros in every gap:
 *   header    one sound_blob_header_t
 *   entries   sound_count * sound_blob_entry_t, strictly ascending by family, then kind, then variant
 *   samples   on a 64-byte boundary. Per sound, on a 32-byte boundary: mono int16_t at SOUND_BLOB_SAMPLE_RATE. A mixer pans them
 */
#ifndef MC_RUN_TYPES_SOUND_BLOB_INDEX_H
#define MC_RUN_TYPES_SOUND_BLOB_INDEX_H

#include <stddef.h>
#include <stdint.h>

#define SOUND_BLOB_MAGIC 0x4253434Du /* "MCSB" in file order */
#define SOUND_BLOB_VERSION 1u
#define SOUND_BLOB_ALIGNMENT 64u
#define SOUND_BLOB_SOUND_ALIGNMENT 32u
#define SOUND_BLOB_SAMPLE_RATE 48000u
#define SOUND_BLOB_MAX_VARIANTS 8u
/* Five seconds. These are crunches: a longer one is a mistake. */
#define SOUND_BLOB_MAX_SAMPLES (5u * SOUND_BLOB_SAMPLE_RATE)

/* What a block is made of, as far as the ear goes. config/blocks/ says it by name, and the block table holds the number. */
typedef enum sound_family_t : uint8_t {
    SOUND_FAMILY_NONE = 0, /* never dug, so never heard: air and fluids. It has no sounds in the blob */
    SOUND_FAMILY_STONE = 1,
    SOUND_FAMILY_WOOD = 2,
    SOUND_FAMILY_DIRT = 3,
    SOUND_FAMILY_GRAVEL = 4,
    SOUND_FAMILY_SAND = 5,
    SOUND_FAMILY_GRASS = 6,
    SOUND_FAMILY_LEAVES = 7,
    SOUND_FAMILY_SNOW = 8,
    SOUND_FAMILY_GLASS = 9,
    SOUND_FAMILY_METAL = 10,
} sound_family_t;
#define SOUND_FAMILY_COUNT 11u

/* When a sound plays. */
typedef enum sound_kind_t : uint8_t {
    SOUND_KIND_HIT = 0,   /* each few ticks while mining */
    SOUND_KIND_BREAK = 1, /* the block breaks: the fuller crunch */
    SOUND_KIND_PLACE = 2, /* the block is placed: a short thud */
} sound_kind_t;
#define SOUND_KIND_COUNT 3u

typedef struct sound_blob_header_t {
    uint32_t magic;          /* 0 */
    uint32_t version;        /* 4 */
    uint32_t sound_count;    /* 8 */
    uint32_t sample_rate;    /* 12 */
    uint64_t total_bytes;    /* 16: of the whole blob */
    uint64_t payload_hash;   /* 24: FNV-1a 64 of everything after this header */
    uint64_t entries_offset; /* 32: this and the next are from the start of the blob */
    uint64_t samples_offset; /* 40 */
    uint64_t samples_bytes;  /* 48 */
    uint64_t _pad0;          /* 56: zero */
} sound_blob_header_t;
static_assert(sizeof(sound_blob_header_t) == 64);
static_assert(alignof(sound_blob_header_t) == 8);
static_assert(offsetof(sound_blob_header_t, magic) == 0);
static_assert(offsetof(sound_blob_header_t, version) == 4);
static_assert(offsetof(sound_blob_header_t, sound_count) == 8);
static_assert(offsetof(sound_blob_header_t, sample_rate) == 12);
static_assert(offsetof(sound_blob_header_t, total_bytes) == 16);
static_assert(offsetof(sound_blob_header_t, payload_hash) == 24);
static_assert(offsetof(sound_blob_header_t, entries_offset) == 32);
static_assert(offsetof(sound_blob_header_t, samples_offset) == 40);
static_assert(offsetof(sound_blob_header_t, samples_bytes) == 48);
static_assert(offsetof(sound_blob_header_t, _pad0) == 56);

typedef struct sound_blob_entry_t {
    uint64_t samples_offset; /* 0: from the start of the samples section */
    uint32_t sample_count;   /* 8 */
    uint16_t peak;           /* 12: the largest magnitude of any sample */
    uint8_t family;          /* 14: a sound_family_t, never SOUND_FAMILY_NONE */
    uint8_t kind;            /* 15: a sound_kind_t */
    uint8_t variant;         /* 16: 1 to SOUND_BLOB_MAX_VARIANTS, with no gap in a family's kind */
    uint8_t _pad0[7];        /* 17: zero */
} sound_blob_entry_t;
static_assert(sizeof(sound_blob_entry_t) == 24);
static_assert(alignof(sound_blob_entry_t) == 8);
static_assert(offsetof(sound_blob_entry_t, samples_offset) == 0);
static_assert(offsetof(sound_blob_entry_t, sample_count) == 8);
static_assert(offsetof(sound_blob_entry_t, peak) == 12);
static_assert(offsetof(sound_blob_entry_t, family) == 14);
static_assert(offsetof(sound_blob_entry_t, kind) == 15);
static_assert(offsetof(sound_blob_entry_t, variant) == 16);
static_assert(offsetof(sound_blob_entry_t, _pad0) == 17);

/* A checked blob. Borrows its bytes, which for the game last as long as the process. */
typedef struct sound_blob_view_t {
    const sound_blob_entry_t *borrowed_entries; /* 0 */
    const uint8_t *borrowed_samples;            /* 8 */
    uint32_t sound_count;                       /* 16 */
    uint8_t _pad0[4];                           /* 20 */
} sound_blob_view_t;
static_assert(sizeof(sound_blob_view_t) == 24);

typedef enum sound_blob_status_t : uint8_t {
    SOUND_BLOB_OK = 0,
    SOUND_BLOB_BAD_ALIGNMENT = 1,
    SOUND_BLOB_TRUNCATED = 2,
    SOUND_BLOB_BAD_MAGIC = 3,
    SOUND_BLOB_BAD_VERSION = 4,
    SOUND_BLOB_BAD_LAYOUT = 5, /* a section is not where and as long as it must be, or a field is out of range */
    SOUND_BLOB_BAD_HASH = 6,
    SOUND_BLOB_BAD_ENTRY = 7, /* out of order, or its samples are not inside the section */
} sound_blob_status_t;

/* The word config/blocks/ and sounds/ use for a family, or nullptr past the last. */
static inline const char *sound_family_name(uint32_t family) {
    static const char *const names[SOUND_FAMILY_COUNT] = {"none",  "stone",  "wood", "dirt",  "gravel", "sand",
                                                          "grass", "leaves", "snow", "glass", "metal"};
    return family < SOUND_FAMILY_COUNT ? names[family] : nullptr;
}

/* The word a sound's directory starts with, or nullptr past the last. */
static inline const char *sound_kind_name(uint32_t kind) {
    static const char *const names[SOUND_KIND_COUNT] = {"hit", "break", "place"};
    return kind < SOUND_KIND_COUNT ? names[kind] : nullptr;
}

/* FNV-1a 64. Written out here because a build-time format must not depend on a library's choice. */
static inline uint64_t sound_blob_hash(const uint8_t *bytes, uint64_t length) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint64_t i = 0; i < length; i++) {
        hash = (hash ^ bytes[i]) * 0x100000001B3ull;
    }
    return hash;
}

static inline uint64_t sound_blob_align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

/* Checks every field, the hash, and every entry, and only then fills view. blob must sit on SOUND_BLOB_ALIGNMENT. */
static inline sound_blob_status_t sound_blob_check(const uint8_t *blob, uint64_t bytes, sound_blob_view_t *view) {
    *view = (sound_blob_view_t){0};
    if ((uintptr_t)blob % SOUND_BLOB_ALIGNMENT != 0u) {
        return SOUND_BLOB_BAD_ALIGNMENT;
    }
    if (bytes < sizeof(sound_blob_header_t)) {
        return SOUND_BLOB_TRUNCATED;
    }
    const sound_blob_header_t *header = (const sound_blob_header_t *)(const void *)blob;
    if (header->magic != SOUND_BLOB_MAGIC) {
        return SOUND_BLOB_BAD_MAGIC;
    }
    if (header->version != SOUND_BLOB_VERSION) {
        return SOUND_BLOB_BAD_VERSION;
    }
    if (header->total_bytes != bytes) {
        return SOUND_BLOB_TRUNCATED;
    }
    const uint64_t entries_end =
        sizeof(sound_blob_header_t) + (uint64_t)header->sound_count * sizeof(sound_blob_entry_t);
    if (header->sample_rate != SOUND_BLOB_SAMPLE_RATE || header->_pad0 != 0u ||
        header->entries_offset != sizeof(sound_blob_header_t) ||
        header->samples_offset != sound_blob_align_up(entries_end, SOUND_BLOB_ALIGNMENT) ||
        header->samples_offset > bytes || header->samples_bytes != bytes - header->samples_offset) {
        return SOUND_BLOB_BAD_LAYOUT;
    }
    if (sound_blob_hash(blob + sizeof(sound_blob_header_t), bytes - sizeof(sound_blob_header_t)) !=
        header->payload_hash) {
        return SOUND_BLOB_BAD_HASH;
    }
    const sound_blob_entry_t *entries = (const sound_blob_entry_t *)(const void *)(blob + header->entries_offset);
    uint64_t last_order = 0;
    for (uint32_t i = 0; i < header->sound_count; i++) {
        const sound_blob_entry_t *entry = &entries[i];
        const uint64_t order = ((uint64_t)entry->family << 16) | ((uint64_t)entry->kind << 8) | entry->variant;
        if (order <= last_order || entry->family == SOUND_FAMILY_NONE || entry->family >= SOUND_FAMILY_COUNT ||
            entry->kind >= SOUND_KIND_COUNT || entry->variant == 0u || entry->variant > SOUND_BLOB_MAX_VARIANTS ||
            entry->sample_count == 0u || entry->sample_count > SOUND_BLOB_MAX_SAMPLES ||
            entry->samples_offset % SOUND_BLOB_SOUND_ALIGNMENT != 0u || entry->samples_offset > header->samples_bytes ||
            (uint64_t)entry->sample_count * 2u > header->samples_bytes - entry->samples_offset) {
            return SOUND_BLOB_BAD_ENTRY;
        }
        last_order = order;
    }
    *view = (sound_blob_view_t){.borrowed_entries = entries,
                                .borrowed_samples = blob + header->samples_offset,
                                .sound_count = header->sound_count};
    return SOUND_BLOB_OK;
}

/* The run of entries for one family and kind: out_first and how many, which is 0 if it has none. They sit together. */
static inline uint32_t sound_blob_find(const sound_blob_view_t *view, uint32_t family, uint32_t kind,
                                       uint32_t *out_first) {
    uint32_t count = 0;
    *out_first = 0;
    for (uint32_t i = 0; i < view->sound_count; i++) {
        if (view->borrowed_entries[i].family == family && view->borrowed_entries[i].kind == kind) {
            if (count == 0u) {
                *out_first = i;
            }
            count++;
        }
    }
    return count;
}

static inline const int16_t *sound_blob_samples(const sound_blob_view_t *view, const sound_blob_entry_t *entry) {
    return (const int16_t *)(const void *)(view->borrowed_samples + entry->samples_offset);
}

#endif
