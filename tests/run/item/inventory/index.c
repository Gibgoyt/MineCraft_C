/* ctest run_item_inventory. See README.md. */
#include "harness/index.h"

#include "file/index.h"
#include "heap/index.h"
#include "item/index.h"
#include "main/index.h"
#include "tables/blocks/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    II_PROGRAM_TIMEOUT_MS = 120000u,
    II_RANDOM_STEPS = 200000u,
    II_RANDOM_STACKS = 8u,
    II_THREAD_SCROLLS = 200000u,
    II_FULL = INVENTORY_SLOTS * 64u,
};

static uint64_t ii_random(uint64_t *state) {
    /* xorshift64*: only so that a failure comes back the same. */
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x9E3779B97F4A7C15ull;
}

static uint16_t ii_random_item(uint64_t *state) {
    return (uint16_t)(1u + ii_random(state) % (ITEM_COUNT - 1u));
}

/* An inventory with an arena of the test's own. */
static void ii_inventory(test_context_t *t, inventory_t *inventory, arena_t *arena) {
    arena_init(arena, "item_test", test_alloc(t, INVENTORY_BYTES), INVENTORY_BYTES);
    inventory_start(inventory, arena);
}

static void ii_all_valid(test_context_t *t, const inventory_t *inventory) {
    for (uint32_t i = 0u; i < INVENTORY_SLOTS; i++) {
        CHECK(item_stack_is_valid(inventory_slot_read(inventory, i)));
    }
}

static void ii_books_balance(test_context_t *t, const inventory_t *inventory) {
    CHECK_EQ_U64(inventory->added_count, inventory->removed_count + inventory_items_held(inventory));
}

/* ---- Stacks ------------------------------------------------------------------------------------------------------- */

TEST(a_new_stack_is_cut_to_what_its_item_holds_for_every_item) {
    for (uint32_t i = 1u; i < ITEM_COUNT; i++) {
        const uint32_t most = item_defs[i].max_stack;
        CHECK_EQ_U64(item_stack_max((uint16_t)i), most);
        const item_stack_t whole = item_stack_make((uint16_t)i, 1000u);
        CHECK_EQ_U64(whole.count, most);
        CHECK_EQ_U64(whole.item, i);
        CHECK(item_stack_is_valid(&whole));
        const item_stack_t one = item_stack_make((uint16_t)i, 1u);
        CHECK_EQ_U64(one.count, 1u);
        const item_stack_t none = item_stack_make((uint16_t)i, 0u);
        const item_stack_t zero = {0};
        CHECK_MEM_EQ(&none, &zero, sizeof(zero));
    }
    CHECK_EQ_U64(item_stack_max(ITEM_ID_NONE), 0u);
    CHECK_EQ_U64(item_stack_max(ITEM_COUNT), 0u);
    CHECK_EQ_U64(item_stack_max(UINT16_MAX), 0u);
    CHECK(item_stack_is_empty(&(item_stack_t){0}));
    const item_stack_t nothing = item_stack_make(ITEM_ID_NONE, 5u);
    CHECK(item_stack_is_empty(&nothing));
    const item_stack_t no_such = item_stack_make(ITEM_COUNT, 5u);
    CHECK(item_stack_is_empty(&no_such));
}

TEST(what_is_a_stack_and_what_is_not) {
    CHECK(item_stack_is_valid(&(item_stack_t){0}));
    CHECK(item_stack_is_valid(&(item_stack_t){.item = ITEM_ID_DIRT, .count = 64u}));
    CHECK(item_stack_is_valid(&(item_stack_t){.item = ITEM_ID_IRON_PICKAXE, .count = 1u, .wear = 249u}));

    CHECK(!item_stack_is_valid(&(item_stack_t){.item = ITEM_ID_DIRT, .count = 65u}));
    CHECK(!item_stack_is_valid(&(item_stack_t){.item = ITEM_ID_SNOWBALL, .count = 17u}));
    CHECK(!item_stack_is_valid(&(item_stack_t){.item = ITEM_ID_IRON_PICKAXE, .count = 2u}));
    CHECK(!item_stack_is_valid(&(item_stack_t){.item = ITEM_ID_NONE, .count = 1u}));
    CHECK(!item_stack_is_valid(&(item_stack_t){.item = ITEM_COUNT, .count = 1u}));
    /* There is one empty stack. */
    CHECK(!item_stack_is_valid(&(item_stack_t){.item = ITEM_ID_DIRT}));
    CHECK(!item_stack_is_valid(&(item_stack_t){.wear = 1u}));
    /* Wear on what does not wear, and on what has none left. */
    CHECK(!item_stack_is_valid(&(item_stack_t){.item = ITEM_ID_DIRT, .count = 1u, .wear = 1u}));
    CHECK(!item_stack_is_valid(&(item_stack_t){.item = ITEM_ID_IRON_PICKAXE, .count = 1u, .wear = 250u}));
    CHECK(!item_stack_is_valid(&(item_stack_t){._pad0 = {0u, 1u, 0u}}));
}

