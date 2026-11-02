/* See README.md. Allocates nothing: all it knows is in player_survival_t, and it only ever asks physics whether a box is clear. */
#include "player/survival/index.h"

#include "tables/blocks/index.h"
#include "world/column/index.h"

#include <math.h>
#include <string.h>

/* What tires a body, per block or per act: the published numbers. */
#define SURVIVAL_TIRE_SPRINT 0.1f64
#define SURVIVAL_TIRE_SWIM 0.01f64
#define SURVIVAL_TIRE_JUMP 0.05f32
#define SURVIVAL_TIRE_SPRINT_JUMP 0.2f32
#define SURVIVAL_TIRE_MINE 0.005f32
#define SURVIVAL_TIRE_HIT 0.1f32
#define SURVIVAL_TIRE_HEAL_SLOW 6.0f32
#define SURVIVAL_HEAL_FAST_SATURATION 6.0f32

#define SURVIVAL_LAVA_DAMAGE 4.0f32
#define SURVIVAL_CACTUS_DAMAGE 1.0f32
#define SURVIVAL_DROWN_DAMAGE 2.0f32
#define SURVIVAL_SUFFOCATE_DAMAGE 1.0f32
#define SURVIVAL_BURN_DAMAGE 1.0f32
#define SURVIVAL_STARVE_DAMAGE 1.0f32
#define SURVIVAL_STARVE_LEAVES 1.0f32

#define SURVIVAL_ARMOR_MAX 20.0f32
#define SURVIVAL_ARMOR_PER_POINT 0.04f32
#define SURVIVAL_ARMOR_MIN_PART 5.0f32
#define SURVIVAL_ARMOR_SPLIT 2.0f32
#define SURVIVAL_FALL_ON_BED 0.5f32

/* How close a cactus has to be to hurt: a body it stops only touches it. */
#define SURVIVAL_TOUCH 0.01f64
/* What the feet sit just above, to find what they stand on. */
#define SURVIVAL_UNDER_FEET 0.01f64

/* Bits of buttons_before. */
enum : uint8_t {
    SURVIVAL_BUTTON_ATTACK = 1u,
    SURVIVAL_BUTTON_PLACE = 2u,
    SURVIVAL_BUTTON_JUMP = 4u,
    SURVIVAL_BUTTON_SNEAK = 8u,
};

/* For each cause, whether armor helps, and how much a hit tires. */
typedef struct survival_cause_row_t {
    float32_t tires;  /* 0 */
    uint8_t armored;  /* 4 */
    uint8_t _pad0[3]; /* 5 */
} survival_cause_row_t;
static_assert(sizeof(survival_cause_row_t) == 8);

static const survival_cause_row_t SURVIVAL_CAUSES[PLAYER_SURVIVAL_CAUSE_COUNT] = {
    [PLAYER_SURVIVAL_CAUSE_NONE] = {0.0f32, 0u, {0}},
    [PLAYER_SURVIVAL_CAUSE_FALL] = {0.0f32, 0u, {0}},
    [PLAYER_SURVIVAL_CAUSE_LAVA] = {SURVIVAL_TIRE_HIT, 1u, {0}},
    [PLAYER_SURVIVAL_CAUSE_BURN] = {0.0f32, 0u, {0}},
    [PLAYER_SURVIVAL_CAUSE_CACTUS] = {SURVIVAL_TIRE_HIT, 1u, {0}},
    [PLAYER_SURVIVAL_CAUSE_DROWN] = {0.0f32, 0u, {0}},
    [PLAYER_SURVIVAL_CAUSE_SUFFOCATE] = {0.0f32, 0u, {0}},
    [PLAYER_SURVIVAL_CAUSE_STARVE] = {0.0f32, 0u, {0}},
    [PLAYER_SURVIVAL_CAUSE_ATTACK] = {SURVIVAL_TIRE_HIT, 1u, {0}},
};

