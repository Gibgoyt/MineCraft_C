/* See README.md. */
#include "save/state/translate/index.h"

#include "assert/index.h"
#include "item/crafting/index.h"
#include "log/index.h"
#include "save/state/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"

#include <inttypes.h>
#include <string.h>

enum : uint32_t {
    /* The bodies' arrays, in the order of save_state_body_fields. The kind is the one that means a table. */
    STATE_TRANSLATE_BODY_ARRAYS = 28u,
    STATE_TRANSLATE_BODY_HANDLE = 23u,
    STATE_TRANSLATE_BODY_KIND = 25u,
    STATE_TRANSLATE_SEEN_WORDS = ENTITY_STORE_CAPACITY / 64u,
};
static_assert(ENTITY_STORE_CAPACITY % 64u == 0u);
static const uint8_t STATE_TRANSLATE_BODY_BYTES[STATE_TRANSLATE_BODY_ARRAYS] = {
    8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, /* place, before, speed, wish, falls */
    4u, 4u, 4u, 4u, 4u, 4u, 4u, 4u, 4u, 4u,             /* box, yaw, health, limbs, head, aim, swell */
    4u, 4u, 2u, 1u, 1u,                                 /* handle, age, kind, flags, swing */
};

/* What one list of a file comes to in this build. */
typedef struct state_translate_table_t {
    const uint16_t *borrowed_map; /* 0: count: SAVE_NAMES_LOST for what this build has not got */
    uint32_t count;               /* 8 */
    uint8_t _pad0[4];             /* 12 */
} state_translate_table_t;
static_assert(sizeof(state_translate_table_t) == 16);

/* What is under way, and what has gone so far. */
typedef struct state_translate_t {
    state_translate_table_t tables[SAVE_NAMES_KIND_COUNT]; /* 0 */
    uint64_t lost_stacks;                                  /* 64: of what a player or a store held */
    uint64_t lost_drops;                                   /* 72: on the ground, and on their way there */
    uint64_t lost_bodies;                                  /* 80 */
} state_translate_t;
static_assert(sizeof(state_translate_t) == 88);

/* A number in a list's terms, in this build's. False if the list has no such number, which only a forged file can say. */
static bool state_translate_id(const state_translate_t *translate, save_names_kind_t kind, uint16_t id, uint16_t *out) {
    const state_translate_table_t *const table = &translate->tables[kind];
    if (id >= table->count) {
        return false;
    }
    *out = table->borrowed_map[id];
    return true;
}

/* An item that does not have to be there: a number that means nothing, or has gone, is no item. */
static uint16_t state_translate_item_or_none(const state_translate_t *translate, uint16_t id) {
    uint16_t item = ITEM_ID_NONE;
    if (!state_translate_id(translate, SAVE_NAMES_KIND_ITEMS, id, &item) || item == SAVE_NAMES_LOST) {
        return ITEM_ID_NONE;
    }
    return item;
}

static bool state_translate_stack(state_translate_t *translate, item_stack_t *stack) {
    if (stack->count == 0u) {
        return true; /* whether it is the one empty form is for the check */
    }
    uint16_t item = ITEM_ID_NONE;
    if (!state_translate_id(translate, SAVE_NAMES_KIND_ITEMS, stack->item, &item)) {
        return false;
    }
    if (item == SAVE_NAMES_LOST || item == ITEM_ID_NONE) {
        *stack = (item_stack_t){0};
        translate->lost_stacks++;
        return true;
    }
    stack->item = item;
    /* What the item is today says what a slot may hold of it, and a table that moved may have changed that too. */
    const uint32_t most = item_stack_max(item);
    if (stack->count > most) {
        stack->count = (uint8_t)most;
    }
    const uint16_t durability = item_defs[item].durability;
    if (durability == 0u) {
        stack->wear = 0u;
    } else if (stack->wear >= durability) {
        stack->wear = (uint16_t)(durability - 1u);
    }
    return true;
}

static bool state_translate_stacks(state_translate_t *translate, item_stack_t *stacks, uint32_t count) {
    for (uint32_t i = 0u; i < count; i++) {
        if (!state_translate_stack(translate, &stacks[i])) {
            return false;
        }
    }
    return true;
}

static bool state_translate_block(const state_translate_t *translate, uint16_t *block, bool *out_lost) {
    uint16_t live = BLOCK_ID_AIR;
    if (!state_translate_id(translate, SAVE_NAMES_KIND_BLOCKS, *block, &live)) {
        return false;
    }
    *out_lost = live == SAVE_NAMES_LOST;
    *block = *out_lost ? BLOCK_ID_AIR : live;
    return true;
}

