/* See run/src/item/README.md. */
#ifndef MC_RUN_ITEM_INDEX_H
#define MC_RUN_ITEM_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "item/inventory/index.h"
#include "item/stack/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* What a player has. There is one, under app_t. Zero bytes mean not running. */
typedef struct item_t {
    arena_t arena_world;        /* 0: over owned_region; World: reset at each item_open and item_close */
    heap_region_t owned_region; /* 64 */
    inventory_t inventory;      /* 80: the sim's */
    uint8_t _pad0[8];           /* 120 */
    /* A line of its own: main writes these when a key goes down, and the sim reads them each tick. */
    _Atomic uint32_t hotbar_key;  /* 128: main to sim: the slot asked for, plus one, and 0 for none */
    _Atomic int32_t hotbar_wheel; /* 132: main to sim: slots to the right, not yet taken */
    uint8_t _pad1[56];            /* 136 */
} item_t;
static_assert(sizeof(item_t) == 192);
static_assert(alignof(item_t) == 64);
static_assert(offsetof(item_t, inventory) == 80);
static_assert(offsetof(item_t, hotbar_key) == 128);

/* Reserves the region. On failure logs why, leaves nothing behind and returns false. */
[[nodiscard]] bool item_init(item_t *item);

/* Gives it back. Safe on a zeroed item_t. */
void item_deinit(item_t *item);

/* Nothing carried yet, with the first hotbar slot in hand. Before the threads start. */
void item_open(item_t *item);

/* What was carried is gone: nothing is saved until Stage 16.2. The threads must have stopped. Safe if none is open. */
void item_close(item_t *item);

/* main asks for a hotbar slot, or to scroll by some slots. Neither waits. Any thread, one at a time. A slot past the hotbar is ignored. */
void item_hotbar_send(item_t *item, uint32_t slot);
void item_hotbar_scroll_send(item_t *item, int32_t slots);

/* The sim takes what has been asked for since it last looked: the slot, and then the scroll on from there. */
void item_hotbar_apply(item_t *item);

#endif