void player_survival_start(player_survival_t *survival, float64_t x, float64_t y, float64_t z) {
    memset(survival, 0, sizeof(*survival));
    survival->spawn[0] = x;
    survival->spawn[1] = y;
    survival->spawn[2] = z;
    survival->health = (float32_t)PLAYER_SURVIVAL_HEALTH_MAX;
    survival->food = PLAYER_SURVIVAL_FOOD_MAX;
    survival->saturation = PLAYER_SURVIVAL_SATURATION_START;
    survival->air = (int32_t)PLAYER_SURVIVAL_AIR_MAX;
    survival->day_fraction = PLAYER_SURVIVAL_MORNING;
}

bool player_survival_is_night(float64_t day_fraction) {
    const float64_t wrapped = day_fraction - floor(day_fraction);
    return wrapped >= PLAYER_SURVIVAL_NIGHT_FROM || wrapped < PLAYER_SURVIVAL_NIGHT_TO;
}

void player_survival_day_set(player_survival_t *survival, float64_t day_fraction) {
    survival->day_fraction = day_fraction;
}

void player_survival_armor_set(player_survival_t *survival, float32_t points) {
    survival->armor_points = points;
}

float32_t player_survival_after_armor(float32_t amount, float32_t points) {
    const float32_t by_hit = points - amount / SURVIVAL_ARMOR_SPLIT;
    const float32_t by_points = points / SURVIVAL_ARMOR_MIN_PART;
    const float32_t counted = by_hit > by_points ? by_hit : by_points;
    const float32_t limited = counted > SURVIVAL_ARMOR_MAX ? SURVIVAL_ARMOR_MAX : counted;
    return amount * (1.0f32 - limited * SURVIVAL_ARMOR_PER_POINT);
}

float32_t player_survival_fall_damage(float64_t distance, bool on_bed) {
    /* In float32_t, whose rounding hides what adding up a fall's steps gets wrong: 3 blocks is 3, and not one more hit. */
    const float32_t counted = (float32_t)distance - PLAYER_SURVIVAL_FALL_SAFE;
    if (counted <= 0.0f32) {
        return 0.0f32;
    }
    return ceilf(on_bed ? counted * SURVIVAL_FALL_ON_BED : counted);
}

void player_survival_hold_food(player_survival_t *survival, uint16_t item, uint32_t food, float32_t saturation) {
    if (survival->held_item != item) {
        survival->eat_ticks = 0u;
    }
    survival->held_item = item;
    survival->held_food = (uint8_t)food;
    survival->held_saturation = saturation;
}

static void survival_die(player_survival_t *survival, player_survival_cause_t cause) {
    survival->health = 0.0f32;
    survival->state = PLAYER_SURVIVAL_DEAD;
    survival->state_ticks = 0u;
    survival->eat_ticks = 0u;
    survival->burn_ticks = 0u;
    survival->killed_by = cause;
    survival->died_count++;
    survival->died_new = 1u;
}

float32_t player_survival_hurt(player_survival_t *survival, float32_t amount, player_survival_cause_t cause) {
    if (survival->state == PLAYER_SURVIVAL_DEAD || survival->placing != 0u || amount <= 0.0f32) {
        return 0.0f32;
    }
    float32_t counted = amount;
    if (survival->invulnerable != 0u) {
        if (amount <= survival->last_hurt) {
            return 0.0f32;
        }
        counted = amount - survival->last_hurt;
    } else {
        survival->invulnerable = PLAYER_SURVIVAL_INVULNERABLE_TICKS;
    }
    survival->last_hurt = amount;
    const survival_cause_row_t *const row = &SURVIVAL_CAUSES[cause];
    const float32_t taken = row->armored != 0u ? player_survival_after_armor(counted, survival->armor_points) : counted;
    player_survival_exhaust(survival, row->tires);
    survival->hurt_count++;
    survival->health -= taken;
    /* It stays where it lay: whatever hurt it is no reason to look for a better place to stand. */
    survival->state = PLAYER_SURVIVAL_ALIVE;
    if (survival->health <= 0.0f32) {
        survival_die(survival, cause);
    }
    return taken;
}

