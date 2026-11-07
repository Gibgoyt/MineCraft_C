/* See README.md. The two toroidal tables' slots: who may read, who may build, and who may take a slot for another tile.
 * The reader count and the state are both seq_cst, so a reader and an evictor cannot both miss each other's write: the
 * same argument as Dekker's for two threads (phase 03 file, "Atomics and the reader count"). */
#include "climate/cache/index.h"

#include <string.h>

uint64_t climate_cache_key(int32_t tile_x, int32_t tile_y) {
    return (uint64_t)(uint32_t)tile_x | (uint64_t)(uint32_t)tile_y << CLIMATE_CACHE_KEY_Y_SHIFT;
}

uint32_t climate_cache_slot_index(int32_t tile_x, int32_t tile_y, uint32_t side) {
    /* Two's complement: & (side - 1) is the modulo for negative numbers too. */
    const uint32_t mask = side - 1u;
    return ((uint32_t)tile_y & mask) * side + ((uint32_t)tile_x & mask);
}

bool climate_cache_keeps(int32_t tile_x, int32_t tile_y, int32_t keep_x, int32_t keep_y, uint32_t side) {
    const int64_t half = (int64_t)(side / 2u);
    const int64_t dx = (int64_t)tile_x - (int64_t)keep_x;
    const int64_t dy = (int64_t)tile_y - (int64_t)keep_y;
    return dx > -half && dx < half && dy > -half && dy < half;
}

void climate_cache_empty(climate_slot_t *slots, uint32_t count) {
    for (uint32_t i = 0u; i < count; i++) {
        atomic_store_explicit(&slots[i].key, 0u, memory_order_relaxed);
        atomic_store_explicit(&slots[i].state, CLIMATE_SLOT_EMPTY, memory_order_relaxed);
        atomic_store_explicit(&slots[i].readers, 0u, memory_order_relaxed);
        slots[i].borrowed_job_owner = nullptr;
        slots[i].job_tile_x = 0;
        slots[i].job_tile_y = 0;
        memset(slots[i]._pad0, 0, sizeof(slots[i]._pad0));
    }
}

bool climate_cache_read_begin(climate_slot_t *slot, uint64_t key) {
    atomic_fetch_add_explicit(&slot->readers, 1u, memory_order_seq_cst);
    /* seq_cst, so also an acquire: the key and the data written before the publisher's release are visible here. */
    const uint32_t state = atomic_load_explicit(&slot->state, memory_order_seq_cst);
    if (state == CLIMATE_SLOT_READY && atomic_load_explicit(&slot->key, memory_order_relaxed) == key) {
        return true;
    }
    atomic_fetch_sub_explicit(&slot->readers, 1u, memory_order_release);
    return false;
}

void climate_cache_read_end(climate_slot_t *slot) {
    atomic_fetch_sub_explicit(&slot->readers, 1u, memory_order_release);
}

bool climate_cache_ready(const climate_slot_t *slot, uint64_t key) {
    return atomic_load_explicit(&slot->state, memory_order_acquire) == CLIMATE_SLOT_READY &&
           atomic_load_explicit(&slot->key, memory_order_relaxed) == key;
}

climate_claim_t climate_cache_claim(climate_slot_t *slot, uint64_t key, bool keeps) {
    for (;;) {
        uint32_t state = atomic_load_explicit(&slot->state, memory_order_acquire);
        switch ((climate_slot_state_t)state) {
        case CLIMATE_SLOT_READY:
            if (atomic_load_explicit(&slot->key, memory_order_relaxed) == key) {
                return CLIMATE_CLAIM_HAVE;
            }
            if (!keeps) {
                return CLIMATE_CLAIM_REFUSED;
            }
            /* What is there is outside the keep square, as key is inside it and shares the slot. */
            if (!atomic_compare_exchange_strong_explicit(&slot->state, &state, CLIMATE_SLOT_EVICTING,
                                                         memory_order_seq_cst, memory_order_seq_cst)) {
                continue; /* it changed under us: look again */
            }
            if (atomic_load_explicit(&slot->readers, memory_order_seq_cst) != 0u) {
                /* A reader came first, and saw READY: it keeps the old tile, and this is tried again later. */
                atomic_store_explicit(&slot->state, CLIMATE_SLOT_READY, memory_order_release);
                return CLIMATE_CLAIM_REFUSED;
            }
            atomic_store_explicit(&slot->key, key, memory_order_relaxed);
            atomic_store_explicit(&slot->state, CLIMATE_SLOT_CLAIMED, memory_order_seq_cst);
            return CLIMATE_CLAIM_OURS;
        case CLIMATE_SLOT_EMPTY:
            if (!keeps) {
                return CLIMATE_CLAIM_REFUSED;
            }
            if (!atomic_compare_exchange_strong_explicit(&slot->state, &state, CLIMATE_SLOT_CLAIMED,
                                                         memory_order_seq_cst, memory_order_seq_cst)) {
                continue;
            }
            atomic_store_explicit(&slot->key, key, memory_order_relaxed);
            return CLIMATE_CLAIM_OURS;
        case CLIMATE_SLOT_CLAIMED:
        case CLIMATE_SLOT_EVICTING:
            return CLIMATE_CLAIM_BUSY;
        }
        return CLIMATE_CLAIM_BUSY; /* no other state is ever stored */
    }
}

void climate_cache_publish(climate_slot_t *slot) {
    atomic_store_explicit(&slot->state, CLIMATE_SLOT_READY, memory_order_release);
}

void climate_cache_unclaim(climate_slot_t *slot) {
    atomic_store_explicit(&slot->state, CLIMATE_SLOT_EMPTY, memory_order_release);
}
