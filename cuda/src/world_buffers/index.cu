/* See README.md. The slab pool and the toroidal column table. Blocks come from the owner. Everything the host must remember
 * about them lives in host memory, since no free slab is worth a read from the card. */
#include "world_buffers/index.hu"

#include "device_memory/index.hu"

#include <string.h>

/* The sizes of slab, in granules. The last is the smallest that holds the largest section there can be. */
static const uint16_t world_buffers_class_granules[WORLD_BUFFERS_CLASS_COUNT] = {1u,  2u,  4u,  6u,  8u,  12u,
                                                                                 16u, 24u, 32u, 48u, 64u, 102u};
static_assert(102u * WORLD_BUFFERS_GRANULE_BYTES >= WORLD_BUFFERS_SECTION_BYTES_MAX);
static_assert(101u * WORLD_BUFFERS_GRANULE_BYTES < WORLD_BUFFERS_SECTION_BYTES_MAX);

enum : uint32_t {
    /* A kernel's workgroup. */
    WORLD_BUFFERS_PROBE_THREADS = 256u,
    /* With at least one slot in this many dirty, one copy of the whole table is cheaper than one per slot: starting a copy
     * costs about what sending 15 KiB does, or 60 slots. See Stage 08.2's issues for the measurement. */
    WORLD_BUFFERS_FLUSH_WHOLE_RATIO = 64u,
};

static gpu_status_t world_buffers_refuse(world_buffers_t *buffers, gpu_status_t status, const char *what) {
    buffers->last_error = gpu_error_t{what, nullptr, nullptr, 0, status};
    return status;
}

uint64_t world_buffers_table_bytes(uint32_t slot_count) {
    return (uint64_t)slot_count * sizeof(world_buffers_column_t);
}

static uint64_t world_buffers_round_8(uint64_t bytes) {
    return (bytes + 7u) & ~(uint64_t)7u;
}

uint64_t world_buffers_host_bytes(uint32_t slot_count, uint64_t pool_bytes) {
    const uint64_t pages = pool_bytes / WORLD_BUFFERS_PAGE_BYTES;
    return world_buffers_table_bytes(slot_count) + world_buffers_round_8((uint64_t)slot_count * sizeof(uint32_t)) +
           world_buffers_round_8((uint64_t)slot_count) + pages * sizeof(world_buffers_page_t) +
           pages * WORLD_BUFFERS_PAGE_GRANULES * sizeof(uint16_t);
}

/* The smallest class a section of this many bytes fits, or the count of classes if none does. */
static uint32_t world_buffers_class_for(uint64_t section_bytes) {
    for (uint32_t i = 0; i < WORLD_BUFFERS_CLASS_COUNT; i++) {
        if (section_bytes <= (uint64_t)world_buffers_class_granules[i] * WORLD_BUFFERS_GRANULE_BYTES) {
            return i;
        }
    }
    return WORLD_BUFFERS_CLASS_COUNT;
}

uint64_t world_buffers_slab_bytes(uint64_t section_bytes) {
    if (section_bytes == 0u || section_bytes > WORLD_BUFFERS_SECTION_BYTES_MAX) {
        return 0u;
    }
    return (uint64_t)world_buffers_class_granules[world_buffers_class_for(section_bytes)] * WORLD_BUFFERS_GRANULE_BYTES;
}