TEST(moving_takes_what_fits_and_says_how_much) {
    item_stack_t from = item_stack_make(ITEM_ID_DIRT, 40u);
    item_stack_t to = item_stack_make(ITEM_ID_DIRT, 30u);
    CHECK_EQ_U64(item_stack_room(&to, &from), 34u);
    CHECK_EQ_U64(item_stack_move(&from, &to, 100u), 34u);
    CHECK_EQ_U64(to.count, 64u);
    CHECK_EQ_U64(from.count, 6u);
    CHECK_EQ_U64(from.item, ITEM_ID_DIRT);
    /* Full now. */
    CHECK_EQ_U64(item_stack_move(&from, &to, 1u), 0u);
    CHECK_EQ_U64(from.count, 6u);

    /* Less than all of it, and into nothing. */
    item_stack_t empty = {0};
    CHECK_EQ_U64(item_stack_move(&from, &empty, 2u), 2u);
    CHECK_EQ_U64(empty.item, ITEM_ID_DIRT);
    CHECK_EQ_U64(empty.count, 2u);
    CHECK_EQ_U64(from.count, 4u);

    /* Moving all of it leaves the one empty stack. */
    CHECK_EQ_U64(item_stack_move(&from, &empty, 4u), 4u);
    const item_stack_t zero = {0};
    CHECK_MEM_EQ(&from, &zero, sizeof(zero));

    /* From nothing, into something else, and onto itself. */
    CHECK_EQ_U64(item_stack_move(&from, &empty, 4u), 0u);
    CHECK_EQ_U64(empty.count, 6u);
    item_stack_t coal = item_stack_make(ITEM_ID_COAL, 3u);
    CHECK(!item_stack_can_merge(&coal, &empty));
    CHECK_EQ_U64(item_stack_room(&empty, &coal), 0u);
    CHECK_EQ_U64(item_stack_move(&coal, &empty, 3u), 0u);
    CHECK_EQ_U64(coal.count, 3u);
    CHECK_EQ_U64(empty.item, ITEM_ID_DIRT);
    CHECK_EQ_U64(item_stack_move(&coal, &coal, 1u), 0u);
    CHECK_EQ_U64(coal.count, 3u);
}

TEST(each_item_has_its_own_limit) {
    item_stack_t from = item_stack_make(ITEM_ID_SNOWBALL, 16u);
    item_stack_t to = item_stack_make(ITEM_ID_SNOWBALL, 10u);
    CHECK_EQ_U64(item_stack_move(&from, &to, 16u), 6u);
    CHECK_EQ_U64(to.count, 16u);
    item_stack_t pick = item_stack_make(ITEM_ID_WOOD_PICKAXE, 5u);
    CHECK_EQ_U64(pick.count, 1u);
    item_stack_t other = item_stack_make(ITEM_ID_WOOD_PICKAXE, 1u);
    CHECK_EQ_U64(item_stack_move(&pick, &other, 1u), 0u);
}

TEST(wear_goes_with_a_stack_and_keeps_it_apart) {
    item_stack_t worn = {.item = ITEM_ID_IRON_PICKAXE, .count = 1u, .wear = 7u};
    item_stack_t empty = {0};
    CHECK_EQ_U64(item_stack_move(&worn, &empty, 1u), 1u);
    CHECK_EQ_U64(empty.wear, 7u);
    const item_stack_t zero = {0};
    CHECK_MEM_EQ(&worn, &zero, sizeof(zero));
    const item_stack_t a = {.item = ITEM_ID_IRON_PICKAXE, .count = 1u, .wear = 1u};
    const item_stack_t b = {.item = ITEM_ID_IRON_PICKAXE, .count = 1u, .wear = 2u};
    CHECK(!item_stack_can_merge(&a, &b));
    CHECK(item_stack_can_merge(&a, &a));
    CHECK(item_stack_can_merge(&a, &zero));
    CHECK(item_stack_can_merge(&zero, &a));
}

TEST(taking_stops_at_nothing_and_leaves_the_one_empty_stack) {
    item_stack_t stack = item_stack_make(ITEM_ID_DIRT, 5u);
    CHECK_EQ_U64(item_stack_take(&stack, 2u), 2u);
    CHECK_EQ_U64(stack.count, 3u);
    CHECK_EQ_U64(item_stack_take(&stack, 0u), 0u);
    CHECK_EQ_U64(stack.count, 3u);
    CHECK_EQ_U64(item_stack_take(&stack, 10u), 3u);
    const item_stack_t zero = {0};
    CHECK_MEM_EQ(&stack, &zero, sizeof(zero));
    CHECK_EQ_U64(item_stack_take(&stack, 1u), 0u);
    item_stack_t worn = {.item = ITEM_ID_IRON_PICKAXE, .count = 1u, .wear = 3u};
    CHECK_EQ_U64(item_stack_take(&worn, 1u), 1u);
    CHECK_MEM_EQ(&worn, &zero, sizeof(zero));
}

