/* See README.md. Compiled with no coverage of its own: the callback below must not call itself. */
#include "fuzz/support/index.h"

#include "types/index.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ASan's own interface. Its header is a part of the compiler, and these two are all that is wanted of it. */
void __asan_poison_memory_region(const volatile void *address, uint64_t bytes);
void __asan_unpoison_memory_region(const volatile void *address, uint64_t bytes);
/* What -fsanitize-coverage=trace-pc puts at the start of every basic block of what it compiles. */
void __sanitizer_cov_trace_pc(void);

enum : uint32_t {
    FUZZ_EDGES = 1u << 16,
    FUZZ_CORPUS_MOST = 1u << 15,
    FUZZ_PATH_BYTES = 1024u,
    FUZZ_DIRECTORIES_MOST = 1024u,
    FUZZ_STACK_MOST = 8u,
    FUZZ_POLL_NS = 20000000u,
    /* What of a child's log a person is shown: the end, which is where a report is. */
    FUZZ_LOG_SHOWN = 1u << 16,
    /* What a child is given to get ready. */
    FUZZ_START_SECONDS = 240u,
    FUZZ_INPUT_OFFSET = 4096u,
    FUZZ_BUFFERS_MOST = 32u,
    FUZZ_COUNTS_MOST = 12u,
};
enum : uint64_t {
    FUZZ_CORPUS_BYTES_MOST = 512ull << 20,
    FUZZ_NS = 1000000000ull,
};

/* Set by the coverage callback on every edge taken: a tick count for each, by a hash of where it came from and went to. */
static uint8_t fuzz_edges_now[FUZZ_EDGES];
/* Which sizes of tick count have ever been seen on each edge. */
static uint8_t fuzz_edges_seen[FUZZ_EDGES];
static uintptr_t fuzz_edge_previous;
/* What has been poisoned, to give back at the end: the harness clears the test arena when a test ends. As numbers, for this is owned by that arena. */
static uintptr_t fuzz_poisoned[FUZZ_BUFFERS_MOST][2];
static uint32_t fuzz_poisoned_count;
static uint64_t fuzz_counts[FUZZ_COUNTS_MOST];

void __sanitizer_cov_trace_pc(void) {
    const uintptr_t here = (uintptr_t)__builtin_return_address(0);
    const uintptr_t mixed = (here ^ (here >> 16)) * 0x9E3779B1u;
    const uint32_t slot = (uint32_t)((mixed >> 8) ^ fuzz_edge_previous) & (FUZZ_EDGES - 1u);
    /* Tops out rather than wrapping to 0. */
    const uint8_t count = fuzz_edges_now[slot];
    fuzz_edges_now[slot] = (uint8_t)(count + (count != UINT8_MAX ? 1u : 0u));
    fuzz_edge_previous = (mixed >> 8) >> 1;
}

/* One bit for each size of tick count that means something different, as AFL has it. */
static uint8_t fuzz_edge_bucket(uint8_t count) {
    if (count <= 3u) {
        return (uint8_t)(count == 3u ? 4u : count);
    }
    if (count < 8u) {
        return 8u;
    }
    if (count < 16u) {
        return 16u;
    }
    if (count < 32u) {
        return 32u;
    }
    return count < 128u ? 64u : 128u;
}

/* Whether the input just run did anything new, which it marks as seen. */
static bool fuzz_edges_take(uint64_t *edge_count) {
    bool fresh = false;
    for (uint32_t word = 0u; word < FUZZ_EDGES; word += 8u) {
        uint64_t any = 0u;
        memcpy(&any, fuzz_edges_now + word, sizeof(any));
        if (any == 0u) {
            continue;
        }
        for (uint32_t i = word; i < word + 8u; i++) {
            const uint8_t bit = fuzz_edge_bucket(fuzz_edges_now[i]);
            if (bit != 0u && (fuzz_edges_seen[i] & bit) == 0u) {
                if (fuzz_edges_seen[i] == 0u) {
                    (*edge_count)++;
                }
                fuzz_edges_seen[i] |= bit;
                fresh = true;
            }
        }
    }
    return fresh;
}

void fuzz_count(uint32_t slot) {
    if (slot < FUZZ_COUNTS_MOST) {
        fuzz_counts[slot]++;
    }
}

uint64_t fuzz_rng_next(fuzz_rng_t *rng) {
    uint64_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng->state = x;
    return x;
}