gpu_status_t world_buffers_init(world_buffers_t *buffers, device_memory_t *memory, const device_block_t *pool,
                                const device_block_t *table, world_buffers_window_t window, uint8_t *host,
                                uint64_t host_bytes) {
    memset(buffers, 0, sizeof(*buffers));
    if (window.slot_count == 0u || window.slot_count != window.side * window.side) {
        return world_buffers_refuse(buffers, GPU_STATUS_INVALID_ARGUMENT, "a table that is not a square of slots");
    }
    if (pool->bytes == 0u || pool->bytes % WORLD_BUFFERS_PAGE_BYTES != 0u) {
        return world_buffers_refuse(buffers, GPU_STATUS_INVALID_ARGUMENT, "a pool that is not a whole number of pages");
    }
    if (table->bytes != world_buffers_table_bytes(window.slot_count)) {
        return world_buffers_refuse(buffers, GPU_STATUS_INVALID_ARGUMENT, "a table block of the wrong size");
    }
    if (host_bytes < world_buffers_host_bytes(window.slot_count, pool->bytes) || ((uintptr_t)host & 7u) != 0u) {
        return world_buffers_refuse(buffers, GPU_STATUS_INVALID_ARGUMENT, "too little host memory for these buffers");
    }
    buffers->borrowed_memory = memory;
    buffers->borrowed_pool = pool;
    buffers->borrowed_table = table;
    buffers->pool_bytes = pool->bytes;
    buffers->page_count = (uint32_t)(pool->bytes / WORLD_BUFFERS_PAGE_BYTES);
    buffers->window = window;
    buffers->free_pages = WORLD_BUFFERS_PAGE_NONE;
    for (uint32_t i = 0; i < WORLD_BUFFERS_CLASS_COUNT; i++) {
        buffers->class_pages[i] = WORLD_BUFFERS_PAGE_NONE;
    }

    uint8_t *cursor = host;
    buffers->borrowed_shadow = (world_buffers_column_t *)(void *)cursor;
    cursor += world_buffers_table_bytes(window.slot_count);
    buffers->borrowed_dirty_slots = (uint32_t *)(void *)cursor;
    cursor += world_buffers_round_8((uint64_t)window.slot_count * sizeof(uint32_t));
    buffers->borrowed_dirty_flags = cursor;
    cursor += world_buffers_round_8((uint64_t)window.slot_count);
    buffers->borrowed_pages = (world_buffers_page_t *)(void *)cursor;
    cursor += (uint64_t)buffers->page_count * sizeof(world_buffers_page_t);
    buffers->borrowed_slab_next = (uint16_t *)(void *)cursor;

    /* The host's memory may have been used, and a table on the card starts as whatever was there. Pages and slab links are
     * written before they are read, so that a large pool costs nothing until it fills. */
    memset(buffers->borrowed_shadow, 0, world_buffers_table_bytes(window.slot_count));
    memset(buffers->borrowed_dirty_flags, 0, window.slot_count);
    return device_memory_check(&buffers->last_error,
                               cudaMemcpy(device_memory_pointer(table), buffers->borrowed_shadow,
                                          world_buffers_table_bytes(window.slot_count), cudaMemcpyHostToDevice),
                               "clearing the column table");
}

gpu_status_t world_buffers_adopt(world_buffers_t *buffers, const world_buffers_t *from) {
    const uint32_t slots = buffers->window.slot_count;
    if (slots != from->window.slot_count || buffers->pool_bytes != from->pool_bytes) {
        return world_buffers_refuse(buffers, GPU_STATUS_INVALID_ARGUMENT, "taking over buffers of another size");
    }
    /* Laid out alike, as world_buffers_init lays them out, for the same sizes: one copy moves them all, and the links in the pages are
     * page and granule numbers, not addresses. */
    memcpy(buffers->borrowed_shadow, from->borrowed_shadow, world_buffers_host_bytes(slots, from->pool_bytes));
    buffers->window = from->window;
    buffers->slab_bytes = from->slab_bytes;
    buffers->pool_full_count = from->pool_full_count;
    buffers->top_limit = from->top_limit;
    memcpy(buffers->class_pages, from->class_pages, sizeof(buffers->class_pages));
    buffers->free_pages = from->free_pages;
    buffers->pages_used = from->pages_used;
    buffers->pages_live = from->pages_live;
    buffers->section_count = from->section_count;
    /* The card's table was cleared by world_buffers_init: the next flush sends all of it. */
    for (uint32_t slot = 0; slot < slots; slot++) {
        buffers->borrowed_dirty_slots[slot] = slot;
        buffers->borrowed_dirty_flags[slot] = 1u;
    }
    buffers->dirty_count = slots;
    return GPU_STATUS_OK;
}

void world_buffers_deinit(world_buffers_t *buffers) {
    memset(buffers, 0, sizeof(*buffers));
}

void world_buffers_set_window(world_buffers_t *buffers, world_buffers_window_t window) {
    buffers->window = window;
}