void player_survival_exhaust(player_survival_t *survival, float32_t amount) {
    const float32_t total = survival->exhaustion + amount;
    survival->exhaustion = total > PLAYER_SURVIVAL_EXHAUSTION_MAX ? PLAYER_SURVIVAL_EXHAUSTION_MAX : total;
}

void player_survival_eat(player_survival_t *survival, uint32_t food, float32_t saturation) {
    const uint32_t total = (uint32_t)survival->food + food;
    survival->food = (uint8_t)(total > PLAYER_SURVIVAL_FOOD_MAX ? PLAYER_SURVIVAL_FOOD_MAX : total);
    const float32_t filled = survival->saturation + saturation;
    const float32_t limit = (float32_t)survival->food;
    survival->saturation = filled > limit ? limit : filled;
}

bool player_survival_holds_body(const player_survival_t *survival) {
    return survival->state != PLAYER_SURVIVAL_ALIVE || survival->placing != 0u;
}

bool player_survival_can_sprint(const player_survival_t *survival) {
    return survival->food > PLAYER_SURVIVAL_FOOD_NO_SPRINT && survival->eat_ticks == 0u;
}

bool player_survival_died_take(player_survival_t *survival) {
    const bool died = survival->died_new != 0u;
    survival->died_new = 0u;
    return died;
}

bool player_survival_morning_take(player_survival_t *survival) {
    const bool morning = survival->morning_new != 0u;
    survival->morning_new = 0u;
    return morning;
}

/* ---- Where a body goes ------------------------------------------------------------------------------------------------- */

/* How high a body's feet are on this bed. */
static float64_t survival_bed_top(const int32_t cell[COLLISION_AXES]) {
    collision_box_t boxes[COLLISION_BOXES_PER_BLOCK];
    const uint32_t count = collision_block_boxes(BLOCK_ID_BED, cell[0], cell[1], cell[2], boxes);
    return count != 0u ? boxes[0].max[COLLISION_AXIS_Z] : (float64_t)cell[2] + 1.0f64;
}

static void survival_body_on_bed(const player_survival_t *survival, player_movement_t *movement) {
    player_movement_start(movement, (float64_t)survival->bed_cell[0] + 0.5f64,
                          (float64_t)survival->bed_cell[1] + 0.5f64, survival_bed_top(survival->bed_cell));
}

/* Whether a standing body fits at this place with something to stand on, out of the lava. */
static bool survival_can_stand(physics_t *physics, const world_map_t *map, const float64_t feet[COLLISION_AXES]) {
    collision_box_t box;
    player_movement_box(feet, PLAYER_MOVEMENT_HEIGHT_STANDING, &box);
    if (!physics_box_is_clear(physics, map, &box)) {
        return false;
    }
    uint16_t block = BLOCK_ID_AIR;
    if (!collision_cell_read(map, (int32_t)floor(feet[0]), (int32_t)floor(feet[1]), (int32_t)floor(feet[2]), &block) ||
        block == BLOCK_ID_LAVA) {
        return false;
    }
    const float64_t below[COLLISION_AXES] = {feet[0], feet[1], feet[2] - 1.0f64};
    collision_box_t under;
    player_movement_box(below, 1.0f64, &under);
    return !physics_box_is_clear(physics, map, &under);
}

/* Beside the bed on any of its four sides, level with it or a block up, and if none will do then on it. */
static void survival_stand_by_bed(const player_survival_t *survival, player_movement_t *movement, physics_t *physics,
                                  const world_map_t *map) {
    static const int8_t sides[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int32_t up = 0; up < 2; up++) {
        for (uint32_t side = 0u; side < 4u; side++) {
            const float64_t feet[COLLISION_AXES] = {
                (float64_t)(survival->bed_cell[0] + (int32_t)sides[side][0]) + 0.5f64,
                (float64_t)(survival->bed_cell[1] + (int32_t)sides[side][1]) + 0.5f64,
                (float64_t)(survival->bed_cell[2] + up),
            };
            if (survival_can_stand(physics, map, feet)) {
                player_movement_start(movement, feet[0], feet[1], feet[2]);
                return;
            }
        }
    }
    survival_body_on_bed(survival, movement);
}

