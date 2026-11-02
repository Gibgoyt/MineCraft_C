/* See README.md. The one file that calls the OS allocator. The counters are a static for the same reason log's settings
 * are: they own nothing, so the One-Owner rule is not in play, and every call in the process must reach the same ones. */
#include "heap/index.h"

#include "assert/index.h"
#include "log/index.h"

#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum : uint64_t { HEAP_PAGE_BYTES = 4096u }; /* x86-64, the only target: plans/CODING_STANDARD.md section 7 */

static_assert((HEAP_REGION_ALIGNMENT & (HEAP_REGION_ALIGNMENT - 1u)) == 0);
static_assert((uint64_t)HEAP_REGION_ALIGNMENT > (uint64_t)HEAP_PAGE_BYTES);
static_assert(HEAP_BUDGET_BYTES >= HEAP_REGION_ALIGNMENT);

/* All relaxed: each is an independent tally that guards no other data, and heap_release's check on live_bytes needs only
 * the atomicity of the read-modify-write. Reservations are rare, so sharing one cache line costs nothing, and the
 * alignment keeps that line from being shared with anything hotter. */
typedef struct heap_state_t {
    alignas(64) _Atomic uint64_t live_bytes;
    _Atomic uint64_t live_regions;
    _Atomic uint64_t peak_bytes;
    _Atomic uint64_t reserve_calls;
    _Atomic uint64_t release_calls;
    _Atomic uint64_t failed_reserves;
    _Atomic uint64_t huge_advice_failures;
    uint8_t _pad0[8];
} heap_state_t;
static_assert(sizeof(heap_state_t) == 64);

static heap_state_t heap_state;

static void heap_count(_Atomic uint64_t *counter) {
    atomic_fetch_add_explicit(counter, 1u, memory_order_relaxed);
}

/* Claims before mapping, so that two threads cannot both pass the check and together go over. */
static bool heap_budget_claim(uint64_t bytes, uint64_t *out_live) {
    uint64_t live = atomic_load_explicit(&heap_state.live_bytes, memory_order_relaxed);
    do {
        if (bytes > HEAP_BUDGET_BYTES - live) {
            return false;
        }
    } while (!atomic_compare_exchange_weak_explicit(&heap_state.live_bytes, &live, live + bytes, memory_order_relaxed,
                                                    memory_order_relaxed));
    *out_live = live + bytes;
    return true;
}

static void heap_peak_raise(uint64_t live) {
    uint64_t peak = atomic_load_explicit(&heap_state.peak_bytes, memory_order_relaxed);
    while (peak < live && !atomic_compare_exchange_weak_explicit(&heap_state.peak_bytes, &peak, live,
                                                                 memory_order_relaxed, memory_order_relaxed)) {
    }
}

/* mmap promises no alignment beyond a page, so this maps enough to hold an aligned block wherever the mapping lands, and
 * unmaps what sticks out at each end. Unmapping part of a mapping splits it, and a split can fail once the process is at
 * vm.max_map_count.
 *
 * Recent kernels do align a mapping whose length is a multiple of 2 MiB. This length never is, so the head trim really
 * runs: relying on that kernel behaviour would leave a branch here that only an older kernel ever executes. */
static bool heap_map_aligned(uint64_t bytes, uint8_t **out_base) {
    const uint64_t span = bytes + HEAP_REGION_ALIGNMENT - HEAP_PAGE_BYTES;
    void *const raw = mmap(nullptr, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == MAP_FAILED) {
        LOG_ERROR("heap: mmap of %" PRIu64 " bytes failed: %s", span, strerror(errno));
        return false;
    }
    const uintptr_t start = (uintptr_t)raw;
    const uintptr_t aligned = (start + HEAP_REGION_ALIGNMENT - 1u) & ~(uintptr_t)(HEAP_REGION_ALIGNMENT - 1u);
    const uint64_t head = aligned - start;
    const uint64_t tail = span - head - bytes;
    uint8_t *const base = (uint8_t *)aligned;

    if (head != 0 && munmap(raw, head) != 0) {
        LOG_ERROR("heap: trimming a mapping failed: %s", strerror(errno));
        if (munmap(raw, span) != 0) {
            LOG_ERROR("heap: %" PRIu64 " bytes stay mapped at %p: %s", span, raw, strerror(errno));
        }
        return false;
    }
    if (tail != 0 && munmap(base + bytes, tail) != 0) {
        LOG_ERROR("heap: trimming a mapping failed: %s", strerror(errno));
        if (munmap(base, bytes + tail) != 0) {
            LOG_ERROR("heap: %" PRIu64 " bytes stay mapped at %p: %s", bytes + tail, (void *)base, strerror(errno));
        }
        return false;
    }
    *out_base = base;
    return true;
}