static void world_buffers_mark(world_buffers_t *buffers, uint32_t slot) {
    if (buffers->borrowed_dirty_flags[slot] == 0u) {
        buffers->borrowed_dirty_flags[slot] = 1u;
        buffers->borrowed_dirty_slots[buffers->dirty_count] = slot;
        buffers->dirty_count++;
    }
}

/* ---- The slab allocator ----------------------------------------------------------------------------------------- */

static void world_buffers_page_link(world_buffers_t *buffers, uint32_t page_index) {
    world_buffers_page_t *const page = &buffers->borrowed_pages[page_index];
    const uint32_t head = buffers->class_pages[page->class_index];
    page->previous = WORLD_BUFFERS_PAGE_NONE;
    page->next = head;
    if (head != WORLD_BUFFERS_PAGE_NONE) {
        buffers->borrowed_pages[head].previous = page_index;
    }
    buffers->class_pages[page->class_index] = page_index;
}

static void world_buffers_page_unlink(world_buffers_t *buffers, uint32_t page_index) {
    const world_buffers_page_t *const page = &buffers->borrowed_pages[page_index];
    if (page->previous != WORLD_BUFFERS_PAGE_NONE) {
        buffers->borrowed_pages[page->previous].next = page->next;
    } else {
        buffers->class_pages[page->class_index] = page->next;
    }
    if (page->next != WORLD_BUFFERS_PAGE_NONE) {
        buffers->borrowed_pages[page->next].previous = page->previous;
    }
}

/* Whether a page has a slab of its class left to give. */
static bool world_buffers_page_has_room(const world_buffers_page_t *page) {
    return page->free_head != WORLD_BUFFERS_SLAB_NONE ||
           (uint32_t)page->carved + world_buffers_class_granules[page->class_index] <= WORLD_BUFFERS_PAGE_GRANULES;
}

/* A page for this class: one with room, one given back, or one never used. False if there is none. */
static bool world_buffers_page_for(world_buffers_t *buffers, uint32_t class_index, uint32_t *out_page) {
    uint32_t page_index = buffers->class_pages[class_index];
    if (page_index != WORLD_BUFFERS_PAGE_NONE) {
        *out_page = page_index;
        return true;
    }
    if (buffers->free_pages != WORLD_BUFFERS_PAGE_NONE) {
        page_index = buffers->free_pages;
        buffers->free_pages = buffers->borrowed_pages[page_index].next;
    } else if (buffers->pages_used < buffers->page_count) {
        page_index = buffers->pages_used;
        buffers->pages_used++;
    } else {
        return false;
    }
    world_buffers_page_t *const page = &buffers->borrowed_pages[page_index];
    page->free_head = WORLD_BUFFERS_SLAB_NONE;
    page->carved = 0u;
    page->live = 0u;
    page->class_index = (uint8_t)class_index;
    world_buffers_page_link(buffers, page_index);
    buffers->pages_live++;
    *out_page = page_index;
    return true;
}

static bool world_buffers_slab_take(world_buffers_t *buffers, uint32_t class_index, uint32_t *out_granule) {
    uint32_t page_index = 0u;
    if (!world_buffers_page_for(buffers, class_index, &page_index)) {
        return false;
    }
    world_buffers_page_t *const page = &buffers->borrowed_pages[page_index];
    const uint32_t page_granule = page_index * WORLD_BUFFERS_PAGE_GRANULES;
    uint32_t local = 0u;
    if (page->free_head != WORLD_BUFFERS_SLAB_NONE) {
        local = page->free_head;
        page->free_head = buffers->borrowed_slab_next[page_granule + local];
    } else {
        local = page->carved;
        page->carved = (uint16_t)(local + world_buffers_class_granules[class_index]);
    }
    page->live++;
    if (!world_buffers_page_has_room(page)) {
        world_buffers_page_unlink(buffers, page_index);
    }
    buffers->slab_bytes += (uint64_t)world_buffers_class_granules[class_index] * WORLD_BUFFERS_GRANULE_BYTES;
    *out_granule = page_granule + local;
    return true;
}

