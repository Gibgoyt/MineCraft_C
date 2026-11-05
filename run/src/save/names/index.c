/* See README.md. */
#include "save/names/index.h"

#include "assert/index.h"
#include "log/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "tables/mobs/index.h"

#include <inttypes.h>
#include <string.h>

/* A world could be held to this many of any table for ever, however it grows, and every id must leave SAVE_NAMES_LOST free. */
static_assert(BLOCK_COUNT <= SAVE_NAMES_ENTRIES_MAX);
static_assert(BLOCK_DROP_ITEM_COUNT <= SAVE_NAMES_ENTRIES_MAX);
static_assert(ITEM_COUNT <= SAVE_NAMES_ENTRIES_MAX);
static_assert(MOB_COUNT <= SAVE_NAMES_ENTRIES_MAX);
static_assert(SAVE_NAMES_ENTRIES_MAX < SAVE_NAMES_LOST);

enum : uint32_t {
    /* Past this many lines about what has gone, the log says how many more there were. */
    SAVE_NAMES_LOG_MOST = 32u,
};

uint32_t save_names_live_count(save_names_kind_t kind) {
    switch (kind) {
    case SAVE_NAMES_KIND_BLOCKS:
        return BLOCK_COUNT;
    case SAVE_NAMES_KIND_DROPS:
        return BLOCK_DROP_ITEM_COUNT;
    case SAVE_NAMES_KIND_ITEMS:
        return ITEM_COUNT;
    case SAVE_NAMES_KIND_MOBS:
        return MOB_COUNT;
    case SAVE_NAMES_KIND_COUNT:
        break;
    }
    ASSERT_ALWAYS(false);
    return 0u;
}

const char *save_names_live_name(save_names_kind_t kind, uint32_t id) {
    ASSERT_ALWAYS(id < save_names_live_count(kind));
    switch (kind) {
    case SAVE_NAMES_KIND_BLOCKS:
        return block_defs[id].name;
    case SAVE_NAMES_KIND_DROPS:
        return block_drop_items[id].name;
    case SAVE_NAMES_KIND_ITEMS:
        return item_defs[id].name;
    case SAVE_NAMES_KIND_MOBS:
        return mob_defs[id].name;
    case SAVE_NAMES_KIND_COUNT:
        break;
    }
    ASSERT_ALWAYS(false);
    return nullptr;
}

static uint64_t save_names_total_of(uint32_t names_bytes) {
    return sizeof(save_names_head_t) + (((uint64_t)names_bytes + 7u) & ~(uint64_t)7u);
}

uint64_t save_names_write(save_names_kind_t kind, uint8_t *out) {
    const uint32_t count = save_names_live_count(kind);
    uint64_t at = sizeof(save_names_head_t);
    for (uint32_t id = 0u; id < count; id++) {
        const char *const name = save_names_live_name(kind, id);
        const uint64_t bytes = strlen(name) + 1u;
        ASSERT_ALWAYS(bytes - 1u <= SAVE_NAMES_NAME_BYTES_MAX && at + bytes + 8u <= SAVE_NAMES_TABLE_BYTES_MAX);
        memcpy(out + at, name, bytes);
        at += bytes;
    }
    const save_names_head_t head = {.count = count, .bytes = (uint32_t)(at - sizeof(save_names_head_t))};
    memcpy(out, &head, sizeof(head));
    const uint64_t total = save_names_total_of(head.bytes);
    memset(out + at, 0, total - at);
    return total;
}

uint64_t save_names_write_all(uint8_t *out) {
    uint64_t at = 0u;
    for (uint32_t kind = 0u; kind < SAVE_NAMES_KIND_COUNT; kind++) {
        at += save_names_write((save_names_kind_t)kind, out + at);
    }
    return at;
}