TEST(the_first_click_picks_up_puts_down_or_swaps) {
    item_stack_t slot = item_stack_make(ITEM_ID_DIRT, 10u);
    item_stack_t cursor = {0};
    item_stack_click_primary(&slot, &cursor);
    CHECK(item_stack_is_empty(&slot));
    CHECK_EQ_U64(cursor.count, 10u);
    item_stack_click_primary(&slot, &cursor);
    CHECK(item_stack_is_empty(&cursor));
    CHECK_EQ_U64(slot.count, 10u);

    /* What fits, and the pointer keeps the rest. */
    cursor = item_stack_make(ITEM_ID_DIRT, 60u);
    item_stack_click_primary(&slot, &cursor);
    CHECK_EQ_U64(slot.count, 64u);
    CHECK_EQ_U64(cursor.count, 6u);

    /* Something else: they change places. */
    cursor = item_stack_make(ITEM_ID_COAL, 3u);
    item_stack_click_primary(&slot, &cursor);
    CHECK_EQ_U64(slot.item, ITEM_ID_COAL);
    CHECK_EQ_U64(slot.count, 3u);
    CHECK_EQ_U64(cursor.item, ITEM_ID_DIRT);
    CHECK_EQ_U64(cursor.count, 64u);

    /* Nothing on nothing is still nothing. */
    item_stack_t empty_slot = {0}, empty_cursor = {0};
    item_stack_click_primary(&empty_slot, &empty_cursor);
    CHECK(item_stack_is_empty(&empty_slot));
    CHECK(item_stack_is_empty(&empty_cursor));
}

TEST(the_second_click_picks_up_the_larger_half_puts_down_one_or_swaps) {
    item_stack_t slot = item_stack_make(ITEM_ID_DIRT, 9u);
    item_stack_t cursor = {0};
    item_stack_click_secondary(&slot, &cursor);
    CHECK_EQ_U64(cursor.count, 5u);
    CHECK_EQ_U64(slot.count, 4u);
    item_stack_click_secondary(&slot, &cursor);
    CHECK_EQ_U64(cursor.count, 4u);
    CHECK_EQ_U64(slot.count, 5u);

    item_stack_t empty = {0};
    item_stack_click_secondary(&empty, &cursor);
    CHECK_EQ_U64(empty.item, ITEM_ID_DIRT);
    CHECK_EQ_U64(empty.count, 1u);
    CHECK_EQ_U64(cursor.count, 3u);

    /* One of one is all of it. */
    item_stack_t one = item_stack_make(ITEM_ID_DIRT, 1u);
    item_stack_t hand = {0};
    item_stack_click_secondary(&one, &hand);
    CHECK(item_stack_is_empty(&one));
    CHECK_EQ_U64(hand.count, 1u);

    /* On a full stack, nothing goes down. */
    item_stack_t full = item_stack_make(ITEM_ID_DIRT, 64u);
    item_stack_click_secondary(&full, &hand);
    CHECK_EQ_U64(full.count, 64u);
    CHECK_EQ_U64(hand.count, 1u);

    item_stack_t coal = item_stack_make(ITEM_ID_COAL, 2u);
    item_stack_click_secondary(&coal, &hand);
    CHECK_EQ_U64(coal.item, ITEM_ID_DIRT);
    CHECK_EQ_U64(hand.item, ITEM_ID_COAL);
    CHECK_EQ_U64(hand.count, 2u);
}

/* Whatever is done, every stack stays a stack and no item comes or goes but by taking. */
TEST(at_random_every_stack_stays_a_stack_and_the_items_add_up) {
    uint64_t state = 0x1234567ull;
    item_stack_t stacks[II_RANDOM_STACKS];
    memset(stacks, 0, sizeof(stacks));
    item_stack_t cursor = {0};
    uint64_t *const made = test_alloc(t, ITEM_COUNT * sizeof(uint64_t));
    uint64_t *const seen = test_alloc(t, ITEM_COUNT * sizeof(uint64_t));
    memset(made, 0, ITEM_COUNT * sizeof(uint64_t));
    /* Only a few items, so that they do meet. */
    const uint16_t items[] = {ITEM_ID_DIRT, ITEM_ID_COAL, ITEM_ID_SNOWBALL, ITEM_ID_IRON_PICKAXE};
    for (uint32_t step = 0u; step < II_RANDOM_STEPS; step++) {
        item_stack_t *const a = &stacks[ii_random(&state) % II_RANDOM_STACKS];
        item_stack_t *const b = &stacks[ii_random(&state) % II_RANDOM_STACKS];
        const uint32_t amount = (uint32_t)(ii_random(&state) % 70u);
        const uint16_t item = items[ii_random(&state) % 4u];
        switch (ii_random(&state) % 6u) {
        case 0u:
            if (item_stack_is_empty(a)) {
                *a = item_stack_make(item, amount);
                made[a->item] += a->count;
            }
            break;
        case 1u:
            (void)item_stack_move(a, b, amount);
            break;
        case 2u: {
            const uint16_t was = a->item;
            made[was] -= item_stack_take(a, amount);
            break;
        }
        case 3u:
            item_stack_swap(a, b);
            break;
        case 4u:
            item_stack_click_primary(a, &cursor);
            break;
        default:
            item_stack_click_secondary(a, &cursor);
            break;
        }
        CHECK(item_stack_is_valid(a));
        CHECK(item_stack_is_valid(b));
        CHECK(item_stack_is_valid(&cursor));
    }
    memset(seen, 0, ITEM_COUNT * sizeof(uint64_t));
    for (uint32_t i = 0u; i < II_RANDOM_STACKS; i++) {
        seen[stacks[i].item] += stacks[i].count;
    }
    seen[cursor.item] += cursor.count;
    for (uint32_t i = 1u; i < ITEM_COUNT; i++) {
        CHECK_EQ_U64(seen[i], made[i]);
    }
}