uint64_t fuzz_rng_below(fuzz_rng_t *rng, uint64_t limit) {
    return limit == 0u ? 0u : fuzz_rng_next(rng) % limit;
}

uint32_t fuzz_get_u32(const uint8_t *from) {
    uint32_t value = 0u;
    memcpy(&value, from, sizeof(value));
    return value;
}

uint64_t fuzz_get_u64(const uint8_t *from) {
    uint64_t value = 0u;
    memcpy(&value, from, sizeof(value));
    return value;
}

void fuzz_put_u32(uint8_t *to, uint32_t value) {
    memcpy(to, &value, sizeof(value));
}

void fuzz_put_u64(uint8_t *to, uint64_t value) {
    memcpy(to, &value, sizeof(value));
}

void fuzz_fail(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    (void)fprintf(stderr, "fuzz: the code under test is wrong: ");
    (void)vfprintf(stderr, format, arguments);
    (void)fprintf(stderr, "\n");
    va_end(arguments);
    (void)fflush(stderr);
    _exit(77);
}

fuzz_buffer_t fuzz_buffer_make(test_context_t *t, uint64_t capacity) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, capacity + 256u);
    /* A clear 64 bytes of poison on each side, as an aligned pointer leaves. */
    const uintptr_t base = (raw + 127u) & ~(uintptr_t)63u;
    if (fuzz_poisoned_count >= FUZZ_BUFFERS_MOST) {
        fuzz_fail("more than %" PRIu32 " buffers", (uint32_t)FUZZ_BUFFERS_MOST);
    }
    fuzz_poisoned[fuzz_poisoned_count][0] = raw;
    fuzz_poisoned[fuzz_poisoned_count][1] = capacity + 256u;
    fuzz_poisoned_count++;
    __asan_poison_memory_region((void *)raw, capacity + 256u);
    return (fuzz_buffer_t){.borrowed_base = (uint8_t *)base, .capacity = capacity, .fitted = 0u};
}

uint8_t *fuzz_buffer_fit(fuzz_buffer_t *buffer, uint64_t bytes) {
    if (bytes > buffer->capacity) {
        fuzz_fail("a buffer of %" PRIu64 " bytes was asked for %" PRIu64, buffer->capacity, bytes);
    }
    /* Only as far as was opened last time, or an input near the size of the whole would cost its shadow every time. */
    __asan_poison_memory_region(buffer->borrowed_base, buffer->fitted);
    __asan_unpoison_memory_region(buffer->borrowed_base, bytes);
    buffer->fitted = bytes;
    return buffer->borrowed_base;
}

uint8_t *fuzz_buffer_hold(fuzz_buffer_t *buffer, const void *bytes, uint64_t length) {
    uint8_t *const held = fuzz_buffer_fit(buffer, length);
    if (length != 0u) {
        memcpy(held, bytes, length);
    }
    return held;
}

uint8_t *fuzz_file_read(test_context_t *t, const char *path, uint64_t capacity, uint64_t *out_length) {
    *out_length = 0u;
    const int32_t fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return nullptr;
    }
    struct stat about;
    if (fstat(fd, &about) != 0 || about.st_size < 0 || (uint64_t)about.st_size > capacity) {
        (void)close(fd);
        return nullptr;
    }
    const uint64_t length = (uint64_t)about.st_size;
    uint8_t *const bytes = test_alloc(t, length + 1u);
    uint64_t have = 0u;
    while (have < length) {
        const int64_t got = read(fd, bytes + have, length - have);
        if (got <= 0) {
            (void)close(fd);
            return nullptr;
        }
        have += (uint64_t)got;
    }
    (void)close(fd);
    *out_length = length;
    return bytes;
}

/* ---- The corpus ------------------------------------------------------------------------------------------------------ */

static void fuzz_corpus_keep(fuzz_corpus_t *corpus, const uint8_t *bytes, uint64_t length) {
    /* Past these the fuzz goes on with what it has. */
    if (corpus->count >= FUZZ_CORPUS_MOST || corpus->bytes_kept + length > FUZZ_CORPUS_BYTES_MOST) {
        return;
    }
    uint8_t *const copy = test_alloc(corpus->borrowed_test, length + 1u);
    if (length != 0u) {
        memcpy(copy, bytes, length);
    }
    corpus->borrowed_entries[corpus->count] = (fuzz_entry_t){.borrowed_bytes = copy, .length = length};
    const uint32_t kind = length != 0u ? bytes[0] % corpus->kind_count : 0u;
    corpus->borrowed_kinds[kind][corpus->kind_counts[kind]] = corpus->count;
    corpus->kind_counts[kind]++;
    corpus->count++;
    corpus->bytes_kept += length;
}

