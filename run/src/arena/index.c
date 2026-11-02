/* See README.md. It never calls the heap: the Owner reserves and passes the memory in, so a search for heap_reserve still
 * finds everything that holds memory, and a depth 2 part handed an arena_t has no route to the OS allocator. */
#include "arena/index.h"

#include "assert/index.h"
#include "log/index.h"

#include <inttypes.h>
#include <string.h>

/* ASan knows nothing of a block inside a mapping, so without this a bump allocator hides from the sanitize preset every
 * bug it exists to find. Memory not currently handed out is poisoned: past the last allocation, in alignment gaps, and
 * all of it after a reset. */
#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/asan_interface.h>
#define ARENA_POISON(memory, bytes) __asan_poison_memory_region((memory), (bytes))
#define ARENA_UNPOISON(memory, bytes) __asan_unpoison_memory_region((memory), (bytes))
#else
#define ARENA_POISON(memory, bytes) ((void)(memory), (void)(bytes))
#define ARENA_UNPOISON(memory, bytes) ((void)(memory), (void)(bytes))
#endif

static const char *arena_name(const arena_t *arena) {
    return arena->borrowed_name != nullptr ? arena->borrowed_name : "(not initialized)";
}

void arena_init(arena_t *arena, const char *name, uint8_t *memory, uint64_t capacity) {
    ASSERT_ALWAYS(name != nullptr);
    ASSERT_ALWAYS(memory != nullptr);
    ASSERT_ALWAYS(capacity != 0);
    *arena = (arena_t){
        .borrowed_base = memory,
        .borrowed_name = name,
        .capacity = capacity,
        .used = 0,
        .peak = 0,
        .alloc_count = 0,
        .failed_count = 0,
        .reset_count = 0,
    };
    ARENA_POISON(memory, capacity);
}

void arena_deinit(arena_t *arena) {
    if (arena->borrowed_base != nullptr) {
        ARENA_UNPOISON(arena->borrowed_base, arena->capacity);
    }
    *arena = (arena_t){
        .borrowed_base = nullptr,
        .borrowed_name = nullptr,
        .capacity = 0,
        .used = 0,
        .peak = 0,
        .alloc_count = 0,
        .failed_count = 0,
        .reset_count = 0,
    };
}

bool arena_alloc_try(arena_t *arena, uint64_t bytes, uint64_t alignment, void **out) {
    ASSERT_ALWAYS(alignment != 0 && (alignment & (alignment - 1u)) == 0);
    /* The address decides the gap and not the offset, since a slice of a region can start anywhere. Each comparison
     * subtracts from what is left and so cannot wrap, however large bytes is. */
    const uintptr_t next = (uintptr_t)arena->borrowed_base + arena->used;
    const uint64_t gap = (uint64_t)(0u - next) & (alignment - 1u);
    const uint64_t left = arena->capacity - arena->used;
    if (gap > left || bytes > left - gap) {
        arena->failed_count++;
        *out = nullptr;
        return false;
    }
    uint8_t *const block = arena->borrowed_base + arena->used + gap;
    ARENA_UNPOISON(block, bytes);
    arena->used += gap + bytes;
    if (arena->used > arena->peak) {
        arena->peak = arena->used;
    }
    arena->alloc_count++;
    *out = block;
    return true;
}

void *arena_alloc(arena_t *arena, uint64_t bytes, uint64_t alignment) {
    void *block = nullptr;
    if (!arena_alloc_try(arena, bytes, alignment, &block)) {
        LOG_ERROR("arena '%s' is out of memory: asked for %" PRIu64 " bytes at alignment %" PRIu64 ", with %" PRIu64
                  " of %" PRIu64 " used",
                  arena_name(arena), bytes, alignment, arena->used, arena->capacity);
        assert_fail(__FILE__, __LINE__, __func__, "arena out of memory");
    }
    return block;
}

void *arena_alloc_zeroed(arena_t *arena, uint64_t bytes, uint64_t alignment) {
    void *const block = arena_alloc(arena, bytes, alignment);
    memset(block, 0, bytes);
    return block;
}

void *arena_alloc_array(arena_t *arena, uint64_t count, uint64_t element_bytes, uint64_t alignment) {
    if (element_bytes != 0 && count > UINT64_MAX / element_bytes) {
        LOG_ERROR("arena '%s': an array of %" PRIu64 " elements of %" PRIu64 " bytes does not fit in 64 bits",
                  arena_name(arena), count, element_bytes);
        assert_fail(__FILE__, __LINE__, __func__, "arena array size overflow");
    }
    return arena_alloc(arena, count * element_bytes, alignment);
}

void arena_reset(arena_t *arena) {
    ARENA_POISON(arena->borrowed_base, arena->used);
    arena->used = 0;
    arena->reset_count++;
}