/* ---- Where things go ---------------------------------------------------------------------------------------------- */

TEST(an_inventory_starts_empty_with_36_slots_and_the_first_in_hand) {
    inventory_t inventory;
    arena_t arena;
    ii_inventory(t, &inventory, &arena);
    CHECK_EQ_U64(INVENTORY_SLOTS, 36u);
    CHECK_EQ_U64(INVENTORY_HOTBAR_SLOTS, 9u);
    CHECK_EQ_U64(inventory.selected, 0u);
    CHECK_EQ_U64(inventory_items_held(&inventory), 0u);
    CHECK_EQ_U64(inventory_stacks_held(&inventory), 0u);
    CHECK(item_stack_is_empty(inventory_selected(&inventory)));
    ii_all_valid(t, &inventory);
    CHECK_EQ_U64(item_defs[inventory_selected(&inventory)->item].block, ITEM_NO_BLOCK);
}

TEST(new_things_go_to_the_first_empty_slot_and_the_hotbar_fills_first) {
    inventory_t inventory;
    arena_t arena;
    ii_inventory(t, &inventory, &arena);
    /* Slot 5 in hand and empty does not draw them. */
    inventory_select(&inventory, 5u);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_DIRT, 3u), 3u);
    CHECK_EQ_U64(inventory_slot(&inventory, 0u)->item, ITEM_ID_DIRT);
    CHECK_EQ_U64(inventory_slot(&inventory, 0u)->count, 3u);
    CHECK(item_stack_is_empty(inventory_selected(&inventory)));
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_COAL, 1u), 1u);
    CHECK_EQ_U64(inventory_slot(&inventory, 1u)->item, ITEM_ID_COAL);

    /* Tools, one to a slot: the rest of the hotbar, and then the rows. */
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_IRON_SWORD, 8u), 8u);
    for (uint32_t i = 2u; i < 10u; i++) {
        CHECK_EQ_U64(inventory_slot(&inventory, i)->item, ITEM_ID_IRON_SWORD);
        CHECK_EQ_U64(inventory_slot(&inventory, i)->count, 1u);
    }
    CHECK(item_stack_is_empty(inventory_slot(&inventory, 10u)));
    ii_all_valid(t, &inventory);
    ii_books_balance(t, &inventory);
}

TEST(the_same_item_goes_on_the_stack_in_hand_then_any_other_then_a_new_one) {
    inventory_t inventory;
    arena_t arena;
    ii_inventory(t, &inventory, &arena);
    *inventory_slot(&inventory, 2u) = item_stack_make(ITEM_ID_DIRT, 60u);
    *inventory_slot(&inventory, 20u) = item_stack_make(ITEM_ID_DIRT, 10u);
    *inventory_slot(&inventory, 4u) = item_stack_make(ITEM_ID_DIRT, 10u);
    inventory_select(&inventory, 4u);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_DIRT, 2u), 2u);
    CHECK_EQ_U64(inventory_slot(&inventory, 4u)->count, 12u);
    CHECK_EQ_U64(inventory_slot(&inventory, 2u)->count, 60u);
    /* It fills what is in hand, then slot 2, then slot 20, and still empty slot 0 gets the rest. */
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_DIRT, 52u + 4u + 54u + 3u), 113u);
    CHECK_EQ_U64(inventory_slot(&inventory, 4u)->count, 64u);
    CHECK_EQ_U64(inventory_slot(&inventory, 2u)->count, 64u);
    CHECK_EQ_U64(inventory_slot(&inventory, 20u)->count, 64u);
    CHECK_EQ_U64(inventory_slot(&inventory, 0u)->item, ITEM_ID_DIRT);
    CHECK_EQ_U64(inventory_slot(&inventory, 0u)->count, 3u);
    ii_all_valid(t, &inventory);
}

TEST(more_than_a_stack_spreads_and_what_has_no_room_stays_out) {
    inventory_t inventory;
    arena_t arena;
    ii_inventory(t, &inventory, &arena);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_DIRT, 100u), 100u);
    CHECK_EQ_U64(inventory_slot(&inventory, 0u)->count, 64u);
    CHECK_EQ_U64(inventory_slot(&inventory, 1u)->count, 36u);
    CHECK_EQ_U64(inventory_stacks_held(&inventory), 2u);

    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_DIRT, UINT32_MAX), II_FULL - 100u);
    CHECK_EQ_U64(inventory_items_held(&inventory), II_FULL);
    CHECK_EQ_U64(inventory_stacks_held(&inventory), INVENTORY_SLOTS);
    CHECK_EQ_U64(inventory.refused_count, (uint64_t)UINT32_MAX - (II_FULL - 100u));
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_DIRT, 1u), 0u);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_COAL, 1u), 0u);
    CHECK_EQ_U64(inventory_room(&inventory, ITEM_ID_COAL, 1u), 0u);
    ii_all_valid(t, &inventory);
    ii_books_balance(t, &inventory);

    /* One gone, and that one fits again, and only that. */
    CHECK_EQ_U64(inventory_remove(&inventory, ITEM_ID_DIRT, 1u), 1u);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_COAL, 1u), 0u);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_DIRT, 5u), 1u);
    ii_books_balance(t, &inventory);
}