/* At the world's place: as far up as it takes to fit, since anything may have been built there. */
static void survival_stand_at_spawn(const player_survival_t *survival, player_movement_t *movement, physics_t *physics,
                                    const world_map_t *map) {
    float64_t feet[COLLISION_AXES] = {survival->spawn[0], survival->spawn[1], survival->spawn[2]};
    while (feet[2] < (float64_t)COLUMN_Z_STORAGE_END) {
        collision_box_t box;
        player_movement_box(feet, PLAYER_MOVEMENT_HEIGHT_STANDING, &box);
        if (physics_box_is_clear(physics, map, &box)) {
            break;
        }
        feet[2] = floor(feet[2]) + 1.0f64;
    }
    player_movement_start(movement, feet[0], feet[1], feet[2]);
}

/* A body that has come back waits, holding still, for the ground where it came back to: only then can it be known if the bed is still
 * there, and what is in the way. */
static void survival_place(player_survival_t *survival, player_movement_t *movement, physics_t *physics,
                           const world_map_t *map) {
    uint16_t block = BLOCK_ID_AIR;
    if (survival->placing_at_bed != 0u) {
        if (!collision_cell_read(map, survival->bed_cell[0], survival->bed_cell[1], survival->bed_cell[2], &block)) {
            return;
        }
        survival->placing_at_bed = 0u;
        if (block == BLOCK_ID_BED) {
            survival_stand_by_bed(survival, movement, physics, map);
            survival->placing = 0u;
            return;
        }
        /* The bed has gone, and with it this as a place to come back to. */
        survival->has_bed = 0u;
        player_movement_start(movement, survival->spawn[0], survival->spawn[1], survival->spawn[2]);
    }
    if (!collision_cell_read(map, (int32_t)floor(survival->spawn[0]), (int32_t)floor(survival->spawn[1]),
                             (int32_t)floor(survival->spawn[2]), &block)) {
        return;
    }
    survival_stand_at_spawn(survival, movement, physics, map);
    survival->placing = 0u;
}

static void survival_respawn(player_survival_t *survival, player_movement_t *movement, physics_t *physics,
                             const world_map_t *map) {
    survival->state = PLAYER_SURVIVAL_ALIVE;
    survival->state_ticks = 0u;
    survival->health = (float32_t)PLAYER_SURVIVAL_HEALTH_MAX;
    survival->food = PLAYER_SURVIVAL_FOOD_MAX;
    survival->saturation = PLAYER_SURVIVAL_SATURATION_START;
    survival->exhaustion = 0.0f32;
    survival->food_timer = 0u;
    survival->air = (int32_t)PLAYER_SURVIVAL_AIR_MAX;
    survival->burn_ticks = 0u;
    survival->invulnerable = 0u;
    survival->last_hurt = 0.0f32;
    survival->respawned_count++;
    survival->placing = 1u;
    survival->placing_at_bed = survival->has_bed;
    if (survival->has_bed != 0u) {
        survival_body_on_bed(survival, movement);
    } else {
        player_movement_start(movement, survival->spawn[0], survival->spawn[1], survival->spawn[2]);
    }
    survival_place(survival, movement, physics, map);
}

static void survival_wake(player_survival_t *survival, player_movement_t *movement, physics_t *physics,
                          const world_map_t *map) {
    survival->state = PLAYER_SURVIVAL_ALIVE;
    survival->state_ticks = 0u;
    survival_stand_by_bed(survival, movement, physics, map);
}

