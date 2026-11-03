/* A coverage-guided mutation fuzzer of our own, for the programs under tests/fuzz/. See README.md. */
#ifndef MC_TESTS_FUZZ_SUPPORT_INDEX_H
#define MC_TESTS_FUZZ_SUPPORT_INDEX_H

#include "harness/index.h"

#include <stdint.h>

#ifndef __SANITIZE_ADDRESS__
#error "tests/fuzz/ is built only by the sanitize preset: without ASan most of what it looks for cannot be seen"
#endif

/* A word worth trying in an input, such as a keyword of the format. */
typedef struct fuzz_token_t {
    const uint8_t *borrowed_bytes; /* 0 */
    uint64_t length;               /* 8 */
} fuzz_token_t;
static_assert(sizeof(fuzz_token_t) == 16);

/* A little xorshift, so that a seed says exactly what was tried. */
typedef struct fuzz_rng_t {
    uint64_t state; /* 0 */
} fuzz_rng_t;
static_assert(sizeof(fuzz_rng_t) == 8);

uint64_t fuzz_rng_next(fuzz_rng_t *rng);
/* 0 for a limit of 0. */
uint64_t fuzz_rng_below(fuzz_rng_t *rng, uint64_t limit);

enum : uint32_t { FUZZ_KINDS_MOST = 8u };

/* One input, kept. */
typedef struct fuzz_entry_t {
    uint8_t *borrowed_bytes; /* 0 */
    uint64_t length;         /* 8 */
} fuzz_entry_t;
static_assert(sizeof(fuzz_entry_t) == 16);

/* What has been tried and did something new. Only fuzz_seed_add and fuzz_seed_files are for a target. */
typedef struct fuzz_corpus_t {
    test_context_t *borrowed_test;             /* 0 */
    fuzz_entry_t *borrowed_entries;            /* 8 */
    uint64_t capacity_each;                    /* 16 */
    uint32_t *borrowed_kinds[FUZZ_KINDS_MOST]; /* 24: of each kind, which entries */
    uint32_t kind_counts[FUZZ_KINDS_MOST];     /* 88 */
    uint64_t bytes_kept;                       /* 120 */
    uint32_t count;                            /* 128 */
    uint32_t kind_count;                       /* 132: at least 1 */
} fuzz_corpus_t;
static_assert(sizeof(fuzz_corpus_t) == 136);

/* One program's way of being fuzzed. seeds and fix may be nullptr. */
typedef struct fuzz_target_t {
    const char *name;                    /* 0: names the crash files */
    void *borrowed_context;              /* 8: given to each function below */
    const fuzz_token_t *borrowed_tokens; /* 16 */
    uint64_t input_capacity;             /* 24: the longest input tried */
    /* Adds the first inputs with fuzz_seed_add. Runs once, in the child, before any input. */
    void (*seeds)(test_context_t *t, void *context, fuzz_corpus_t *corpus); /* 32 */
    /* Makes a mutated input believable: checksums and lengths, which would otherwise stop nearly every one at the door. Its result is what runs, and what a
     * crash file holds. Should now and then leave things wrong, or nothing would try the checks themselves. */
    void (*fix)(void *context, uint8_t *input, uint64_t *length, uint64_t capacity, fuzz_rng_t *rng); /* 40 */
    /* One input. Anything wrong with what the code under test does is a crash: an ASan or UBSan report, or fuzz_fail. */
    void (*execute)(void *context, const uint8_t *input, uint64_t length); /* 48 */
    /* What fuzz_count counts, by slot: how far inputs got. Each must reach at least one, or a run says it failed: a fuzz whose inputs all stop at the door has
     * tested the door. */
    const char *const *borrowed_count_names; /* 56 */
    uint32_t token_count;                    /* 64 */
    uint32_t count_name_count;               /* 68 */
    /* How many kinds of input there are, the first byte modulo this saying which, for a target with several. Each kind gets the same share of the inputs tried,
     * and keeps its first byte through nearly every mutation. Otherwise whatever is quickest to find new edges in crowds out the rest. 0 or 1 for one kind. */
    uint32_t kind_count; /* 72 */
    uint8_t _pad0[4];    /* 76 */
    /* May change an input in a way that knows its format, in place of the general mutations, and then returns true. For a format where nearly every change of
     * length or stray byte is refused on sight, so that those mutations would never get inside. May be nullptr. */
    bool (*mutate)(void *context, uint8_t *input, uint64_t *length, uint64_t capacity, fuzz_rng_t *rng); /* 80 */
} fuzz_target_t;
static_assert(sizeof(fuzz_target_t) == 88);

/* Runs the program's arguments against the target, in the calling test. See README.md for them. */
void fuzz_run(test_context_t *t, const fuzz_target_t *target);

/* Adds a first input. Ignored if longer than the target takes. */
void fuzz_seed_add(fuzz_corpus_t *corpus, const void *bytes, uint64_t length);

/* A whole file into the test arena. nullptr if it cannot be read, or is over capacity. */
uint8_t *fuzz_file_read(test_context_t *t, const char *path, uint64_t capacity, uint64_t *out_length);

/* Every file under a directory whose name ends in suffix, as a seed, each with prefix before it. Returns how many. */
uint32_t fuzz_seed_files(test_context_t *t, fuzz_corpus_t *corpus, const char *directory, const char *suffix,
                         const void *prefix, uint64_t prefix_length);

/* One more of what that slot counts. */
void fuzz_count(uint32_t slot);

/* Says what is wrong and ends the process, so that the supervisor keeps the input. For an oracle's failure, as opposed to what a sanitizer sees. */
[[noreturn, gnu::format(printf, 1, 2)]] void fuzz_fail(const char *format, ...);

/* A buffer ASan watches to the byte. The test arena is one big mapping, and a read just off the end of a slice of it would otherwise be nothing at all. */
typedef struct fuzz_buffer_t {
    uint8_t *borrowed_base; /* 0: 64 byte aligned, with poison before and after */
    uint64_t capacity;      /* 8 */
    uint64_t fitted;        /* 16: what the last fit made usable */
} fuzz_buffer_t;
static_assert(sizeof(fuzz_buffer_t) == 24);

fuzz_buffer_t fuzz_buffer_make(test_context_t *t, uint64_t capacity);
/* Exactly bytes bytes usable from the base, and nothing else. Contents are as they were. Past capacity is a fuzz_fail. */
uint8_t *fuzz_buffer_fit(fuzz_buffer_t *buffer, uint64_t bytes);
/* The same with bytes copied in. */
uint8_t *fuzz_buffer_hold(fuzz_buffer_t *buffer, const void *bytes, uint64_t length);

/* Little-endian reads and writes of an input, which has no alignment. */
uint32_t fuzz_get_u32(const uint8_t *from);
uint64_t fuzz_get_u64(const uint8_t *from);
void fuzz_put_u32(uint8_t *to, uint32_t value);
void fuzz_put_u64(uint8_t *to, uint64_t value);

#endif