TEST(what_is_no_item_goes_nowhere) {
    inventory_t inventory;
    arena_t arena;
    ii_inventory(t, &inventory, &arena);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_NONE, 5u), 0u);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_COUNT, 5u), 0u);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_DIRT, 0u), 0u);
    CHECK_EQ_U64(inventory_room(&inventory, ITEM_ID_NONE, 5u), 0u);
    CHECK_EQ_U64(inventory_stacks_held(&inventory), 0u);
    CHECK_EQ_U64(inventory.refused_count, 10u);
}

TEST(a_worn_tool_keeps_its_wear_and_a_full_inventory_leaves_it_be) {
    inventory_t inventory;
    arena_t arena;
    ii_inventory(t, &inventory, &arena);
    item_stack_t worn = {.item = ITEM_ID_IRON_PICKAXE, .count = 1u, .wear = 12u};
    CHECK_EQ_U64(inventory_add_stack(&inventory, &worn), 1u);
    CHECK(item_stack_is_empty(&worn));
    CHECK_EQ_U64(inventory_slot(&inventory, 0u)->wear, 12u);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_DIRT, II_FULL), II_FULL - 64u);
    item_stack_t other = {.item = ITEM_ID_IRON_PICKAXE, .count = 1u, .wear = 13u};
    CHECK_EQ_U64(inventory_add_stack(&inventory, &other), 0u);
    CHECK_EQ_U64(other.count, 1u);
    CHECK_EQ_U64(other.wear, 13u);
    ii_books_balance(t, &inventory);
}

TEST(every_item_a_block_drops_can_be_carried) {
    inventory_t inventory;
    arena_t arena;
    ii_inventory(t, &inventory, &arena);
    for (uint32_t i = 0u; i < BLOCK_DROP_ITEM_COUNT && i < INVENTORY_SLOTS; i++) {
        const uint16_t item = block_drop_items[i].item;
        CHECK_EQ_U64(inventory_add(&inventory, item, 1u), 1u);
        CHECK_EQ_U64(inventory_count(&inventory, item), 1u);
        CHECK_STR_EQ(item_defs[item].name, block_drop_items[i].name);
    }
}

TEST(counting_and_taking_away_from_the_last_slot_back) {
    inventory_t inventory;
    arena_t arena;
    ii_inventory(t, &inventory, &arena);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_DIRT, 64u), 64u);
    *inventory_slot(&inventory, 30u) = item_stack_make(ITEM_ID_DIRT, 5u);
    inventory.added_count += 5u; /* it was put there by hand, past the books */
    CHECK_EQ_U64(inventory_count(&inventory, ITEM_ID_DIRT), 69u);
    CHECK_EQ_U64(inventory_count(&inventory, ITEM_ID_COAL), 0u);
    CHECK_EQ_U64(inventory_remove(&inventory, ITEM_ID_COAL, 1u), 0u);
    CHECK_EQ_U64(inventory_remove(&inventory, ITEM_ID_DIRT, 3u), 3u);
    CHECK_EQ_U64(inventory_slot(&inventory, 30u)->count, 2u);
    CHECK_EQ_U64(inventory_slot(&inventory, 0u)->count, 64u);
    CHECK_EQ_U64(inventory_remove(&inventory, ITEM_ID_DIRT, 4u), 4u);
    CHECK(item_stack_is_empty(inventory_slot(&inventory, 30u)));
    CHECK_EQ_U64(inventory_slot(&inventory, 0u)->count, 62u);
    CHECK_EQ_U64(inventory_remove(&inventory, ITEM_ID_DIRT, 1000u), 62u);
    CHECK_EQ_U64(inventory_stacks_held(&inventory), 0u);
    ii_books_balance(t, &inventory);
}

/* ---- The hotbar --------------------------------------------------------------------------------------------------- */

TEST(the_hotbar_in_hand_by_number_and_by_scrolling_round_the_ends) {
    inventory_t inventory;
    arena_t arena;
    ii_inventory(t, &inventory, &arena);
    inventory_select(&inventory, 8u);
    CHECK_EQ_U64(inventory.selected, 8u);
    inventory_select(&inventory, 9u); /* the first of the rows, which is no hotbar slot */
    CHECK_EQ_U64(inventory.selected, 8u);
    inventory_select(&inventory, UINT32_MAX);
    CHECK_EQ_U64(inventory.selected, 8u);
    inventory_scroll(&inventory, 1);
    CHECK_EQ_U64(inventory.selected, 0u);
    inventory_scroll(&inventory, -1);
    CHECK_EQ_U64(inventory.selected, 8u);
    inventory_scroll(&inventory, -18);
    CHECK_EQ_U64(inventory.selected, 8u);
    inventory_scroll(&inventory, 3);
    CHECK_EQ_U64(inventory.selected, 2u);
    inventory_scroll(&inventory, INT32_MIN);
    CHECK(inventory.selected < INVENTORY_HOTBAR_SLOTS);
    inventory_scroll(&inventory, INT32_MAX);
    CHECK(inventory.selected < INVENTORY_HOTBAR_SLOTS);
}