/* ---- Each section ----------------------------------------------------------------------------------------------------- */

static bool state_translate_player(state_translate_t *translate, uint8_t *data, uint64_t bytes) {
    save_state_player_t player;
    if (bytes != sizeof(player)) {
        return false;
    }
    memcpy(&player, data, sizeof(player));
    player_interaction_t *const hands = &player.interaction;
    bool lost = false;
    if (!state_translate_block(translate, &hands->last_mined, &lost) ||
        !state_translate_block(translate, &hands->mining_block, &lost)) {
        return false;
    }
    if (lost) {
        hands->mining = 0u;
        hands->progress = 0u;
    }
    if (!state_translate_block(translate, &hands->hand_block, &lost)) {
        return false;
    }
    if (lost) {
        hands->hand_count = 0u;
    }
    if (hands->drop_head >= PLAYER_INTERACTION_DROPS || hands->drop_count > PLAYER_INTERACTION_DROPS) {
        return false;
    }
    /* Along the ring from its oldest, closing up what has gone. Each goes no further round than it was. */
    uint32_t kept = 0u;
    for (uint32_t i = 0u; i < hands->drop_count; i++) {
        player_drop_t drop = hands->drops[(hands->drop_head + i) % PLAYER_INTERACTION_DROPS];
        uint16_t item = 0u;
        if (!state_translate_id(translate, SAVE_NAMES_KIND_DROPS, drop.item, &item)) {
            return false;
        }
        if (item == SAVE_NAMES_LOST) {
            translate->lost_drops++;
            continue;
        }
        drop.item = item;
        hands->drops[(hands->drop_head + kept) % PLAYER_INTERACTION_DROPS] = drop;
        kept++;
    }
    hands->drop_count = kept;
    player.survival.held_item = state_translate_item_or_none(translate, player.survival.held_item);
    player.combat.held_item = state_translate_item_or_none(translate, player.combat.held_item);
    memcpy(data, &player, sizeof(player));
    return true;
}

/* What a grid makes is worked out from its cells, and save_state_apply does that with this build's recipes. */
static bool state_translate_items(state_translate_t *translate, uint8_t *data, uint64_t bytes) {
    save_state_items_t items;
    if (bytes != sizeof(items)) {
        return false;
    }
    memcpy(&items, data, sizeof(items));
    items.crafting_hands.output = (item_stack_t){0};
    items.crafting_table.output = (item_stack_t){0};
    items.crafting_bench.output = (item_stack_t){0};
    items.crafting_hands.recipe = CRAFTING_NO_RECIPE;
    items.crafting_table.recipe = CRAFTING_NO_RECIPE;
    items.crafting_bench.recipe = CRAFTING_NO_RECIPE;
    if (!state_translate_stacks(translate, items.slots, INVENTORY_SLOTS) ||
        !state_translate_stack(translate, &items.held) ||
        !state_translate_stacks(translate, items.crafting_hands.cells, RECIPE_CELLS) ||
        !state_translate_stacks(translate, items.crafting_table.cells, RECIPE_CELLS) ||
        !state_translate_stacks(translate, items.crafting_bench.cells, RECIPE_CELLS) ||
        !state_translate_stacks(translate, items.equipment.armor, EQUIPMENT_ARMOR_SLOTS)) {
        return false;
    }
    memcpy(data, &items, sizeof(items));
    return true;
}

static bool state_translate_chests(state_translate_t *translate, uint8_t *data, uint64_t bytes) {
    if (bytes % sizeof(container_chest_t) != 0u) {
        return false;
    }
    for (uint64_t at = 0u; at < bytes; at += sizeof(container_chest_t)) {
        container_chest_t chest;
        memcpy(&chest, data + at, sizeof(chest));
        if (!state_translate_stacks(translate, chest.slots, CONTAINER_CHEST_SLOTS)) {
            return false;
        }
        memcpy(data + at, &chest, sizeof(chest));
    }
    return true;
}