static void world_buffers_slab_give(world_buffers_t *buffers, uint32_t granule) {
    const uint32_t page_index = granule / WORLD_BUFFERS_PAGE_GRANULES;
    world_buffers_page_t *const page = &buffers->borrowed_pages[page_index];
    const bool had_room = world_buffers_page_has_room(page);
    buffers->borrowed_slab_next[granule] = page->free_head;
    page->free_head = (uint16_t)(granule % WORLD_BUFFERS_PAGE_GRANULES);
    page->live--;
    buffers->slab_bytes -= (uint64_t)world_buffers_class_granules[page->class_index] * WORLD_BUFFERS_GRANULE_BYTES;
    if (page->live == 0u) {
        /* A page with nothing in it can serve any size, which is what stops one size starving the others for good. */
        if (had_room) {
            world_buffers_page_unlink(buffers, page_index);
        }
        page->next = buffers->free_pages;
        buffers->free_pages = page_index;
        buffers->pages_live--;
    } else if (!had_room) {
        world_buffers_page_link(buffers, page_index);
    }
}

/* ---- The table -------------------------------------------------------------------------------------------------- */

static void world_buffers_sections_release(world_buffers_t *buffers, world_buffers_column_t *column) {
    for (uint32_t i = 0; i < WORLD_BUFFERS_COLUMN_SECTIONS; i++) {
        if (column->sections[i] != 0u) {
            world_buffers_slab_give(buffers, column->sections[i] - 1u);
            column->sections[i] = 0u;
            buffers->section_count--;
        }
    }
    column->section_mask[0] = 0u;
    column->section_mask[1] = 0u;
}

void world_buffers_column_set(world_buffers_t *buffers, uint32_t slot, int32_t chunk_x, int32_t chunk_y,
                              uint32_t generation, int16_t top_z) {
    world_buffers_column_t *const column = &buffers->borrowed_shadow[slot];
    const bool same_load = column->ready != 0u && column->generation == generation;
    if (same_load && column->chunk_x == chunk_x && column->chunk_y == chunk_y && column->top_z == top_z) {
        return;
    }
    if (!same_load) {
        world_buffers_sections_release(buffers, column);
    }
    column->chunk_x = chunk_x;
    column->chunk_y = chunk_y;
    column->generation = generation;
    column->top_z = top_z;
    column->ready = 1u;
    /* Until world_buffers_region_note says: what it held was another column's square. */
    column->region_top = 0u;
    /* One above it, as a storage height: 1 at the least, as 0 means not known. */
    const int32_t above = (int32_t)top_z + (int32_t)WORLD_BUFFERS_Z_ORIGIN + 1;
    if (above > 0 && (uint32_t)above > buffers->top_limit) {
        buffers->top_limit = (uint32_t)above;
    } else if (buffers->top_limit == 0u) {
        buffers->top_limit = 1u;
    }
    world_buffers_mark(buffers, slot);
}

void world_buffers_region_note(world_buffers_t *buffers, uint32_t slot) {
    const world_buffers_column_t *const own = &buffers->borrowed_shadow[slot];
    const int32_t region_mask = (int32_t)WORLD_BUFFERS_REGION_COLUMNS - 1;
    const int32_t first_x = own->chunk_x & ~region_mask, first_y = own->chunk_y & ~region_mask;
    int32_t highest = 0;
    uint32_t slots[WORLD_BUFFERS_REGION_COLUMNS * WORLD_BUFFERS_REGION_COLUMNS];
    uint32_t count = 0u;
    for (uint32_t dy = 0u; dy < WORLD_BUFFERS_REGION_COLUMNS; dy++) {
        for (uint32_t dx = 0u; dx < WORLD_BUFFERS_REGION_COLUMNS; dx++) {
            const int32_t chunk_x = first_x + (int32_t)dx, chunk_y = first_y + (int32_t)dy;
            uint32_t other = 0u;
            if (!world_buffers_slot_of(&buffers->window, chunk_x, chunk_y, &other)) {
                continue;
            }
            const world_buffers_column_t *const column = &buffers->borrowed_shadow[other];
            if (column->ready == 0u || column->chunk_x != chunk_x || column->chunk_y != chunk_y) {
                continue;
            }
            const int32_t above = (int32_t)column->top_z + (int32_t)WORLD_BUFFERS_Z_ORIGIN + 1;
            highest = above > highest ? above : highest;
            slots[count] = other;
            count++;
        }
    }
    /* Up to whole sections, and past nothing it could hold. */
    const int32_t sections = (highest + 15) >> 4;
    const uint8_t top = (uint8_t)(sections < 1 ? 1 : (sections > (int32_t)UINT8_MAX ? (int32_t)UINT8_MAX : sections));
    for (uint32_t i = 0u; i < count; i++) {
        world_buffers_column_t *const column = &buffers->borrowed_shadow[slots[i]];
        if (column->region_top != top) {
            column->region_top = top;
            world_buffers_mark(buffers, slots[i]);
        }
    }
}