TEST(what_is_in_hand_is_what_is_taken) {
    inventory_t inventory;
    arena_t arena;
    ii_inventory(t, &inventory, &arena);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_DIRT, 2u), 2u);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_COAL, 2u), 2u);
    inventory_select(&inventory, 1u);
    CHECK_EQ_U64(inventory_selected(&inventory)->item, ITEM_ID_COAL);
    CHECK_EQ_U64(inventory_selected_take(&inventory, 1u), 1u);
    CHECK_EQ_U64(inventory_selected(&inventory)->count, 1u);
    CHECK_EQ_U64(inventory_slot(&inventory, 0u)->count, 2u);
    CHECK_EQ_U64(inventory_selected_take(&inventory, 5u), 1u);
    CHECK(item_stack_is_empty(inventory_selected(&inventory)));
    CHECK_EQ_U64(inventory_selected_take(&inventory, 5u), 0u);
    ii_books_balance(t, &inventory);
}

TEST(swapping_and_a_shift_click_across_the_hotbar_line) {
    inventory_t inventory;
    arena_t arena;
    ii_inventory(t, &inventory, &arena);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_DIRT, 5u), 5u);
    inventory_swap(&inventory, 0u, 35u);
    CHECK(item_stack_is_empty(inventory_slot(&inventory, 0u)));
    CHECK_EQ_U64(inventory_slot(&inventory, 35u)->count, 5u);
    inventory_swap(&inventory, 35u, INVENTORY_SLOTS);
    inventory_swap(&inventory, INVENTORY_SLOTS, 35u);
    CHECK_EQ_U64(inventory_slot(&inventory, 35u)->count, 5u);

    /* From the rows to the first empty on the hotbar. */
    CHECK_EQ_U64(inventory_quick_move(&inventory, 35u), 5u);
    CHECK_EQ_U64(inventory_slot(&inventory, 0u)->count, 5u);
    CHECK(item_stack_is_empty(inventory_slot(&inventory, 35u)));
    /* And back, to the first of the rows. */
    CHECK_EQ_U64(inventory_quick_move(&inventory, 0u), 5u);
    CHECK_EQ_U64(inventory_slot(&inventory, 9u)->count, 5u);
    /* On to a stack of it first, and the rest to an empty slot. */
    *inventory_slot(&inventory, 3u) = item_stack_make(ITEM_ID_DIRT, 62u);
    CHECK_EQ_U64(inventory_quick_move(&inventory, 9u), 5u);
    CHECK_EQ_U64(inventory_slot(&inventory, 3u)->count, 64u);
    CHECK_EQ_U64(inventory_slot(&inventory, 0u)->count, 3u);
    CHECK_EQ_U64(inventory_quick_move(&inventory, INVENTORY_SLOTS), 0u);
    /* Nothing to move. */
    CHECK_EQ_U64(inventory_quick_move(&inventory, 20u), 0u);

    /* A full hotbar takes nothing from the rows. */
    for (uint32_t i = 0u; i < INVENTORY_HOTBAR_SLOTS; i++) {
        *inventory_slot(&inventory, i) = item_stack_make(ITEM_ID_IRON_SWORD, 1u);
    }
    *inventory_slot(&inventory, 12u) = item_stack_make(ITEM_ID_DIRT, 1u);
    CHECK_EQ_U64(inventory_quick_move(&inventory, 12u), 0u);
    CHECK_EQ_U64(inventory_slot(&inventory, 12u)->count, 1u);
    ii_all_valid(t, &inventory);
}

/* Room is what adding says it is, and nothing changes what is held but adding and taking. */
TEST(at_random_room_is_what_adding_finds_and_the_books_balance) {
    inventory_t inventory;
    arena_t arena;
    ii_inventory(t, &inventory, &arena);
    uint64_t state = 0xFEEDFACEull;
    for (uint32_t step = 0u; step < II_RANDOM_STEPS; step++) {
        const uint16_t item = ii_random_item(&state);
        const uint32_t amount = (uint32_t)(ii_random(&state) % 200u);
        const uint32_t slot = (uint32_t)(ii_random(&state) % INVENTORY_SLOTS);
        const uint64_t held = inventory_items_held(&inventory);
        switch (ii_random(&state) % 6u) {
        case 0u:
        case 1u: {
            const uint32_t room = inventory_room(&inventory, item, amount);
            CHECK_EQ_U64(inventory_add(&inventory, item, amount), room);
            CHECK_EQ_U64(inventory_items_held(&inventory), held + room);
            break;
        }
        case 2u: {
            const uint32_t there = inventory_count(&inventory, item);
            const uint32_t went = inventory_remove(&inventory, item, amount);
            CHECK_EQ_U64(went, amount < there ? amount : there);
            CHECK_EQ_U64(inventory_count(&inventory, item), there - went);
            break;
        }
        case 3u:
            inventory_swap(&inventory, slot, (uint32_t)(ii_random(&state) % INVENTORY_SLOTS));
            CHECK_EQ_U64(inventory_items_held(&inventory), held);
            break;
        case 4u:
            (void)inventory_quick_move(&inventory, slot);
            CHECK_EQ_U64(inventory_items_held(&inventory), held);
            break;
        default:
            inventory_select(&inventory, slot);
            (void)inventory_selected_take(&inventory, amount % 3u);
            break;
        }
        if (step % 1024u == 0u) {
            ii_all_valid(t, &inventory);
        }
        ii_books_balance(t, &inventory);
    }
    ii_all_valid(t, &inventory);
}

