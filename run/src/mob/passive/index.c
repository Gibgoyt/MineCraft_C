/* See README.md. The Arena tier by its depth: it takes one array from the arena it is given, and never frees. */
#include "mob/passive/index.h"

#include "item/stack/index.h"

#include <math.h>
#include <string.h>

/* What it leaves starts this far above its feet, and clear of the floor they stand on. */
#define MOB_PASSIVE_DROP_ABOVE 0.5f64

void mob_passive_start(mob_passive_t *passive, arena_t *arena, uint64_t seed) {
    memset(passive, 0, sizeof(*passive));
    passive->borrowed_bodies =
        arena_alloc_zeroed(arena, (uint64_t)ENTITY_STORE_CAPACITY * sizeof(mob_passive_body_t), 64u);
    passive->borrowed_tally = arena_alloc_zeroed(arena, sizeof(mob_passive_tally_t), 64u);
    passive->borrowed_tally->random = terrain_rng_start(seed);
}

mob_passive_body_t *mob_passive_body_of(mob_passive_t *passive, const entity_store_t *store, uint32_t index) {
    const entity_handle_t handle = store->borrowed_handle[index];
    mob_passive_body_t *const body = &passive->borrowed_bodies[entity_handle_slot(handle)];
    if (body->owner != handle) {
        /* A slot used again starts over: this one has never been hit, and has every yield ready. */
        memset(body, 0, sizeof(*body));
        body->owner = handle;
        body->last_health = store->borrowed_health[index];
    }
    return body;
}

const mob_sex_t *mob_passive_sex_of(uint16_t kind, uint8_t sex) {
    if ((uint32_t)kind >= MOB_COUNT || mob_defs[kind].first_sex == 0u ||
        (sex != ENTITY_STORE_SEX_MALE && sex != ENTITY_STORE_SEX_FEMALE)) {
        return nullptr;
    }
    return &mob_sexes[mob_defs[kind].first_sex + (uint32_t)sex - (uint32_t)ENTITY_STORE_SEX_MALE];
}

/* Each row rolled on its own, as config/mobs/README.md says. */
static void mp_roll(mob_passive_t *passive, item_drop_store_t *drops, uint32_t first, uint32_t count,
                    const float64_t at[COLLISION_AXES]) {
    for (uint32_t row = 0u; row < count; row++) {
        const mob_drop_t *const drop = &mob_drops[first + row];
        const bool lucky = terrain_rng_next_f32(&passive->borrowed_tally->random) < drop->chance;
        const uint32_t many =
            drop->count_min +
            terrain_rng_next_below(&passive->borrowed_tally->random, (uint32_t)drop->count_max - drop->count_min + 1u);
        if (lucky && many != 0u) {
            item_drop_spawn(drops, item_stack_drop_row(drop->item), many, at);
            passive->borrowed_tally->dropped_count += many;
        }
    }
}

/* Its kind's drops, then its sex's. */
static void mp_die(mob_passive_t *passive, entity_store_t *store, item_drop_store_t *drops, uint32_t index) {
    const uint16_t kind = store->borrowed_kind[index];
    const mob_def_t *const def = &mob_defs[kind];
    const float64_t at[COLLISION_AXES] = {store->borrowed_position[0][index], store->borrowed_position[1][index],
                                          store->borrowed_position[2][index] + MOB_PASSIVE_DROP_ABOVE};
    mp_roll(passive, drops, def->first_drop, def->drop_count, at);
    const mob_sex_t *const sex = mob_passive_sex_of(kind, store->borrowed_sex[index]);
    if (sex != nullptr) {
        mp_roll(passive, drops, sex->first_drop, sex->drop_count, at);
    }
    passive->borrowed_tally->died_count++;
    (void)entity_store_remove(store, store->borrowed_handle[index]);
}

/* Straight away from where the blow came from, faster than it walks, hopping what it runs into. Its stroll is over. */
static void mp_flee(entity_store_t *store, uint32_t index, mob_passive_body_t *body) {
    body->flee--;
    body->stroll = 0u;
    if (body->flee == 0u) {
        entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        return;
    }
    float64_t away_x = store->borrowed_position[0][index] - body->from[0];
    float64_t away_y = store->borrowed_position[1][index] - body->from[1];
    const float64_t length = hypot(away_x, away_y);
    if (length < 1.0e-6f64) {
        /* Struck from exactly where it stands: it runs the way it faces. */
        away_x = cos((float64_t)store->borrowed_yaw[index]);
        away_y = sin((float64_t)store->borrowed_yaw[index]);
    } else {
        away_x /= length;
        away_y /= length;
    }
    const float64_t speed = (float64_t)mob_defs[store->borrowed_kind[index]].speed * MOB_PASSIVE_FLEE_SPEED;
    entity_store_wish_set(store, index, away_x * speed, away_y * speed);
    store->borrowed_yaw[index] = (float32_t)atan2(away_y, away_x);
    if ((store->borrowed_flags[index] & ENTITY_STORE_FLAG_HIT_WALL) != 0u) {
        entity_store_jump(store, index);
    }
}