/* ---- The buttons ------------------------------------------------------------------------------------------------------- */

static void survival_bed_use(player_survival_t *survival, player_movement_t *movement,
                             const int32_t cell[COLLISION_AXES]) {
    const float64_t across_x = fabs(movement->position[0] - ((float64_t)cell[0] + 0.5f64));
    const float64_t across_y = fabs(movement->position[1] - ((float64_t)cell[1] + 0.5f64));
    const float64_t up = fabs(movement->position[2] - ((float64_t)cell[2] + 0.5f64));
    if (across_x > PLAYER_SURVIVAL_BED_REACH_ACROSS || across_y > PLAYER_SURVIVAL_BED_REACH_ACROSS ||
        up > PLAYER_SURVIVAL_BED_REACH_UP) {
        survival->bed_answer = PLAYER_SURVIVAL_BED_TOO_FAR;
        return;
    }
    memcpy(survival->bed_cell, cell, sizeof(survival->bed_cell));
    survival->has_bed = 1u;
    if (!player_survival_is_night(survival->day_fraction)) {
        survival->bed_answer = PLAYER_SURVIVAL_BED_NOT_NIGHT;
        return;
    }
    survival->bed_answer = PLAYER_SURVIVAL_BED_ASLEEP;
    survival->state = PLAYER_SURVIVAL_SLEEPING;
    survival->state_ticks = 0u;
    survival_body_on_bed(survival, movement);
}

static void survival_buttons_clear(player_survival_buttons_t *buttons) {
    buttons->attack = 0u;
    buttons->place = 0u;
}

/* A living body: the right button on a bed, unless it is sneaking, and on food. */
static void survival_buttons_alive(player_survival_t *survival, player_survival_buttons_t *buttons, uint32_t pressed) {
    const raycast_hit_t *const hit = &buttons->borrowed_target->hit;
    if (buttons->place != 0u && buttons->sneak == 0u && hit->result == RAYCAST_RESULT_HIT) {
        uint16_t block = BLOCK_ID_AIR;
        /* The target may be a tick old. */
        if (collision_cell_read(buttons->borrowed_map, hit->cell[0], hit->cell[1], hit->cell[2], &block) &&
            block == BLOCK_ID_BED) {
            survival->eat_ticks = 0u;
            if ((pressed & SURVIVAL_BUTTON_PLACE) != 0u) {
                survival_bed_use(survival, buttons->borrowed_movement, hit->cell);
            }
            survival_buttons_clear(buttons);
            return;
        }
    }
    if (buttons->place == 0u || survival->held_food == 0u || survival->food >= PLAYER_SURVIVAL_FOOD_MAX) {
        survival->eat_ticks = 0u;
        return;
    }
    survival_buttons_clear(buttons);
    survival->eat_ticks++;
    if (survival->eat_ticks >= PLAYER_SURVIVAL_EAT_TICKS) {
        player_survival_eat(survival, survival->held_food, survival->held_saturation);
        survival->eaten_count++;
        survival->eat_ticks = 0u;
    }
}