void fuzz_seed_add(fuzz_corpus_t *corpus, const void *bytes, uint64_t length) {
    if (length <= corpus->capacity_each) {
        fuzz_corpus_keep(corpus, bytes, length);
    }
}

static bool fuzz_name_ends(const char *name, const char *suffix) {
    const uint64_t name_length = strlen(name);
    const uint64_t suffix_length = strlen(suffix);
    return name_length >= suffix_length && strcmp(name + name_length - suffix_length, suffix) == 0;
}

uint32_t fuzz_seed_files(test_context_t *t, fuzz_corpus_t *corpus, const char *directory, const char *suffix,
                         const void *prefix, uint64_t prefix_length) {
    /* A list and not a walk that calls itself: recursion is banned. */
    char *const list = test_alloc(t, (uint64_t)FUZZ_DIRECTORIES_MOST * FUZZ_PATH_BYTES);
    uint8_t *const joined = test_alloc(t, corpus->capacity_each + prefix_length + 1u);
    uint32_t added = 0u;
    (void)snprintf(list, FUZZ_PATH_BYTES, "%s", directory);
    uint32_t pending = 1u;
    while (pending != 0u) {
        pending--;
        char here[FUZZ_PATH_BYTES];
        memcpy(here, list + (uint64_t)pending * FUZZ_PATH_BYTES, FUZZ_PATH_BYTES);
        DIR *const handle = opendir(here);
        if (handle == nullptr) {
            continue;
        }
        for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
            if (entry->d_name[0] == '.') {
                continue;
            }
            char path[FUZZ_PATH_BYTES + 512u];
            const int32_t written = snprintf(path, sizeof(path), "%s/%s", here, entry->d_name);
            struct stat about;
            if (written <= 0 || written >= (int32_t)FUZZ_PATH_BYTES || stat(path, &about) != 0) {
                continue;
            }
            if (S_ISDIR(about.st_mode)) {
                if (pending < FUZZ_DIRECTORIES_MOST) {
                    memcpy(list + (uint64_t)pending * FUZZ_PATH_BYTES, path, FUZZ_PATH_BYTES);
                    pending++;
                }
            } else if (fuzz_name_ends(entry->d_name, suffix)) {
                uint64_t length = 0u;
                const uint8_t *const bytes = fuzz_file_read(t, path, corpus->capacity_each, &length);
                if (bytes != nullptr && prefix_length + length <= corpus->capacity_each) {
                    if (prefix_length != 0u) {
                        memcpy(joined, prefix, prefix_length);
                    }
                    memcpy(joined + prefix_length, bytes, length);
                    fuzz_seed_add(corpus, joined, prefix_length + length);
                    added++;
                }
            }
        }
        (void)closedir(handle);
    }
    return added;
}

/* ---- Mutation ---------------------------------------------------------------------------------------------------------- */

static const int64_t fuzz_interesting[] = {
    0,
    1,
    -1,
    2,
    3,
    4,
    7,
    8,
    15,
    16,
    31,
    32,
    63,
    64,
    127,
    128,
    255,
    256,
    1024,
    4096,
    32767,
    32768,
    65535,
    65536,
    INT32_MAX,
    (int64_t)INT32_MAX + 1,
    (int64_t)UINT32_MAX,
    (int64_t)UINT32_MAX + 1,
    INT64_MAX,
    INT64_MIN,
};
enum { FUZZ_INTERESTING_COUNT = sizeof(fuzz_interesting) / sizeof(fuzz_interesting[0]) };

/* Room is made by moving what follows: at is at most *length. */
static bool fuzz_gap_make(uint8_t *input, uint64_t *length, uint64_t capacity, uint64_t at, uint64_t bytes) {
    if (bytes > capacity - *length) {
        return false;
    }
    memmove(input + at + bytes, input + at, *length - at);
    *length += bytes;
    return true;
}

static uint64_t fuzz_run_size(fuzz_rng_t *rng, uint64_t most) {
    /* Short ones mostly, since those keep a file's shape. */
    const uint64_t limit = fuzz_rng_below(rng, 8u) == 0u ? most : (most < 32u ? most : 32u);
    return 1u + fuzz_rng_below(rng, limit);
}