/* ---- The Owner ---------------------------------------------------------------------------------------------------- */

TEST(the_owner_gives_back_what_it_took_and_a_world_starts_empty) {
    item_t *const item = test_alloc(t, sizeof(item_t) + 64u);
    item_t *const aligned = (item_t *)(((uintptr_t)item + 63u) & ~(uintptr_t)63u);
    heap_counters_t before, after;
    heap_counters_read(&before);
    REQUIRE(item_init(aligned));
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_regions, before.live_regions + 1u);
    for (uint32_t i = 0u; i < 3u; i++) {
        item_open(aligned);
        CHECK_EQ_U64(inventory_items_held(&aligned->inventory), 0u);
        CHECK_EQ_U64(aligned->inventory.selected, 0u);
        CHECK_EQ_U64(aligned->inventory.added_count, 0u);
        CHECK_EQ_U64(inventory_add(&aligned->inventory, ITEM_ID_DIRT, 70u), 70u);
        inventory_select(&aligned->inventory, 4u);
        item_close(aligned);
        CHECK(aligned->inventory.borrowed_slots == nullptr);
    }
    item_close(aligned); /* none open */
    item_deinit(aligned);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    /* Zero bytes mean not running. */
    item_deinit(aligned);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
}

TEST(nothing_touches_the_heap_while_playing) {
    item_t *const item = test_alloc(t, sizeof(item_t) + 64u);
    item_t *const aligned = (item_t *)(((uintptr_t)item + 63u) & ~(uintptr_t)63u);
    REQUIRE(item_init(aligned));
    item_open(aligned);
    heap_counters_t before, after;
    uint64_t state = 99u;
    heap_counters_read(&before);
    for (uint32_t step = 0u; step < 10000u; step++) {
        (void)inventory_add(&aligned->inventory, ii_random_item(&state), 3u);
        (void)inventory_remove(&aligned->inventory, ii_random_item(&state), 3u);
        item_hotbar_send(aligned, step % 12u);
        item_hotbar_apply(aligned);
    }
    heap_counters_read(&after);
    CHECK_MEM_EQ(&before, &after, sizeof(before));
    item_close(aligned);
    item_deinit(aligned);
}

TEST(a_key_then_a_wheel_is_taken_once_and_past_the_hotbar_is_nothing) {
    item_t *const item = test_alloc(t, sizeof(item_t) + 64u);
    item_t *const aligned = (item_t *)(((uintptr_t)item + 63u) & ~(uintptr_t)63u);
    REQUIRE(item_init(aligned));
    item_open(aligned);
    item_hotbar_send(aligned, 4u);
    CHECK_EQ_U64(aligned->inventory.selected, 0u); /* not until the sim looks */
    item_hotbar_apply(aligned);
    CHECK_EQ_U64(aligned->inventory.selected, 4u);
    item_hotbar_apply(aligned);
    CHECK_EQ_U64(aligned->inventory.selected, 4u);
    item_hotbar_send(aligned, 9u);
    item_hotbar_apply(aligned);
    CHECK_EQ_U64(aligned->inventory.selected, 4u);

    item_hotbar_send(aligned, 2u);
    item_hotbar_scroll_send(aligned, 1);
    item_hotbar_scroll_send(aligned, 1);
    item_hotbar_apply(aligned);
    CHECK_EQ_U64(aligned->inventory.selected, 4u);
    item_hotbar_apply(aligned);
    CHECK_EQ_U64(aligned->inventory.selected, 4u);
    item_hotbar_scroll_send(aligned, -5);
    item_hotbar_apply(aligned);
    CHECK_EQ_U64(aligned->inventory.selected, 8u);
    /* A new world forgets what was asked. */
    item_hotbar_send(aligned, 3u);
    item_hotbar_scroll_send(aligned, 2);
    item_close(aligned);
    item_open(aligned);
    item_hotbar_apply(aligned);
    CHECK_EQ_U64(aligned->inventory.selected, 0u);
    item_close(aligned);
    item_deinit(aligned);
}

static void *ii_scroller(void *borrowed_item) {
    for (uint32_t i = 0u; i < II_THREAD_SCROLLS; i++) {
        item_hotbar_scroll_send(borrowed_item, 1);
    }
    return nullptr;
}

TEST(a_wheel_on_another_thread_loses_nothing) {
    item_t *const item = test_alloc(t, sizeof(item_t) + 64u);
    item_t *const aligned = (item_t *)(((uintptr_t)item + 63u) & ~(uintptr_t)63u);
    REQUIRE(item_init(aligned));
    item_open(aligned);
    pthread_t thread;
    REQUIRE(pthread_create(&thread, nullptr, ii_scroller, aligned) == 0);
    for (uint32_t i = 0u; i < 100000u; i++) {
        item_hotbar_apply(aligned);
    }
    REQUIRE(pthread_join(thread, nullptr) == 0);
    item_hotbar_apply(aligned);
    CHECK_EQ_U64(aligned->inventory.selected, II_THREAD_SCROLLS % INVENTORY_HOTBAR_SLOTS);
    item_close(aligned);
    item_deinit(aligned);
}