void world_buffers_column_clear(world_buffers_t *buffers, uint32_t slot) {
    world_buffers_column_t *const column = &buffers->borrowed_shadow[slot];
    if (column->ready == 0u) {
        return;
    }
    world_buffers_sections_release(buffers, column);
    memset(column, 0, sizeof(*column));
    world_buffers_mark(buffers, slot);
}

gpu_status_t world_buffers_section_write(world_buffers_t *buffers, uint32_t slot, uint32_t section_index,
                                         const void *bytes, uint64_t byte_count) {
    if (slot >= buffers->window.slot_count || section_index >= WORLD_BUFFERS_COLUMN_SECTIONS ||
        byte_count > WORLD_BUFFERS_SECTION_BYTES_MAX || buffers->borrowed_shadow[slot].ready == 0u) {
        return world_buffers_refuse(buffers, GPU_STATUS_INVALID_ARGUMENT, "writing a section that cannot be there");
    }
    world_buffers_column_t *const column = &buffers->borrowed_shadow[slot];
    const uint32_t old_entry = column->sections[section_index];
    if (byte_count == 0u) {
        if (old_entry != 0u) {
            world_buffers_slab_give(buffers, old_entry - 1u);
            column->sections[section_index] = 0u;
            column->section_mask[section_index >> 6] &= ~(1ull << (section_index & 63u));
            buffers->section_count--;
            world_buffers_mark(buffers, slot);
        }
        return GPU_STATUS_OK;
    }

    const uint32_t class_index = world_buffers_class_for(byte_count);
    const bool in_place =
        old_entry != 0u &&
        buffers->borrowed_pages[(old_entry - 1u) / WORLD_BUFFERS_PAGE_GRANULES].class_index == class_index;
    uint32_t granule = old_entry - 1u;
    if (!in_place && !world_buffers_slab_take(buffers, class_index, &granule)) {
        buffers->pool_full_count++;
        return world_buffers_refuse(buffers, GPU_STATUS_OVER_BUDGET, "finding a slab for a section");
    }
    uint8_t *const target =
        device_memory_pointer(buffers->borrowed_pool) + (uint64_t)granule * WORLD_BUFFERS_GRANULE_BYTES;
    const gpu_status_t copied =
        device_memory_check(&buffers->last_error, cudaMemcpy(target, bytes, byte_count, cudaMemcpyHostToDevice),
                            "sending a section to the card");
    if (copied != GPU_STATUS_OK) {
        if (!in_place) {
            world_buffers_slab_give(buffers, granule);
        }
        return copied;
    }
    if (in_place) {
        return GPU_STATUS_OK; /* the table already points here */
    }
    if (old_entry != 0u) {
        world_buffers_slab_give(buffers, old_entry - 1u);
    } else {
        buffers->section_count++;
    }
    column->sections[section_index] = granule + 1u;
    column->section_mask[section_index >> 6] |= 1ull << (section_index & 63u);
    world_buffers_mark(buffers, slot);
    return GPU_STATUS_OK;
}