static void fuzz_mutate_one(const fuzz_target_t *target, const fuzz_corpus_t *corpus, uint8_t *input, uint64_t *length,
                            fuzz_rng_t *rng) {
    const uint64_t capacity = target->input_capacity;
    const uint64_t have = *length;
    const uint64_t width = 1ull << fuzz_rng_below(rng, 4u);
    switch (fuzz_rng_below(rng, 11u)) {
    case 0u: /* a bit */
        if (have != 0u) {
            input[fuzz_rng_below(rng, have)] ^= (uint8_t)(1u << fuzz_rng_below(rng, 8u));
        }
        break;
    case 1u: /* a byte */
        if (have != 0u) {
            input[fuzz_rng_below(rng, have)] = (uint8_t)fuzz_rng_next(rng);
        }
        break;
    case 2u: /* a number that often means trouble, of any width, anywhere */
        if (have >= width) {
            const int64_t value = fuzz_interesting[fuzz_rng_below(rng, FUZZ_INTERESTING_COUNT)];
            memcpy(input + fuzz_rng_below(rng, have - width + 1u), &value, width);
        }
        break;
    case 3u: /* near what is there */
        if (have >= width) {
            const uint64_t at = fuzz_rng_below(rng, have - width + 1u);
            uint64_t value = 0u;
            memcpy(&value, input + at, width);
            value += fuzz_rng_below(rng, 33u) - 16u;
            memcpy(input + at, &value, width);
        }
        break;
    case 4u: /* a run goes */
        if (have != 0u) {
            const uint64_t at = fuzz_rng_below(rng, have);
            const uint64_t bytes = fuzz_run_size(rng, have - at);
            memmove(input + at, input + at + bytes, have - at - bytes);
            *length = have - bytes;
        }
        break;
    case 5u: /* a run of what is here, again, elsewhere */
        if (have != 0u) {
            const uint64_t from = fuzz_rng_below(rng, have);
            const uint64_t to = fuzz_rng_below(rng, have + 1u);
            /* Copied first, since making the gap can move it. */
            uint8_t held[4096];
            const uint64_t bytes = fuzz_run_size(rng, have - from);
            const uint64_t taken = bytes < sizeof(held) ? bytes : sizeof(held);
            memcpy(held, input + from, taken);
            if (fuzz_gap_make(input, length, capacity, to, taken)) {
                memcpy(input + to, held, taken);
            }
        }
        break;
    case 6u: /* a run over another, with no change of size */
        if (have != 0u) {
            const uint64_t from = fuzz_rng_below(rng, have);
            const uint64_t bytes = fuzz_run_size(rng, have - from);
            memmove(input + fuzz_rng_below(rng, have - bytes + 1u), input + from, bytes);
        }
        break;
    case 7u: /* cut short */
        if (have != 0u) {
            *length = fuzz_rng_below(rng, have);
        }
        break;
    case 8u: { /* a run of one byte */
        const uint64_t at = fuzz_rng_below(rng, have + 1u);
        const uint64_t bytes = fuzz_run_size(rng, 256u);
        if (fuzz_gap_make(input, length, capacity, at, bytes)) {
            memset(input + at, (int32_t)(fuzz_rng_next(rng) & 0xFFu), bytes);
        }
        break;
    }
    case 9u: /* a word of the format */
        if (target->token_count != 0u) {
            const fuzz_token_t token = target->borrowed_tokens[fuzz_rng_below(rng, target->token_count)];
            const uint64_t at = fuzz_rng_below(rng, have + 1u);
            if (fuzz_rng_below(rng, 2u) == 0u) {
                if (fuzz_gap_make(input, length, capacity, at, token.length)) {
                    memcpy(input + at, token.borrowed_bytes, token.length);
                }
            } else if (token.length <= have) {
                memcpy(input + fuzz_rng_below(rng, have - token.length + 1u), token.borrowed_bytes, token.length);
            }
        }
        break;
    default: /* a run of another input */
        if (corpus->count != 0u) {
            const fuzz_entry_t other = corpus->borrowed_entries[fuzz_rng_below(rng, corpus->count)];
            if (other.length != 0u) {
                const uint64_t from = fuzz_rng_below(rng, other.length);
                const uint64_t bytes = fuzz_run_size(rng, other.length - from);
                const uint64_t to = fuzz_rng_below(rng, have + 1u);
                if (fuzz_gap_make(input, length, capacity, to, bytes)) {
                    memcpy(input + to, other.borrowed_bytes + from, bytes);
                }
            }
        }
        break;
    }
}

