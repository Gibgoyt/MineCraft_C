/* See run/src/mob/combat/README.md. */
#ifndef MC_RUN_MOB_COMBAT_INDEX_H
#define MC_RUN_MOB_COMBAT_INDEX_H

#include "arena/index.h"
#include "entity/store/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* The published numbers, in half hearts, ticks at 20 Hz and blocks per tick. README.md has them in a table. */
enum : uint32_t {
    MOB_COMBAT_INVULNERABLE_TICKS = 10u,
};
#define MOB_COMBAT_KNOCKBACK 0.4f64
#define MOB_COMBAT_KNOCKBACK_UP_MAX 0.4f64
#define MOB_COMBAT_LAVA_DAMAGE 4.0f32

/* What one body remembers of being hurt. Indexed by the slot of its handle, and good only while owner is its handle. */
typedef struct mob_combat_body_t {
    float32_t last_hurt;   /* 0: the size of the hit it is invulnerable after */
    entity_handle_t owner; /* 4 */
    uint8_t invulnerable;  /* 8: ticks left */
    uint8_t _pad0[3];      /* 9 */
} mob_combat_body_t;
static_assert(sizeof(mob_combat_body_t) == 12);

/* What every body remembers of being hurt. Plain numbers, so that the same world and the same calls give the same bytes. The sim's. */
typedef struct mob_combat_t {
    mob_combat_body_t *borrowed_bodies; /* 0: ENTITY_STORE_CAPACITY, by a handle's slot */
    uint64_t landed_count;              /* 8: hits that took effect */
    uint64_t killed_count;              /* 16: bodies those took the last health of */
    uint64_t knocked_count;             /* 24: pushes */
} mob_combat_t;
static_assert(sizeof(mob_combat_t) == 32);

/* What a start takes from its arena. */
enum : uint64_t {
    MOB_COMBAT_BYTES = (uint64_t)ENTITY_STORE_CAPACITY * sizeof(mob_combat_body_t) + 64ull,
};

/* What a hit came to. */
typedef struct mob_combat_outcome_t {
    float32_t taken;  /* 0: the health it took */
    uint8_t landed;   /* 4: it took effect: a whole hit, or what it was more than the last by */
    uint8_t fresh;    /* 5: a whole hit, which starts the time the body cannot be hurt, and pushes it */
    uint8_t killed;   /* 6: it took the last of its health */
    uint8_t _pad0[1]; /* 7 */
} mob_combat_outcome_t;
static_assert(sizeof(mob_combat_outcome_t) == 8);

/* A blow from something standing somewhere. Plain numbers: a mob does not know what a player is. */
typedef struct mob_combat_blow_t {
    float64_t from[2]; /* 0: where the striker stands, x and y: the body goes away from there */
    float64_t
        facing[2]; /* 16: which way it faces, a unit vector in x and y: where a blow from on top of it sends a body */
    float32_t damage;    /* 32 */
    float32_t knockback; /* 36: more push along facing, if the hit took effect; 0 for none */
} mob_combat_blow_t;
static_assert(sizeof(mob_combat_blow_t) == 40);

/* Nobody has been hurt yet, from the arena, which lasts as long as the world. */
void mob_combat_start(mob_combat_t *combat, arena_t *arena);

/* A hit on the body at this index. Within MOB_COMBAT_INVULNERABLE_TICKS of the last whole hit only what is more than that one counts, and
 * only the difference. One already at no health feels nothing. Health stops at 0, and whichever mind the body has takes it away, on its next
 * tick, with what it leaves. */
mob_combat_outcome_t mob_combat_hurt(mob_combat_t *combat, entity_store_t *store, uint32_t index, float32_t amount);

/* The published push: half of what it was moving at across, and this much away along (away_x, away_y), which need not be unit length, and
 * up from the ground. (0, 0) only slows it. */
void mob_combat_knockback(mob_combat_t *combat, entity_store_t *store, uint32_t index, float64_t strength,
                          float64_t away_x, float64_t away_y);

/* A blow on this body: the hit, its push if the hit was whole, and blow's own push if it took effect at all. False, with outcome all
 * zero, if the body is gone or the hit did nothing. */
bool mob_combat_strike(mob_combat_t *combat, entity_store_t *store, entity_handle_t handle,
                       const mob_combat_blow_t *blow, mob_combat_outcome_t *out);

/* One tick of every body, after they have moved: the time they cannot be hurt runs down, and a fall that has just ended or lava they stand
 * in hurts them. A body waiting for its column does nothing, and no time passes for it. Never waits and never touches the heap. */
void mob_combat_tick(mob_combat_t *combat, entity_store_t *store);

#endif