bool heap_reserve(uint64_t bytes, heap_region_t *out) {
    *out = (heap_region_t){.owned_base = nullptr, .bytes = 0};
    heap_count(&heap_state.reserve_calls);

    /* Testing against the budget first is also what makes the rounding below unable to overflow. */
    if (bytes == 0 || bytes > HEAP_BUDGET_BYTES) {
        LOG_ERROR("heap: cannot reserve %" PRIu64 " bytes: the size must be from 1 to the budget of %" PRIu64, bytes,
                  (uint64_t)HEAP_BUDGET_BYTES);
        heap_count(&heap_state.failed_reserves);
        return false;
    }
    const uint64_t rounded = (bytes + HEAP_REGION_ALIGNMENT - 1u) & ~(HEAP_REGION_ALIGNMENT - 1u);

    uint64_t live = 0;
    if (!heap_budget_claim(rounded, &live)) {
        LOG_ERROR("heap: reserving %" PRIu64 " bytes would go over the budget of %" PRIu64
                  " (memory.process_budget_mib)",
                  rounded, (uint64_t)HEAP_BUDGET_BYTES);
        heap_count(&heap_state.failed_reserves);
        return false;
    }

    uint8_t *base = nullptr;
    if (!heap_map_aligned(rounded, &base)) {
        atomic_fetch_sub_explicit(&heap_state.live_bytes, rounded, memory_order_relaxed);
        heap_count(&heap_state.failed_reserves);
        return false;
    }

    /* A kernel built without transparent huge pages says EINVAL. The memory is still good, only slower to walk. */
    if (madvise(base, rounded, MADV_HUGEPAGE) != 0) {
        LOG_WARN("heap: MADV_HUGEPAGE on %" PRIu64 " bytes failed: %s", rounded, strerror(errno));
        heap_count(&heap_state.huge_advice_failures);
    }

    heap_count(&heap_state.live_regions);
    heap_peak_raise(live);
    *out = (heap_region_t){.owned_base = base, .bytes = rounded};
    return true;
}

/* munmap reports success on memory that is not mapped, so a second release through a stale copy of a handle would
 * otherwise pass here and throw the counters off. mincore says ENOMEM for such a page. */
static bool heap_page_is_mapped(uint8_t *page) {
    uint8_t residency = 0;
    return mincore(page, 1u, &residency) == 0;
}

void heap_release(heap_region_t *region) {
    uint8_t *const base = region->owned_base;
    const uint64_t bytes = region->bytes;
    if (base == nullptr) {
        ASSERT_ALWAYS(bytes == 0);
        return;
    }
    ASSERT_ALWAYS(((uintptr_t)base & (HEAP_REGION_ALIGNMENT - 1u)) == 0);
    ASSERT_ALWAYS(bytes != 0 && (bytes & (HEAP_REGION_ALIGNMENT - 1u)) == 0);
    ASSERT_ALWAYS(heap_page_is_mapped(base));
    ASSERT_ALWAYS(heap_page_is_mapped(base + bytes - HEAP_REGION_ALIGNMENT));
    ASSERT_ALWAYS(munmap(base, bytes) == 0);

    const uint64_t live_before = atomic_fetch_sub_explicit(&heap_state.live_bytes, bytes, memory_order_relaxed);
    ASSERT_ALWAYS(live_before >= bytes);
    atomic_fetch_sub_explicit(&heap_state.live_regions, 1u, memory_order_relaxed);
    heap_count(&heap_state.release_calls);
    *region = (heap_region_t){.owned_base = nullptr, .bytes = 0};
}

void heap_counters_read(heap_counters_t *out) {
    *out = (heap_counters_t){
        .live_bytes = atomic_load_explicit(&heap_state.live_bytes, memory_order_relaxed),
        .live_regions = atomic_load_explicit(&heap_state.live_regions, memory_order_relaxed),
        .peak_bytes = atomic_load_explicit(&heap_state.peak_bytes, memory_order_relaxed),
        .reserve_calls = atomic_load_explicit(&heap_state.reserve_calls, memory_order_relaxed),
        .release_calls = atomic_load_explicit(&heap_state.release_calls, memory_order_relaxed),
        .failed_reserves = atomic_load_explicit(&heap_state.failed_reserves, memory_order_relaxed),
        .huge_advice_failures = atomic_load_explicit(&heap_state.huge_advice_failures, memory_order_relaxed),
    };
}

bool heap_exit_check(void) {
    heap_counters_t counters;
    heap_counters_read(&counters);
    if (counters.live_regions == 0 && counters.live_bytes == 0) {
        return true;
    }
    LOG_ERROR("heap: leak at exit: %" PRIu64 " regions and %" PRIu64 " bytes are still live", counters.live_regions,
              counters.live_bytes);
    return false;
}