/* ---- The supervisor ---------------------------------------------------------------------------------------------------- */

/* At the start of the file a child and its supervisor share. This, then at FUZZ_INPUT_OFFSET the input being run. By pwrite and not through a mapping:
 * mmap is banned here, and a few bytes written cost next to nothing against the rest of an input's run. */
typedef struct fuzz_shared_t {
    uint64_t execs;                    /* 0: inputs finished */
    uint64_t length;                   /* 8: of the input now being run */
    uint64_t corpus;                   /* 16 */
    uint64_t edges;                    /* 24 */
    uint64_t slowest_ns;               /* 32 */
    uint64_t running;                  /* 40: 1 once its seeds are made */
    uint64_t counts[FUZZ_COUNTS_MOST]; /* 48 */
} fuzz_shared_t;
static_assert(sizeof(fuzz_shared_t) == 144);

typedef struct fuzz_arguments_t {
    const char *replay;    /* 0 */
    const char *crash_dir; /* 8 */
    uint64_t seed;         /* 16 */
    uint64_t seconds;      /* 24 */
    uint64_t max_execs;    /* 32: 0 for no limit */
    uint64_t hang_seconds; /* 40 */
    uint64_t max_crashes;  /* 48 */
} fuzz_arguments_t;
static_assert(sizeof(fuzz_arguments_t) == 56);

static uint64_t fuzz_now_ns(void) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * FUZZ_NS + (uint64_t)now.tv_nsec;
}

static uint64_t fuzz_hash(const uint8_t *bytes, uint64_t length) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint64_t i = 0u; i < length; i++) {
        hash = (hash ^ bytes[i]) * 0x100000001B3ull;
    }
    return hash;
}

static bool fuzz_arguments_read(test_context_t *t, fuzz_arguments_t *out) {
    *out = (fuzz_arguments_t){.replay = nullptr,
                              .crash_dir = "/tmp/mc_fuzz_crashes",
                              .seed = 1u,
                              .seconds = 30u,
                              .max_execs = 0u,
                              .hang_seconds = 60u,
                              .max_crashes = 1u};
    for (int32_t i = 0; i < t->arg_count; i += 2) {
        const char *const name = t->borrowed_args[i];
        const char *const value = i + 1 < t->arg_count ? t->borrowed_args[i + 1] : nullptr;
        if (value == nullptr) {
            (void)printf("fuzz: %s needs a value\n", name);
            return false;
        }
        const uint64_t number = strtoull(value, nullptr, 10);
        if (strcmp(name, "--replay") == 0) {
            out->replay = value;
        } else if (strcmp(name, "--crash-dir") == 0) {
            out->crash_dir = value;
        } else if (strcmp(name, "--seed") == 0) {
            out->seed = number == 0u ? 1u : number;
        } else if (strcmp(name, "--seconds") == 0) {
            out->seconds = number;
        } else if (strcmp(name, "--max-execs") == 0) {
            out->max_execs = number;
        } else if (strcmp(name, "--hang-seconds") == 0) {
            out->hang_seconds = number;
        } else if (strcmp(name, "--max-crashes") == 0) {
            out->max_crashes = number;
        } else {
            (void)printf("fuzz: no such argument as %s\n", name);
            return false;
        }
    }
    return true;
}

static void fuzz_shared_put(int32_t shared_fd, const fuzz_shared_t *shared) {
    if (pwrite(shared_fd, shared, sizeof(*shared), 0) != (int64_t)sizeof(*shared)) {
        _exit(79);
    }
}