/* Where the others of its kind about it stand, on average. False if none is within MOB_PASSIVE_HERD_RANGE. */
static bool mp_herd_middle(const entity_store_t *store, uint32_t index, float64_t out[COLLISION_AXES]) {
    const uint16_t kind = store->borrowed_kind[index];
    const float64_t range = (float64_t)MOB_PASSIVE_HERD_RANGE;
    float64_t sum[COLLISION_AXES] = {0.0f64, 0.0f64, 0.0f64};
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < store->count; i++) {
        if (i == index || store->borrowed_kind[i] != kind) {
            continue;
        }
        const float64_t dx = store->borrowed_position[0][i] - store->borrowed_position[0][index];
        const float64_t dy = store->borrowed_position[1][i] - store->borrowed_position[1][index];
        if (fabs(dx) > range || fabs(dy) > range) {
            continue;
        }
        for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
            sum[a] += store->borrowed_position[a][i];
        }
        count++;
    }
    if (count == 0u) {
        return false;
    }
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        out[a] = sum[a] / (float64_t)count;
    }
    return true;
}

static float64_t mp_spread(mob_passive_t *passive, uint32_t most) {
    return (float64_t)terrain_rng_next_below(&passive->borrowed_tally->random, 2u * most + 1u) - (float64_t)most;
}

/* At rest: it stands, and now and then sets off for somewhere near by the same paths a zombie walks, back towards its herd if it has
 * strayed from it. */
static void mp_stroll(mob_passive_t *passive, mob_path_t *path, entity_store_t *store, uint32_t index,
                      mob_passive_body_t *body) {
    const entity_handle_t handle = store->borrowed_handle[index];
    if (body->stroll != 0u) {
        body->stroll--;
        const mob_path_follow_t follow =
            mob_path_follow(path, store, handle, (float64_t)mob_defs[store->borrowed_kind[index]].speed);
        /* None is an answer on its way. Any other but walking is the end of this one, and the wish is already 0. */
        if (follow == MOB_PATH_FOLLOW_NONE) {
            entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        } else if (follow != MOB_PATH_FOLLOW_WALKING) {
            body->stroll = 0u;
        }
        if (body->stroll == 0u) {
            entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        }
        return;
    }
    if (terrain_rng_next_below(&passive->borrowed_tally->random, MOB_PASSIVE_STROLL_ONE_IN) != 0u) {
        return;
    }
    float64_t goal[COLLISION_AXES] = {store->borrowed_position[0][index], store->borrowed_position[1][index],
                                      store->borrowed_position[2][index]};
    float64_t middle[COLLISION_AXES];
    const float64_t far = (float64_t)MOB_PASSIVE_HERD_FAR;
    if (mp_herd_middle(store, index, middle) && (fabs(middle[0] - goal[0]) > far || fabs(middle[1] - goal[1]) > far)) {
        goal[0] = middle[0] + mp_spread(passive, MOB_PASSIVE_HERD_SPREAD);
        goal[1] = middle[1] + mp_spread(passive, MOB_PASSIVE_HERD_SPREAD);
        goal[2] = middle[2];
    } else {
        goal[0] += mp_spread(passive, MOB_PASSIVE_STROLL_ACROSS);
        goal[1] += mp_spread(passive, MOB_PASSIVE_STROLL_ACROSS);
        goal[2] += mp_spread(passive, MOB_PASSIVE_STROLL_UP);
    }
    mob_path_ask_t ask;
    mob_path_ask_for_body(store, index, goal, 0u, &ask);
    ask.nodes = (uint16_t)MOB_PASSIVE_STROLL_NODES;
    mob_path_request(path, handle, &ask);
    body->stroll = (uint16_t)MOB_PASSIVE_STROLL_TICKS;
}

void mob_passive_tick(mob_passive_t *passive, mob_path_t *path, entity_store_t *store, item_drop_store_t *drops,
                      const mob_zombie_view_t *view) {
    /* From the end, as a death moves the last body into its place, and that one has had its turn. */
    for (uint32_t i = store->count; i-- > 0u;) {
        const uint16_t kind = store->borrowed_kind[i];
        if (kind == (uint16_t)MOB_ID_NONE || (uint32_t)kind >= MOB_COUNT || mob_defs[kind].mind != MOB_MIND_PASSIVE) {
            continue;
        }
        mob_passive_body_t *const body = mob_passive_body_of(passive, store, i);
        if (store->borrowed_health[i] <= 0.0f32) {
            mp_die(passive, store, drops, i);
            continue;
        }
        if ((store->borrowed_flags[i] & ENTITY_STORE_FLAG_WAITING) != 0u) {
            continue;
        }
        for (uint32_t y = 0u; y < MOB_YIELDS_MAX; y++) {
            body->yield_wait[y] = body->yield_wait[y] != 0u ? body->yield_wait[y] - 1u : 0u;
        }
        /* Any hurt sends it running, a fall too, and away from whoever is about: an animal does not stop to ask. */
        if (store->borrowed_health[i] < body->last_health) {
            body->flee = (uint16_t)(MOB_PASSIVE_FLEE_TICKS + 1u);
            body->from[0] = view->position[0];
            body->from[1] = view->position[1];
            passive->borrowed_tally->fled_count++;
        }
        body->last_health = store->borrowed_health[i];
        if (body->flee != 0u) {
            mp_flee(store, i, body);
        } else {
            mp_stroll(passive, path, store, i, body);
        }
    }
}