/* ---- The program -------------------------------------------------------------------------------------------------- */

typedef struct ii_temp_dir_t {
    char path[64]; /* 0 */
} ii_temp_dir_t;
static_assert(sizeof(ii_temp_dir_t) == 64);

static bool ii_temp_dir_make(ii_temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_item_inventory_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void ii_temp_dir_remove(const ii_temp_dir_t *dir) {
    DIR *const handle = opendir(dir->path);
    if (handle != nullptr) {
        char path[512];
        for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
            if (entry->d_name[0] != '.') {
                snprintf(path, sizeof(path), "%s/%s", dir->path, entry->d_name);
                unlink(path);
            }
        }
        closedir(handle);
    }
    rmdir(dir->path);
}

static void ii_program_run(test_context_t *t, const ii_temp_dir_t *dir, const char *text, test_process_t *out) {
    char script[128];
    snprintf(script, sizeof(script), "%s/hotbar.script", dir->path);
    CHECK_EQ_U64(file_write_atomic(script, text, strlen(text)), FILE_OK);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--test-world",
                          (char *)"--walk",
                          (char *)"--render-distance",
                          (char *)"3",
                          (char *)"--script",
                          script,
                          (char *)"--screenshot-dir",
                          (char *)dir->path,
                          (char *)"--width",
                          (char *)"160",
                          (char *)"--height",
                          (char *)"90",
                          nullptr};
    test_process_run(t, argv, II_PROGRAM_TIMEOUT_MS, out);
}

/* The player looks down, and chooses the third slot before mining the grass under its feet, which takes 18 ticks by hand. It falls into
 * the hole with the dirt, which it can pick up 10 ticks after it came out, and that goes to the first empty slot and not to the third. */
#define II_SCRIPT_MINE                                                                                                 \
    "1 move 0 500\n"                                                                                                   \
    "2 key 3 down\n"                                                                                                   \
    "3 key 3 up\n"                                                                                                     \
    "6 button left down\n"                                                                                             \
    "26 button left up\n"
/* Jumps and holds the right button, so that with its feet clear of the hole it places what is in hand. */
#define II_SCRIPT_JUMP                                                                                                 \
    "46 key space down\n"                                                                                              \
    "46 button right down\n"                                                                                           \
    "65 key space up\n"                                                                                                \
    "65 button right up\n"

/* The line the program ends with if the body stands below where it started, as a world's own ground decides where that is. */
static void ii_player_is_below_start(test_context_t *t, const char *output, float64_t below) {
    static const char start[] = "the player starts at ";
    const char *const at = strstr(output, start);
    REQUIRE(at != nullptr);
    char *rest = nullptr;
    const float64_t x = strtod(at + sizeof(start) - 1u, &rest);
    REQUIRE(rest[0] == ',');
    const float64_t y = strtod(rest + 1, &rest);
    REQUIRE(rest[0] == ',');
    const float64_t z = strtod(rest + 1, &rest);
    char line[128];
    snprintf(line, sizeof(line), "the player is at %.3f, %.3f, %.3f", (libc_double_t)x, (libc_double_t)y,
             (libc_double_t)(z - below));
    CHECK_CONTAINS(output, line);
}

TEST(the_program_chooses_a_slot_picks_up_and_places_what_is_in_hand) {
    ii_temp_dir_t dir;
    REQUIRE(ii_temp_dir_make(&dir));
    test_process_t run;

    /* The dirt came to the first slot, and the third is in hand. */
    ii_program_run(t, &dir, II_SCRIPT_MINE "44 end\n", &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, "1 items dropped, 1 picked up, 0 on the ground in 0 stacks");
    CHECK_CONTAINS(run.output,
                   "the inventory holds 1 items in 1 stacks; 1 added, 0 removed, 0 refused; hotbar slot 3 is "
                   "in hand, with 0 of none");
    CHECK_CONTAINS(run.output, "the last mined was grass_block, and the hand holds 0 of nothing");

    /* So the right button places nothing: it is still carried, and the player comes down in the hole. */
    ii_program_run(t, &dir, II_SCRIPT_MINE II_SCRIPT_JUMP "95 end\n", &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, "the player mined 1 blocks and placed 0");
    CHECK_CONTAINS(run.output,
                   "the inventory holds 1 items in 1 stacks; 1 added, 0 removed, 0 refused; hotbar slot 3 is "
                   "in hand, with 0 of none");
    ii_player_is_below_start(t, run.output, 1.0f64);

    /* Key 1 first, and it is in hand, and goes back where it came from. */
    ii_program_run(t, &dir, II_SCRIPT_MINE "44 key 1 down\n45 key 1 up\n" II_SCRIPT_JUMP "95 end\n", &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output,
                   "the player mined 1 blocks and placed 1; the last mined was grass_block, and the hand holds "
                   "0 of nothing");
    CHECK_CONTAINS(run.output,
                   "the inventory holds 0 items in 0 stacks; 1 added, 1 removed, 0 refused; hotbar slot 1 is "
                   "in hand, with 0 of none");
    ii_player_is_below_start(t, run.output, 0.0f64);
    CHECK_CONTAINS(run.output, "0 bytes live");
    ii_temp_dir_remove(&dir);
}

TEST_MAIN()
