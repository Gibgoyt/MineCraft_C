/* See README.md.
 *
 * The one ordering this depends on: the producer copies an element in and then stores tail with release, and the
 * consumer loads tail with acquire and then copies it out. So an element is whole by the time the consumer can see it.
 * head works the same way in the other direction, and is what makes a slot free for reuse. Each side reads its own
 * index relaxed, since it alone writes it.
 *
 * head and tail never wrap in practice: at 4 billion elements a second, 64 bits last over a hundred years. So
 * tail - head is always the count, and a full ring and an empty one need no flag or wasted slot to tell apart. */
#include "thread/ring/index.h"

#include <string.h>

uint64_t ring_storage_bytes(uint64_t capacity, uint64_t element_bytes) {
    const bool is_power_of_two = capacity != 0 && (capacity & (capacity - 1u)) == 0;
    if (!is_power_of_two || element_bytes == 0 || capacity > UINT64_MAX / element_bytes) {
        return 0;
    }
    return capacity * element_bytes;
}

bool ring_init(ring_t *ring, void *borrowed_storage, uint64_t storage_bytes, uint64_t capacity,
               uint64_t element_bytes) {
    const uint64_t needed = ring_storage_bytes(capacity, element_bytes);
    if (needed == 0 || borrowed_storage == nullptr || storage_bytes < needed) {
        return false;
    }
    memset(ring, 0, sizeof(*ring));
    ring->borrowed_storage = borrowed_storage;
    ring->capacity = capacity;
    ring->element_bytes = element_bytes;
    return true;
}

void ring_deinit(ring_t *ring) {
    memset(ring, 0, sizeof(*ring));
}

/* Copies count elements between the ring and a flat array, from the slot of index, across the wrap if need be. */
static void ring_copy_in(ring_t *ring, uint64_t index, const uint8_t *from, uint64_t count) {
    const uint64_t slot = index & (ring->capacity - 1u);
    const uint64_t before_wrap = count < ring->capacity - slot ? count : ring->capacity - slot;
    memcpy(ring->borrowed_storage + slot * ring->element_bytes, from, before_wrap * ring->element_bytes);
    memcpy(ring->borrowed_storage, from + before_wrap * ring->element_bytes,
           (count - before_wrap) * ring->element_bytes);
}

static void ring_copy_out(const ring_t *ring, uint64_t index, uint8_t *to, uint64_t count) {
    const uint64_t slot = index & (ring->capacity - 1u);
    const uint64_t before_wrap = count < ring->capacity - slot ? count : ring->capacity - slot;
    memcpy(to, ring->borrowed_storage + slot * ring->element_bytes, before_wrap * ring->element_bytes);
    memcpy(to + before_wrap * ring->element_bytes, ring->borrowed_storage, (count - before_wrap) * ring->element_bytes);
}

uint64_t ring_push_many(ring_t *ring, const void *elements, uint64_t count) {
    const uint64_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    uint64_t free_slots = ring->capacity - (tail - ring->producer_seen_head);
    if (free_slots < count) {
        ring->producer_seen_head = atomic_load_explicit(&ring->head, memory_order_acquire);
        free_slots = ring->capacity - (tail - ring->producer_seen_head);
    }
    const uint64_t pushed = count < free_slots ? count : free_slots;
    if (pushed != 0) {
        ring_copy_in(ring, tail, elements, pushed);
        atomic_store_explicit(&ring->tail, tail + pushed, memory_order_release);
    }
    return pushed;
}

bool ring_try_push(ring_t *ring, const void *element) {
    return ring_push_many(ring, element, 1u) == 1u;
}

uint64_t ring_pop_many(ring_t *ring, void *out_elements, uint64_t count) {
    const uint64_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    uint64_t available = ring->consumer_seen_tail - head;
    if (available < count) {
        ring->consumer_seen_tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
        available = ring->consumer_seen_tail - head;
    }
    const uint64_t popped = count < available ? count : available;
    if (popped != 0) {
        ring_copy_out(ring, head, out_elements, popped);
        atomic_store_explicit(&ring->head, head + popped, memory_order_release);
    }
    return popped;
}

bool ring_try_pop(ring_t *ring, void *out_element) {
    return ring_pop_many(ring, out_element, 1u) == 1u;
}

uint64_t ring_count(const ring_t *ring) {
    /* head first: tail read after it cannot be behind it, so the difference cannot go negative. The consumer can move
     * on and the producer refill between the two reads, which would count past capacity. */
    const uint64_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    const uint64_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    const uint64_t count = tail - head;
    return count < ring->capacity ? count : ring->capacity;
}