/* Takes the next input: a seed as it is, or some other input changed. */
static uint64_t fuzz_input_next(const fuzz_target_t *target, const fuzz_corpus_t *corpus, uint64_t iteration,
                                uint64_t seeds, uint8_t *input, fuzz_rng_t *rng) {
    uint64_t length = 0u;
    if (iteration < seeds) {
        const fuzz_entry_t entry = corpus->borrowed_entries[iteration];
        memcpy(input, entry.borrowed_bytes, entry.length);
        return entry.length;
    }
    /* Now and then from nothing, which finds what no seed looks like. */
    uint8_t first = 0u;
    bool keep_first = false;
    if (corpus->count != 0u && fuzz_rng_below(rng, 64u) != 0u) {
        /* Each kind the same, and in it the newest half twice as often: what was found last is what has not been looked into yet. */
        uint32_t kind = (uint32_t)fuzz_rng_below(rng, corpus->kind_count);
        for (uint32_t tried = 0u; tried < corpus->kind_count && corpus->kind_counts[kind] == 0u; tried++) {
            kind = (kind + 1u) % corpus->kind_count;
        }
        const uint64_t count = corpus->kind_counts[kind];
        const uint64_t half = count / 2u;
        const uint64_t pick =
            fuzz_rng_below(rng, 3u) != 0u ? half + fuzz_rng_below(rng, count - half) : fuzz_rng_below(rng, count);
        const fuzz_entry_t entry = corpus->borrowed_entries[corpus->borrowed_kinds[kind][pick]];
        memcpy(input, entry.borrowed_bytes, entry.length);
        length = entry.length;
        if (length != 0u && corpus->kind_count > 1u && fuzz_rng_below(rng, 32u) != 0u) {
            first = input[0];
            keep_first = true;
        }
    }
    if (target->mutate == nullptr ||
        !target->mutate(target->borrowed_context, input, &length, target->input_capacity, rng)) {
        const uint64_t stack = 1u + fuzz_rng_below(rng, FUZZ_STACK_MOST);
        for (uint64_t m = 0u; m < stack; m++) {
            fuzz_mutate_one(target, corpus, input, &length, rng);
        }
    }
    if (keep_first && length != 0u) {
        input[0] = first;
    }
    if (target->fix != nullptr) {
        target->fix(target->borrowed_context, input, &length, target->input_capacity, rng);
    }
    return length;
}

/* All that a child does. Its stdout and stderr are log_fd, emptied before each input, so that when it dies the log is that input's. */
[[noreturn]] static void fuzz_child(test_context_t *t, const fuzz_target_t *target, const fuzz_arguments_t *arguments,
                                    uint64_t seed, uint64_t deadline_ns, int32_t shared_fd, int32_t log_fd) {
    if (dup2(log_fd, 1) < 0 || dup2(log_fd, 2) < 0) {
        _exit(78);
    }
    fuzz_corpus_t corpus = {.borrowed_test = t,
                            .borrowed_entries = test_alloc(t, (uint64_t)FUZZ_CORPUS_MOST * sizeof(fuzz_entry_t)),
                            .borrowed_kinds = {nullptr},
                            .kind_counts = {0u},
                            .capacity_each = target->input_capacity,
                            .bytes_kept = 0u,
                            .count = 0u,
                            .kind_count = target->kind_count != 0u ? target->kind_count : 1u};
    if (corpus.kind_count > FUZZ_KINDS_MOST) {
        _exit(78);
    }
    for (uint32_t kind = 0u; kind < corpus.kind_count; kind++) {
        corpus.borrowed_kinds[kind] = test_alloc(t, (uint64_t)FUZZ_CORPUS_MOST * sizeof(uint32_t));
    }
    if (target->seeds != nullptr) {
        target->seeds(t, target->borrowed_context, &corpus);
    }
    fuzz_rng_t rng = {.state = seed * 0x9E3779B97F4A7C15ull + 0x1234567ull};
    uint8_t *const input = test_alloc(t, target->input_capacity + 1u);
    fuzz_shared_t shared = {.running = 1u};
    const uint64_t seeds = corpus.count;
    for (uint64_t iteration = 0u;; iteration++) {
        const uint64_t length = fuzz_input_next(target, &corpus, iteration, seeds, input, &rng);
        shared.length = length;
        memcpy(shared.counts, fuzz_counts, sizeof(shared.counts));
        fuzz_shared_put(shared_fd, &shared);
        if (pwrite(shared_fd, input, length, FUZZ_INPUT_OFFSET) != (int64_t)length || ftruncate(log_fd, 0) != 0) {
            _exit(79);
        }
        memset(fuzz_edges_now, 0, sizeof(fuzz_edges_now));
        fuzz_edge_previous = 0u;
        const uint64_t started_ns = fuzz_now_ns();
        target->execute(target->borrowed_context, input, length);
        const uint64_t took_ns = fuzz_now_ns() - started_ns;
        if (took_ns > shared.slowest_ns) {
            shared.slowest_ns = took_ns;
        }
        shared.execs++;
        if (fuzz_edges_take(&shared.edges) && iteration >= seeds) {
            fuzz_corpus_keep(&corpus, input, length);
        }
        shared.corpus = corpus.count;
        if ((arguments->max_execs != 0u && shared.execs >= arguments->max_execs) || started_ns >= deadline_ns) {
            memcpy(shared.counts, fuzz_counts, sizeof(shared.counts));
            fuzz_shared_put(shared_fd, &shared);
            if (ftruncate(log_fd, 0) != 0) {
                _exit(79);
            }
            /* Through exit and not _exit, so that LeakSanitizer looks: a handle glibc was given and nobody closed is what it would find. */
            exit(0);
        }
    }
}