void player_survival_buttons(player_survival_t *survival, player_survival_buttons_t *buttons) {
    const uint32_t now = (buttons->attack != 0u ? (uint32_t)SURVIVAL_BUTTON_ATTACK : 0u) |
                         (buttons->place != 0u ? (uint32_t)SURVIVAL_BUTTON_PLACE : 0u) |
                         (buttons->jump != 0u ? (uint32_t)SURVIVAL_BUTTON_JUMP : 0u) |
                         (buttons->sneak != 0u ? (uint32_t)SURVIVAL_BUTTON_SNEAK : 0u);
    const uint32_t pressed = now & ~(uint32_t)survival->buttons_before;
    survival->buttons_before = (uint8_t)now;
    player_movement_t *const movement = buttons->borrowed_movement;
    physics_t *const physics = buttons->borrowed_physics;
    const world_map_t *const map = buttons->borrowed_map;
    if (survival->state == PLAYER_SURVIVAL_DEAD) {
        if (survival->state_ticks >= PLAYER_SURVIVAL_RESPAWN_WAIT_TICKS &&
            (pressed & (uint32_t)(SURVIVAL_BUTTON_ATTACK | SURVIVAL_BUTTON_PLACE | SURVIVAL_BUTTON_JUMP)) != 0u) {
            survival_respawn(survival, movement, physics, map);
        }
        survival_buttons_clear(buttons);
        return;
    }
    if (survival->placing != 0u) {
        survival_place(survival, movement, physics, map);
        survival_buttons_clear(buttons);
        return;
    }
    if (survival->state == PLAYER_SURVIVAL_SLEEPING) {
        if (pressed != 0u) {
            survival_wake(survival, movement, physics, map);
        }
        survival_buttons_clear(buttons);
        return;
    }
    survival_buttons_alive(survival, buttons, pressed);
}

/* ---- One tick ---------------------------------------------------------------------------------------------------------- */