static bool state_translate_furnaces(state_translate_t *translate, uint8_t *data, uint64_t bytes) {
    if (bytes % sizeof(smelting_furnace_t) != 0u) {
        return false;
    }
    for (uint64_t at = 0u; at < bytes; at += sizeof(smelting_furnace_t)) {
        smelting_furnace_t furnace;
        memcpy(&furnace, data + at, sizeof(furnace));
        if (!state_translate_stacks(translate, furnace.slots, SMELTING_SLOTS)) {
            return false;
        }
        const uint16_t cooking = state_translate_item_or_none(translate, furnace.cooking);
        if (cooking == ITEM_ID_NONE) {
            furnace.cook_ticks = 0u;
        }
        furnace.cooking = cooking;
        memcpy(data + at, &furnace, sizeof(furnace));
    }
    return true;
}

/* Shrinks what lies on the ground by what has gone, and says how long the section is now. */
static bool state_translate_drops(state_translate_t *translate, uint8_t *data, uint64_t *in_out_bytes) {
    save_state_drops_t head;
    if (*in_out_bytes < sizeof(head)) {
        return false;
    }
    memcpy(&head, data, sizeof(head));
    if (head.count > ITEM_DROP_CAPACITY || *in_out_bytes != sizeof(head) + (uint64_t)head.count * sizeof(item_drop_t)) {
        return false;
    }
    uint32_t kept = 0u;
    for (uint32_t i = 0u; i < head.count; i++) {
        item_drop_t drop;
        memcpy(&drop, data + sizeof(head) + (uint64_t)i * sizeof(drop), sizeof(drop));
        uint16_t item = 0u;
        if (!state_translate_id(translate, SAVE_NAMES_KIND_DROPS, drop.item, &item)) {
            return false;
        }
        if (item == SAVE_NAMES_LOST) {
            /* Counted in items, so that a store's books still add up. */
            head.lost_count += drop.count;
            translate->lost_drops++;
            continue;
        }
        drop.item = item;
        memcpy(data + sizeof(head) + (uint64_t)kept * sizeof(drop), &drop, sizeof(drop));
        kept++;
    }
    head.count = kept;
    memcpy(data, &head, sizeof(head));
    *in_out_bytes = sizeof(head) + (uint64_t)kept * sizeof(item_drop_t);
    return true;
}

/* Where an array of the bodies' starts, for this many bodies. */
static uint64_t state_translate_body_array_at(uint32_t array, uint32_t bodies) {
    uint64_t at = 0u;
    for (uint32_t i = 0u; i < array; i++) {
        at += (uint64_t)bodies * STATE_TRANSLATE_BODY_BYTES[i];
    }
    return at;
}

/* Gives back the slot of a body that has gone, as entity_store_remove does. */
static void state_translate_slot_free(uint8_t *tables, uint32_t slot, uint32_t free_head, uint32_t free_count) {
    const uint64_t each = ENTITY_STORE_CAPACITY * sizeof(uint32_t);
    const uint32_t nowhere = ENTITY_STORE_CAPACITY;
    memcpy(tables + (uint64_t)slot * sizeof(uint32_t), &nowhere, sizeof(nowhere));
    uint32_t generation = 0u;
    memcpy(&generation, tables + each + (uint64_t)slot * sizeof(generation), sizeof(generation));
    generation = generation + 1u >= ENTITY_STORE_GENERATION_LIMIT ? 1u : generation + 1u;
    memcpy(tables + each + (uint64_t)slot * sizeof(generation), &generation, sizeof(generation));
    const uint32_t ring = (free_head + free_count - 1u) & ENTITY_STORE_SLOT_MASK;
    memcpy(tables + 2u * each + (uint64_t)ring * sizeof(slot), &slot, sizeof(slot));
}

/* Each body's kind in this build's table. A body of a kind this build has not got goes, its slot back in the free ring, and the arrays close up, in order. A mind whose slot it
 * was starts over, as for any body that has gone. */