/* The end of what the child wrote, for a person. */
static void fuzz_log_show(int32_t log_fd) {
    struct stat about;
    if (fstat(log_fd, &about) != 0) {
        return;
    }
    const uint64_t size = (uint64_t)about.st_size;
    const uint64_t from = size > FUZZ_LOG_SHOWN ? size - FUZZ_LOG_SHOWN : 0u;
    static char shown[FUZZ_LOG_SHOWN];
    const int64_t got = pread(log_fd, shown, sizeof(shown), (off_t)from);
    if (got > 0) {
        (void)printf("---- what it said ----\n%.*s\n---- end ----\n", (int32_t)got, shown);
    }
}

/* Keeps the input the child was on when it went wrong. */
static void fuzz_crash_keep(test_context_t *t, const fuzz_target_t *target, const fuzz_arguments_t *arguments,
                            int32_t shared_fd, int32_t log_fd, const char *what) {
    fuzz_shared_t shared;
    if (pread(shared_fd, &shared, sizeof(shared), 0) != (int64_t)sizeof(shared)) {
        memset(&shared, 0, sizeof(shared));
    }
    const uint64_t length = shared.length <= target->input_capacity ? shared.length : 0u;
    uint8_t *const input = test_alloc(t, length + 1u);
    if (pread(shared_fd, input, length, FUZZ_INPUT_OFFSET) != (int64_t)length) {
        (void)printf("fuzz: could not read back the input\n");
    }
    (void)mkdir(arguments->crash_dir, 0755);
    char *const path = test_alloc(t, FUZZ_PATH_BYTES);
    (void)snprintf(path, FUZZ_PATH_BYTES, "%s/%s-%016" PRIx64 ".input", arguments->crash_dir, target->name,
                   fuzz_hash(input, length));
    const int32_t out = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    const bool kept = out >= 0 && write(out, input, length) == (int64_t)length;
    if (out >= 0) {
        (void)close(out);
    }
    (void)printf("fuzz: %s: %s, on input %" PRIu64 " of %" PRIu64 " bytes\n", target->name, what, shared.execs, length);
    fuzz_log_show(log_fd);
    if (kept) {
        (void)printf("fuzz: to run it again: timeout 120 %s --replay %s\n", program_invocation_name, path);
    } else {
        (void)printf("fuzz: and the input could not be kept at %s\n", path);
    }
}

/* One child, until it stops or is stopped. Fills shared with where it got. True if it had nothing to report. */
static bool fuzz_supervise(test_context_t *t, const fuzz_target_t *target, const fuzz_arguments_t *arguments,
                           uint64_t seed, uint64_t deadline_ns, int32_t shared_fd, int32_t log_fd,
                           fuzz_shared_t *shared) {
    (void)fflush(stdout);
    const pid_t child = fork();
    if (child < 0) {
        (void)printf("fuzz: no child: %s\n", strerror(errno));
        return false;
    }
    if (child == 0) {
        fuzz_child(t, target, arguments, seed, deadline_ns, shared_fd, log_fd);
    }
    uint64_t last_execs = 0u;
    uint64_t last_change_ns = fuzz_now_ns();
    bool started = false;
    const struct timespec nap = {.tv_sec = 0, .tv_nsec = FUZZ_POLL_NS};
    for (;;) {
        int32_t status = 0;
        const pid_t done = waitpid(child, &status, WNOHANG);
        if (pread(shared_fd, shared, sizeof(*shared), 0) != (int64_t)sizeof(*shared)) {
            memset(shared, 0, sizeof(*shared));
        }
        if (done == child) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                return true;
            }
            char what[128];
            if (WIFEXITED(status)) {
                (void)snprintf(what, sizeof(what), "the child exited with status %" PRId32,
                               (int32_t)WEXITSTATUS(status));
            } else {
                (void)snprintf(what, sizeof(what), "the child died of signal %" PRId32, (int32_t)WTERMSIG(status));
            }
            fuzz_crash_keep(t, target, arguments, shared_fd, log_fd, what);
            return false;
        }
        const uint64_t now_ns = fuzz_now_ns();
        if (shared->running != 0u && !started) {
            started = true;
            last_change_ns = now_ns;
        }
        if (shared->execs != last_execs) {
            last_execs = shared->execs;
            last_change_ns = now_ns;
        }
        const uint64_t allowed_ns = (started ? arguments->hang_seconds : (uint64_t)FUZZ_START_SECONDS) * FUZZ_NS;
        if (now_ns - last_change_ns > allowed_ns) {
            (void)kill(child, SIGKILL);
            (void)waitpid(child, &status, 0);
            fuzz_crash_keep(t, target, arguments, shared_fd, log_fd, "one input ran past the hang limit");
            return false;
        }
        (void)nanosleep(&nap, nullptr);
    }
}