/* Whether any cell this close to the body holds a cactus. */
static bool survival_touches_cactus(const player_movement_t *movement, const world_map_t *map) {
    collision_box_t box;
    const float64_t height =
        movement->crouched != 0u ? PLAYER_MOVEMENT_HEIGHT_CROUCHED : PLAYER_MOVEMENT_HEIGHT_STANDING;
    player_movement_box(movement->position, height, &box);
    const int32_t x_from = (int32_t)floor(box.min[0] - SURVIVAL_TOUCH),
                  x_to = (int32_t)floor(box.max[0] + SURVIVAL_TOUCH);
    const int32_t y_from = (int32_t)floor(box.min[1] - SURVIVAL_TOUCH),
                  y_to = (int32_t)floor(box.max[1] + SURVIVAL_TOUCH);
    const int32_t z_from = (int32_t)floor(box.min[2] - SURVIVAL_TOUCH),
                  z_to = (int32_t)floor(box.max[2] + SURVIVAL_TOUCH);
    for (int32_t x = x_from; x <= x_to; x++) {
        for (int32_t y = y_from; y <= y_to; y++) {
            for (int32_t z = z_from; z <= z_to; z++) {
                uint16_t block = BLOCK_ID_AIR;
                if (collision_cell_read(map, x, y, z, &block) && block == BLOCK_ID_CACTUS) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* A full cube that stops all light: glass and leaves do not smother. */
static bool survival_eyes_buried(const player_movement_t *movement, const world_map_t *map) {
    uint16_t block = BLOCK_ID_AIR;
    if (!collision_cell_read(map, (int32_t)floor(movement->position[0]), (int32_t)floor(movement->position[1]),
                             (int32_t)floor(movement->position[2] + movement->eye_height), &block)) {
        return false;
    }
    const block_def_t *const def = &block_defs[block];
    return (def->flags & BLOCK_FLAG_SOLID) != 0u && def->shape == BLOCK_SHAPE_CUBE && def->opacity >= 15u;
}

static bool survival_on_bed(const player_movement_t *movement, const world_map_t *map) {
    uint16_t block = BLOCK_ID_AIR;
    return collision_cell_read(map, (int32_t)floor(movement->position[0]), (int32_t)floor(movement->position[1]),
                               (int32_t)floor(movement->position[2] - SURVIVAL_UNDER_FEET), &block) &&
           block == BLOCK_ID_BED;
}

static void survival_breathe(player_survival_t *survival, const player_movement_t *movement) {
    if (movement->eyes_under_water == 0u) {
        const int32_t filled = (survival->air < 0 ? 0 : survival->air) + (int32_t)PLAYER_SURVIVAL_AIR_REFILL;
        survival->air = filled > (int32_t)PLAYER_SURVIVAL_AIR_MAX ? (int32_t)PLAYER_SURVIVAL_AIR_MAX : filled;
        return;
    }
    survival->air--;
    if (survival->air <= -(int32_t)PLAYER_SURVIVAL_DROWN_EVERY_TICKS) {
        survival->air = 0;
        (void)player_survival_hurt(survival, SURVIVAL_DROWN_DAMAGE, PLAYER_SURVIVAL_CAUSE_DROWN);
    }
}

static void survival_burn(player_survival_t *survival, const player_movement_t *movement) {
    if (movement->mode == PLAYER_MOVEMENT_MODE_WATER) {
        survival->burn_ticks = 0u;
        return;
    }
    if (movement->mode == PLAYER_MOVEMENT_MODE_LAVA) {
        survival->burn_ticks = PLAYER_SURVIVAL_BURN_TICKS;
        (void)player_survival_hurt(survival, SURVIVAL_LAVA_DAMAGE, PLAYER_SURVIVAL_CAUSE_LAVA);
        return;
    }
    if (survival->burn_ticks != 0u) {
        survival->burn_ticks--;
        if (survival->burn_ticks % PLAYER_SURVIVAL_BURN_EVERY_TICKS == 0u) {
            (void)player_survival_hurt(survival, SURVIVAL_BURN_DAMAGE, PLAYER_SURVIVAL_CAUSE_BURN);
        }
    }
}

/* What the world does to it. When several come at one tick the first counts in full, and the rest only by what they are over it. */
static void survival_hazards(player_survival_t *survival, const player_movement_t *movement, const world_map_t *map) {
    const float32_t fall = player_survival_fall_damage(movement->landed_fall, survival_on_bed(movement, map));
    (void)player_survival_hurt(survival, fall, PLAYER_SURVIVAL_CAUSE_FALL);
    survival_burn(survival, movement);
    survival_breathe(survival, movement);
    if (survival_touches_cactus(movement, map)) {
        (void)player_survival_hurt(survival, SURVIVAL_CACTUS_DAMAGE, PLAYER_SURVIVAL_CAUSE_CACTUS);
    }
    if (survival_eyes_buried(movement, map)) {
        (void)player_survival_hurt(survival, SURVIVAL_SUFFOCATE_DAMAGE, PLAYER_SURVIVAL_CAUSE_SUFFOCATE);
    }
}

/* What moving took out of it, from where it was and is. */
static void survival_tire(player_survival_t *survival, const player_movement_t *movement) {
    const float64_t dx = movement->position[0] - movement->previous[0];
    const float64_t dy = movement->position[1] - movement->previous[1];
    const float64_t dz = movement->position[2] - movement->previous[2];
    if (movement->mode == PLAYER_MOVEMENT_MODE_WATER) {
        player_survival_exhaust(survival, (float32_t)(SURVIVAL_TIRE_SWIM * sqrt(dx * dx + dy * dy + dz * dz)));
    } else if (movement->sprinting != 0u && movement->on_ground != 0u) {
        player_survival_exhaust(survival, (float32_t)(SURVIVAL_TIRE_SPRINT * sqrt(dx * dx + dy * dy)));
    }
    if (survival->was_on_ground != 0u && movement->on_ground == 0u && dz > 0.0f64 &&
        movement->mode == PLAYER_MOVEMENT_MODE_AIR) {
        player_survival_exhaust(survival, movement->sprinting != 0u ? SURVIVAL_TIRE_SPRINT_JUMP : SURVIVAL_TIRE_JUMP);
    }
    survival->was_on_ground = movement->on_ground;
}

static void survival_heal(player_survival_t *survival, float32_t amount) {
    const float32_t total = survival->health + amount;
    const float32_t limit = (float32_t)PLAYER_SURVIVAL_HEALTH_MAX;
    survival->health = total > limit ? limit : total;
}

/* Saturation goes before food, and food decides whether it heals or starves. */
static void survival_food_tick(player_survival_t *survival) {
    if (survival->exhaustion > PLAYER_SURVIVAL_EXHAUSTION_STEP) {
        survival->exhaustion -= PLAYER_SURVIVAL_EXHAUSTION_STEP;
        if (survival->saturation > 0.0f32) {
            const float32_t left = survival->saturation - 1.0f32;
            survival->saturation = left < 0.0f32 ? 0.0f32 : left;
        } else if (survival->food != 0u) {
            survival->food--;
        }
    }
    const bool hurt = survival->health < (float32_t)PLAYER_SURVIVAL_HEALTH_MAX;
    if (hurt && survival->saturation > 0.0f32 && survival->food >= PLAYER_SURVIVAL_FOOD_MAX) {
        survival->food_timer++;
        if (survival->food_timer >= PLAYER_SURVIVAL_HEAL_FAST_TICKS) {
            const float32_t spent = survival->saturation < SURVIVAL_HEAL_FAST_SATURATION
                                        ? survival->saturation
                                        : SURVIVAL_HEAL_FAST_SATURATION;
            survival_heal(survival, spent / SURVIVAL_HEAL_FAST_SATURATION);
            player_survival_exhaust(survival, spent);
            survival->food_timer = 0u;
        }
    } else if (hurt && survival->food >= PLAYER_SURVIVAL_FOOD_HEALS) {
        survival->food_timer++;
        if (survival->food_timer >= PLAYER_SURVIVAL_HEAL_SLOW_TICKS) {
            survival_heal(survival, 1.0f32);
            player_survival_exhaust(survival, SURVIVAL_TIRE_HEAL_SLOW);
            survival->food_timer = 0u;
        }
    } else if (survival->food == 0u) {
        survival->food_timer++;
        if (survival->food_timer >= PLAYER_SURVIVAL_STARVE_TICKS) {
            if (survival->health > SURVIVAL_STARVE_LEAVES) {
                (void)player_survival_hurt(survival, SURVIVAL_STARVE_DAMAGE, PLAYER_SURVIVAL_CAUSE_STARVE);
            }
            survival->food_timer = 0u;
        }
    } else {
        survival->food_timer = 0u;
    }
}

static void survival_sleep_tick(player_survival_t *survival, player_movement_t *movement, physics_t *physics,
                                const world_map_t *map) {
    uint16_t block = BLOCK_ID_AIR;
    if (!collision_cell_read(map, survival->bed_cell[0], survival->bed_cell[1], survival->bed_cell[2], &block) ||
        block != BLOCK_ID_BED) {
        /* The bed went from under it. It stays where it is, and falls if there is nothing under it. */
        survival->state = PLAYER_SURVIVAL_ALIVE;
        survival->state_ticks = 0u;
        return;
    }
    survival->state_ticks++;
    if (survival->state_ticks >= PLAYER_SURVIVAL_SLEEP_TICKS) {
        survival->slept_count++;
        survival->morning_new = 1u;
        survival_wake(survival, movement, physics, map);
    }
}

void player_survival_tick(player_survival_t *survival, player_movement_t *movement, physics_t *physics,
                          const world_map_t *map, uint64_t mined_count) {
    if (survival->invulnerable != 0u) {
        survival->invulnerable--;
    }
    const uint64_t mined = mined_count - survival->mined_seen;
    survival->mined_seen = mined_count;
    if (survival->state == PLAYER_SURVIVAL_DEAD) {
        survival->state_ticks++;
        return;
    }
    /* With no ground loaded, what happens to a body has nothing to go on. */
    if (survival->placing != 0u || movement->waiting_for_column != 0u) {
        return;
    }
    player_survival_exhaust(survival, (float32_t)mined * SURVIVAL_TIRE_MINE);
    survival_hazards(survival, movement, map);
    if (survival->state == PLAYER_SURVIVAL_DEAD) {
        return;
    }
    survival_tire(survival, movement);
    survival_food_tick(survival);
    if (survival->state == PLAYER_SURVIVAL_SLEEPING) {
        survival_sleep_tick(survival, movement, physics, map);
    }
}