gpu_status_t world_buffers_flush(world_buffers_t *buffers) {
    if (buffers->dirty_count == 0u) {
        return GPU_STATUS_OK;
    }
    uint8_t *const table = device_memory_pointer(buffers->borrowed_table);
    const uint8_t *const shadow = (const uint8_t *)buffers->borrowed_shadow;
    if ((uint64_t)buffers->dirty_count * WORLD_BUFFERS_FLUSH_WHOLE_RATIO >= buffers->window.slot_count) {
        const uint64_t bytes = world_buffers_table_bytes(buffers->window.slot_count);
        const gpu_status_t status = device_memory_check(
            &buffers->last_error, cudaMemcpy(table, shadow, bytes, cudaMemcpyHostToDevice), "sending the column table");
        if (status != GPU_STATUS_OK) {
            return status;
        }
        buffers->flush_bytes += bytes;
        buffers->flush_whole_count++;
    } else {
        for (uint32_t i = 0; i < buffers->dirty_count; i++) {
            const uint64_t offset = (uint64_t)buffers->borrowed_dirty_slots[i] * sizeof(world_buffers_column_t);
            const gpu_status_t status = device_memory_check(
                &buffers->last_error,
                cudaMemcpy(table + offset, shadow + offset, sizeof(world_buffers_column_t), cudaMemcpyHostToDevice),
                "sending a slot of the column table");
            if (status != GPU_STATUS_OK) {
                return status;
            }
        }
        buffers->flush_bytes += (uint64_t)buffers->dirty_count * sizeof(world_buffers_column_t);
    }
    for (uint32_t i = 0; i < buffers->dirty_count; i++) {
        buffers->borrowed_dirty_flags[buffers->borrowed_dirty_slots[i]] = 0u;
    }
    buffers->dirty_count = 0u;
    buffers->flush_count++;
    return GPU_STATUS_OK;
}

/* ---- The probe -------------------------------------------------------------------------------------------------- */

__global__ void world_buffers_probe_kernel(const world_buffers_column_t *table, const uint8_t *pool,
                                           world_buffers_window_t window, const world_buffers_probe_query_t *queries,
                                           world_buffers_probe_result_t *results, uint32_t count) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < count) {
        const world_buffers_probe_query_t query = queries[index];
        results[index] = world_buffers_voxel_lookup(table, pool, &window, query.x, query.y, query.z);
    }
}

gpu_status_t world_buffers_probe(world_buffers_t *buffers, const device_block_t *scratch,
                                 const world_buffers_probe_query_t *queries, world_buffers_probe_result_t *results,
                                 uint32_t count) {
    if (count == 0u) {
        return GPU_STATUS_OK;
    }
    const uint64_t query_bytes = (uint64_t)count * sizeof(world_buffers_probe_query_t);
    const uint64_t result_bytes = (uint64_t)count * sizeof(world_buffers_probe_result_t);
    if (scratch->bytes < query_bytes + result_bytes) {
        return world_buffers_refuse(buffers, GPU_STATUS_INVALID_ARGUMENT, "a probe's scratch block is too small");
    }
    device_memory_t *const memory = buffers->borrowed_memory;
    gpu_status_t status = device_memory_write(memory, scratch, 0u, queries, query_bytes);
    if (status != GPU_STATUS_OK) {
        buffers->last_error = memory->last_error;
        return status;
    }
    /* Results are 2-aligned and follow 4-aligned queries, so they need nothing between. */
    uint8_t *const base = device_memory_pointer(scratch);
    const uint32_t blocks = (count + WORLD_BUFFERS_PROBE_THREADS - 1u) / WORLD_BUFFERS_PROBE_THREADS;
    world_buffers_probe_kernel<<<blocks, WORLD_BUFFERS_PROBE_THREADS>>>(
        world_buffers_table_pointer(buffers), world_buffers_pool_pointer(buffers), buffers->window,
        (const world_buffers_probe_query_t *)(const void *)base,
        (world_buffers_probe_result_t *)(void *)(base + query_bytes), count);
    status = device_memory_check(&buffers->last_error, cudaGetLastError(), "starting the probe kernel");
    if (status != GPU_STATUS_OK) {
        return status;
    }
    status = device_memory_check(&buffers->last_error, cudaDeviceSynchronize(), "running the probe kernel");
    if (status != GPU_STATUS_OK) {
        return status;
    }
    status = device_memory_read(memory, scratch, query_bytes, results, result_bytes);
    if (status != GPU_STATUS_OK) {
        buffers->last_error = memory->last_error;
    }
    return status;
}
