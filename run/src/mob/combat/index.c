/* See README.md. The Arena tier by its depth: it takes one array from the arena it is given, once, and never frees. */
#include "mob/combat/index.h"

#include "player/survival/index.h"

#include <math.h>
#include <string.h>

void mob_combat_start(mob_combat_t *combat, arena_t *arena) {
    memset(combat, 0, sizeof(*combat));
    combat->borrowed_bodies =
        arena_alloc_zeroed(arena, (uint64_t)ENTITY_STORE_CAPACITY * sizeof(mob_combat_body_t), 64u);
}

/* A slot used again starts over: this one has never been hurt. */
static mob_combat_body_t *mob_combat_body_of(mob_combat_t *combat, const entity_store_t *store, uint32_t index) {
    const entity_handle_t handle = store->borrowed_handle[index];
    mob_combat_body_t *const body = &combat->borrowed_bodies[entity_handle_slot(handle)];
    if (body->owner != handle) {
        *body = (mob_combat_body_t){.owner = handle};
    }
    return body;
}

mob_combat_outcome_t mob_combat_hurt(mob_combat_t *combat, entity_store_t *store, uint32_t index, float32_t amount) {
    mob_combat_outcome_t outcome = {0};
    const float32_t health = store->borrowed_health[index];
    if (!(amount > 0.0f32) || !(health > 0.0f32)) {
        return outcome;
    }
    mob_combat_body_t *const body = mob_combat_body_of(combat, store, index);
    float32_t counts = amount;
    if (body->invulnerable != 0u) {
        if (amount <= body->last_hurt) {
            return outcome;
        }
        counts = amount - body->last_hurt;
    } else {
        body->invulnerable = (uint8_t)MOB_COMBAT_INVULNERABLE_TICKS;
        outcome.fresh = 1u;
    }
    body->last_hurt = amount;
    outcome.landed = 1u;
    outcome.taken = counts < health ? counts : health;
    store->borrowed_health[index] = health - outcome.taken;
    combat->landed_count++;
    if (store->borrowed_health[index] <= 0.0f32) {
        store->borrowed_health[index] = 0.0f32;
        outcome.killed = 1u;
        combat->killed_count++;
    }
    return outcome;
}

void mob_combat_knockback(mob_combat_t *combat, entity_store_t *store, uint32_t index, float64_t strength,
                          float64_t away_x, float64_t away_y) {
    if (!(strength > 0.0f64)) {
        return;
    }
    const float64_t length = hypot(away_x, away_y);
    const float64_t scale = length > 0.0f64 ? strength / length : 0.0f64;
    store->borrowed_velocity[0][index] = store->borrowed_velocity[0][index] * 0.5f64 + away_x * scale;
    store->borrowed_velocity[1][index] = store->borrowed_velocity[1][index] * 0.5f64 + away_y * scale;
    if ((store->borrowed_flags[index] & ENTITY_STORE_FLAG_ON_GROUND) != 0u) {
        const float64_t up = store->borrowed_velocity[2][index] * 0.5f64 + strength;
        store->borrowed_velocity[2][index] = up < MOB_COMBAT_KNOCKBACK_UP_MAX ? up : MOB_COMBAT_KNOCKBACK_UP_MAX;
    }
    combat->knocked_count++;
}

bool mob_combat_strike(mob_combat_t *combat, entity_store_t *store, entity_handle_t handle,
                       const mob_combat_blow_t *blow, mob_combat_outcome_t *out) {
    *out = (mob_combat_outcome_t){0};
    uint32_t index = 0u;
    if (!entity_store_index_of(store, handle, &index)) {
        return false;
    }
    *out = mob_combat_hurt(combat, store, index, blow->damage);
    if (out->landed == 0u) {
        return false;
    }
    if (out->fresh != 0u) {
        float64_t away_x = store->borrowed_position[0][index] - blow->from[0];
        float64_t away_y = store->borrowed_position[1][index] - blow->from[1];
        /* Exactly on top of it there is no away, and the blow's own direction will have to do. */
        if (away_x == 0.0f64 && away_y == 0.0f64) {
            away_x = blow->facing[0];
            away_y = blow->facing[1];
        }
        mob_combat_knockback(combat, store, index, MOB_COMBAT_KNOCKBACK, away_x, away_y);
    }
    mob_combat_knockback(combat, store, index, (float64_t)blow->knockback, blow->facing[0], blow->facing[1]);
    return true;
}

void mob_combat_tick(mob_combat_t *combat, entity_store_t *store) {
    for (uint32_t i = 0u; i < store->count; i++) {
        if ((store->borrowed_flags[i] & ENTITY_STORE_FLAG_WAITING) != 0u) {
            continue;
        }
        mob_combat_body_t *const body = mob_combat_body_of(combat, store, i);
        body->invulnerable = body->invulnerable != 0u ? (uint8_t)(body->invulnerable - 1u) : 0u;
        /* As a player's: the same three blocks are safe, and the same sum after that. */
        (void)mob_combat_hurt(combat, store, i, player_survival_fall_damage(store->borrowed_landed_fall[i], false));
        if ((store->borrowed_flags[i] & ENTITY_STORE_FLAG_IN_LAVA) != 0u) {
            (void)mob_combat_hurt(combat, store, i, MOB_COMBAT_LAVA_DAMAGE);
        }
    }
}