/* Only what a table's names can be: they go into a log, and into nobody's path. */
static bool save_names_character_fine(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

bool save_names_read(const uint8_t *data, uint64_t bytes, save_names_view_t *out) {
    save_names_head_t head;
    if (bytes < sizeof(head)) {
        return false;
    }
    memcpy(&head, data, sizeof(head));
    if (head.count > SAVE_NAMES_ENTRIES_MAX || head.bytes > SAVE_NAMES_TABLE_BYTES_MAX - sizeof(head)) {
        return false;
    }
    const uint64_t total = save_names_total_of(head.bytes);
    if (total > bytes || total > SAVE_NAMES_TABLE_BYTES_MAX) {
        return false;
    }
    const char *const names = (const char *)(const void *)(data + sizeof(head));
    uint32_t seen = 0u;
    uint32_t length = 0u;
    for (uint32_t i = 0u; i < head.bytes; i++) {
        if (names[i] == '\0') {
            if (length == 0u) {
                return false;
            }
            seen++;
            length = 0u;
        } else {
            length++;
            if (length > SAVE_NAMES_NAME_BYTES_MAX || !save_names_character_fine(names[i])) {
                return false;
            }
        }
    }
    if (length != 0u || seen != head.count) {
        return false;
    }
    for (uint64_t i = sizeof(head) + head.bytes; i < total; i++) {
        if (data[i] != 0u) {
            return false;
        }
    }
    *out = (save_names_view_t){
        .borrowed_names = names, .total_bytes = total, .count = head.count, .names_bytes = head.bytes};
    return true;
}

bool save_names_are_live(const uint8_t *data, uint64_t bytes) {
    uint64_t at = 0u;
    for (uint32_t kind = 0u; kind < SAVE_NAMES_KIND_COUNT; kind++) {
        save_names_view_t view;
        if (!save_names_read(data + at, bytes - at, &view) ||
            view.count != save_names_live_count((save_names_kind_t)kind)) {
            return false;
        }
        const char *name = view.borrowed_names;
        for (uint32_t id = 0u; id < view.count; id++) {
            if (strcmp(name, save_names_live_name((save_names_kind_t)kind, id)) != 0) {
                return false;
            }
            name = save_names_next(name);
        }
        at += view.total_bytes;
    }
    return at == bytes;
}

uint32_t save_names_map(const save_names_view_t *view, save_names_kind_t kind, uint16_t *out) {
    const uint32_t live = save_names_live_count(kind);
    uint32_t lost = 0u;
    const char *name = view->borrowed_names;
    for (uint32_t id = 0u; id < view->count; id++) {
        /* Most lists are nearly this build's, so the same number is looked at first. The rest is a straight look: this runs once, on opening a world from another
         * build. */
        uint16_t found = SAVE_NAMES_LOST;
        if (id < live && strcmp(name, save_names_live_name(kind, id)) == 0) {
            found = (uint16_t)id;
        }
        for (uint32_t other = 0u; other < live && found == SAVE_NAMES_LOST; other++) {
            if (strcmp(name, save_names_live_name(kind, other)) == 0) {
                found = (uint16_t)other;
            }
        }
        out[id] = found;
        lost += found == SAVE_NAMES_LOST ? 1u : 0u;
        name = save_names_next(name);
    }
    return lost;
}

void save_names_lost_log(const save_names_view_t *view, const uint16_t *map, const char *where, const char *what,
                         const char *becomes) {
    uint32_t told = 0u;
    uint32_t more = 0u;
    const char *name = view->borrowed_names;
    for (uint32_t id = 0u; id < view->count; id++) {
        if (map[id] == SAVE_NAMES_LOST) {
            if (told < SAVE_NAMES_LOG_MOST) {
                LOG_WARN("save: %s was saved with the %s '%s', which this game does not have: %s", where, what, name,
                         becomes);
                told++;
            } else {
                more++;
            }
        }
        name = save_names_next(name);
    }
    if (more != 0u) {
        LOG_WARN("save: %s was saved with %" PRIu32 " more of those", where, more);
    }
}

const char *save_names_next(const char *name) {
    return name + strlen(name) + 1u;
}