static bool state_translate_bodies(state_translate_t *translate, uint8_t *data, uint64_t *in_out_bytes) {
    save_state_bodies_t head;
    if (*in_out_bytes < sizeof(head)) {
        return false;
    }
    memcpy(&head, data, sizeof(head));
    const uint64_t tables_bytes = (uint64_t)SAVE_STATE_SLOT_TABLES * ENTITY_STORE_CAPACITY * sizeof(uint32_t);
    if (head.count > ENTITY_STORE_CAPACITY || head.free_head >= ENTITY_STORE_CAPACITY ||
        *in_out_bytes != sizeof(head) + tables_bytes + (uint64_t)head.count * SAVE_STATE_BODY_BYTES) {
        return false;
    }
    uint8_t *const tables = data + sizeof(head);
    uint8_t *const arrays = tables + tables_bytes;
    uint8_t *const kinds = arrays + state_translate_body_array_at(STATE_TRANSLATE_BODY_KIND, head.count);
    const uint8_t *const handles = arrays + state_translate_body_array_at(STATE_TRANSLATE_BODY_HANDLE, head.count);
    uint64_t gone[STATE_TRANSLATE_SEEN_WORDS] = {0};
    uint32_t kept = 0u;
    for (uint32_t i = 0u; i < head.count; i++) {
        uint16_t kind = 0u;
        memcpy(&kind, kinds + (uint64_t)i * sizeof(kind), sizeof(kind));
        uint16_t live = 0u;
        if (!state_translate_id(translate, SAVE_NAMES_KIND_MOBS, kind, &live)) {
            return false;
        }
        entity_handle_t handle = 0u;
        memcpy(&handle, handles + (uint64_t)i * sizeof(handle), sizeof(handle));
        const uint32_t slot = entity_handle_slot(handle);
        if (live == SAVE_NAMES_LOST) {
            gone[i / 64u] |= 1ull << (i % 64u);
            continue;
        }
        memcpy(kinds + (uint64_t)i * sizeof(live), &live, sizeof(live));
        memcpy(tables + (uint64_t)slot * sizeof(kept), &kept, sizeof(kept));
        kept++;
    }
    const uint32_t lost = head.count - kept;
    if (lost == 0u) {
        return true;
    }
    uint32_t free_count = ENTITY_STORE_CAPACITY - head.count;
    for (uint32_t i = 0u; i < head.count; i++) {
        if ((gone[i / 64u] & (1ull << (i % 64u))) != 0u) {
            entity_handle_t handle = 0u;
            memcpy(&handle, handles + (uint64_t)i * sizeof(handle), sizeof(handle));
            free_count++;
            state_translate_slot_free(tables, entity_handle_slot(handle), head.free_head, free_count);
        }
    }
    /* Each array to where it goes for the fewer bodies, which is never later than where it was. */
    for (uint32_t array = 0u; array < STATE_TRANSLATE_BODY_ARRAYS; array++) {
        const uint32_t each = STATE_TRANSLATE_BODY_BYTES[array];
        const uint8_t *const from = arrays + state_translate_body_array_at(array, head.count);
        uint8_t *const to = arrays + state_translate_body_array_at(array, kept);
        uint32_t written = 0u;
        for (uint32_t i = 0u; i < head.count; i++) {
            if ((gone[i / 64u] & (1ull << (i % 64u))) == 0u) {
                memmove(to + (uint64_t)written * each, from + (uint64_t)i * each, each);
                written++;
            }
        }
    }
    head.count = kept;
    head.removed_count += lost;
    memcpy(data, &head, sizeof(head));
    translate->lost_bodies += lost;
    *in_out_bytes -= (uint64_t)lost * SAVE_STATE_BODY_BYTES;
    return true;
}

/* One section, where it now lies. Its length may come down. */
static bool state_translate_section(state_translate_t *translate, uint32_t id, uint8_t *data, uint64_t *in_out_bytes) {
    switch ((save_state_section_id_t)id) {
    case SAVE_STATE_SECTION_PLAYER:
        return state_translate_player(translate, data, *in_out_bytes);
    case SAVE_STATE_SECTION_ITEMS:
        return state_translate_items(translate, data, *in_out_bytes);
    case SAVE_STATE_SECTION_CHESTS:
        return state_translate_chests(translate, data, *in_out_bytes);
    case SAVE_STATE_SECTION_FURNACES:
        return state_translate_furnaces(translate, data, *in_out_bytes);
    case SAVE_STATE_SECTION_DROPS:
        return state_translate_drops(translate, data, in_out_bytes);
    case SAVE_STATE_SECTION_BODIES:
        return state_translate_bodies(translate, data, in_out_bytes);
    case SAVE_STATE_SECTION_TIME:
    case SAVE_STATE_SECTION_ARROWS:
    case SAVE_STATE_SECTION_SPAWN:
    case SAVE_STATE_SECTION_MINDS:
    case SAVE_STATE_SECTION_NAMES:
        return true; /* no number in these is a place in a table */
    }
    return false;
}

/* ---- The whole file ---------------------------------------------------------------------------------------------------- */