static void fuzz_run_all(test_context_t *t, const fuzz_target_t *target) {
    fuzz_arguments_t arguments;
    REQUIRE(fuzz_arguments_read(t, &arguments));
    if (arguments.replay != nullptr) {
        uint64_t length = 0u;
        const uint8_t *const bytes = fuzz_file_read(t, arguments.replay, target->input_capacity, &length);
        REQUIRE(bytes != nullptr);
        target->execute(target->borrowed_context, bytes, length);
        (void)printf("fuzz: %s: %s ran through with nothing wrong\n", target->name, arguments.replay);
        return;
    }
    const int32_t shared_fd = memfd_create("mc_fuzz_shared", MFD_CLOEXEC);
    const int32_t log_fd = memfd_create("mc_fuzz_log", MFD_CLOEXEC);
    REQUIRE(shared_fd >= 0 && log_fd >= 0);
    REQUIRE(fcntl(log_fd, F_SETFL, O_APPEND) == 0);

    const uint64_t started_ns = fuzz_now_ns();
    const uint64_t deadline_ns = started_ns + arguments.seconds * FUZZ_NS;
    uint64_t execs = 0u;
    uint64_t crashes = 0u;
    uint64_t seed = arguments.seed;
    fuzz_shared_t last = {0};
    uint64_t totals[FUZZ_COUNTS_MOST] = {0};
    for (;;) {
        fuzz_shared_t shared = {0};
        REQUIRE(pwrite(shared_fd, &shared, sizeof(shared), 0) == (int64_t)sizeof(shared));
        const bool clean = fuzz_supervise(t, target, &arguments, seed, deadline_ns, shared_fd, log_fd, &shared);
        execs += shared.execs;
        last = shared;
        for (uint32_t i = 0u; i < FUZZ_COUNTS_MOST; i++) {
            totals[i] += shared.counts[i];
        }
        if (clean) {
            break;
        }
        crashes++;
        seed++;
        if (crashes >= arguments.max_crashes || fuzz_now_ns() >= deadline_ns) {
            break;
        }
    }
    const uint64_t took_ns = fuzz_now_ns() - started_ns;
    (void)printf("fuzz: %s: %" PRIu64 " inputs in %.1f s (%.0f/s), from seed %" PRIu64 "; the last child kept %" PRIu64
                 " inputs and reached %" PRIu64 " edges; its slowest input %.1f ms; %" PRIu64 " found wrong\n",
                 target->name, execs, (libc_double_t)((float64_t)took_ns / 1e9f64),
                 (libc_double_t)((float64_t)execs * 1e9f64 / (float64_t)took_ns), arguments.seed, last.corpus,
                 last.edges, (libc_double_t)((float64_t)last.slowest_ns / 1e6f64), crashes);
    for (uint32_t i = 0u; i < target->count_name_count; i++) {
        (void)printf("fuzz: %s: %s: %" PRIu64 "\n", target->name, target->borrowed_count_names[i], totals[i]);
        CHECK(totals[i] > 0u);
    }
    CHECK_EQ_U64(crashes, 0u);
    CHECK(execs > 0u);
    (void)close(shared_fd);
    (void)close(log_fd);
}

void fuzz_run(test_context_t *t, const fuzz_target_t *target) {
    fuzz_run_all(t, target);
    for (uint32_t i = 0u; i < fuzz_poisoned_count; i++) {
        __asan_unpoison_memory_region((void *)fuzz_poisoned[i][0], fuzz_poisoned[i][1]);
    }
    fuzz_poisoned_count = 0u;
}