/* The maps, from the file's section of names, with what has gone told to the log. False if there is no good list. */
static bool state_translate_maps(state_translate_t *translate, const uint8_t *file, uint64_t bytes, uint16_t *maps,
                                 const char *where) {
    uint64_t names_bytes = 0u;
    const uint8_t *const names = save_state_section_find(file, bytes, SAVE_STATE_SECTION_NAMES, &names_bytes);
    if (names == nullptr) {
        return false;
    }
    static const char *const what[SAVE_NAMES_KIND_COUNT] = {"block", "dropped item", "item", "mob"};
    static const char *const becomes[SAVE_NAMES_KIND_COUNT] = {"what held it no longer does", "those on the ground go",
                                                               "those held go, and each slot is empty",
                                                               "those alive go"};
    uint64_t at = 0u;
    for (uint32_t kind = 0u; kind < SAVE_NAMES_KIND_COUNT; kind++) {
        save_names_view_t view;
        if (!save_names_read(names + at, names_bytes - at, &view)) {
            return false;
        }
        uint16_t *const map = maps + (uint64_t)kind * SAVE_NAMES_ENTRIES_MAX;
        if (save_names_map(&view, (save_names_kind_t)kind, map) != 0u) {
            save_names_lost_log(&view, map, where, what[kind], becomes[kind]);
        }
        translate->tables[kind] = (state_translate_table_t){.borrowed_map = map, .count = view.count, ._pad0 = {0}};
        at += view.total_bytes;
    }
    return at == names_bytes;
}

save_world_status_t save_state_translate(uint8_t *file, uint64_t *in_out_bytes, uint64_t capacity, uint16_t *maps,
                                         const char *where, uint32_t *out_section_count) {
    const uint64_t bytes = *in_out_bytes;
    state_translate_t translate;
    memset(&translate, 0, sizeof(translate));
    if (!state_translate_maps(&translate, file, bytes, maps, where)) {
        LOG_ERROR("save: the state in %s was saved with other tables, and has no good list of what they were", where);
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    /* In order, closing up: nothing ever goes later than it was, so nothing not yet read is written over. */
    uint64_t read_at = sizeof(save_state_header_t);
    uint64_t write_at = read_at;
    uint32_t count = 0u;
    while (read_at < bytes) {
        save_state_section_t section;
        if (bytes - read_at < sizeof(section)) {
            return SAVE_WORLD_ERROR_DAMAGED;
        }
        memcpy(&section, file + read_at, sizeof(section));
        read_at += sizeof(section);
        const uint64_t padded = (section.bytes + 7u) & ~(uint64_t)7u;
        if (section.bytes > bytes - read_at || padded > bytes - read_at) {
            return SAVE_WORLD_ERROR_DAMAGED;
        }
        if (section.id != SAVE_STATE_SECTION_NAMES) {
            memmove(file + write_at + sizeof(section), file + read_at, section.bytes);
            uint64_t now_bytes = section.bytes;
            if (!state_translate_section(&translate, section.id, file + write_at + sizeof(section), &now_bytes)) {
                LOG_ERROR("save: part %" PRIu32 " of the state in %s says what cannot be", section.id, where);
                return SAVE_WORLD_ERROR_DAMAGED;
            }
            section.bytes = now_bytes;
            memcpy(file + write_at, &section, sizeof(section));
            write_at += sizeof(section) + now_bytes;
            while ((write_at & 7u) != 0u) {
                file[write_at] = 0u;
                write_at++;
            }
            count++;
        }
        read_at += padded;
    }
    /* A file of good lengths with this much would not fit one this game wrote. */
    if (write_at + sizeof(save_state_section_t) + SAVE_STATE_NAMES_BYTES_MAX > capacity) {
        LOG_ERROR("save: the state in %s is larger than any state", where);
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    const uint64_t names_bytes = save_names_write_all(file + write_at + sizeof(save_state_section_t));
    const save_state_section_t names = {.bytes = names_bytes, .id = SAVE_STATE_SECTION_NAMES, ._pad0 = {0}};
    memcpy(file + write_at, &names, sizeof(names));
    write_at += sizeof(names) + names_bytes;
    count++;
    if (translate.lost_stacks != 0u || translate.lost_drops != 0u || translate.lost_bodies != 0u) {
        LOG_WARN("save: of the state in %s, %" PRIu64 " stacks held, %" PRIu64 " stacks dropped and %" PRIu64
                 " bodies were of things this game does not have, and are gone",
                 where, translate.lost_stacks, translate.lost_drops, translate.lost_bodies);
    }
    LOG_INFO("save: the state in %s was saved with other tables, and each number in it has been found by name", where);
    *in_out_bytes = write_at;
    *out_section_count = count;
    return SAVE_WORLD_OK;
}
