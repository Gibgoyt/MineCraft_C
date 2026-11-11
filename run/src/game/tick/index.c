/* See README.md. */
#include "game/index.h"

#include "audio/index.h"
#include "climate/sun/index.h"
#include "entity/animation/index.h"
#include "game/corner/index.h"
#include "game/watch/index.h"
#include "game/weather/index.h"
#include "item/stack/index.h"
#include "log/index.h"
#include "render/entities/index.h"
#include "tables/blocks/index.h"
#include "time/index.h"
#include "world/bench/index.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

/* An item picked up goes to the inventory, or with none to the hand. What does not fit stays on the ground. */
static uint32_t game_pickup_take(void *borrowed_game, uint16_t item, uint16_t wear, uint32_t count) {
    game_t *const game = borrowed_game;
    if (game->borrowed_item != nullptr) {
        inventory_t *const inventory = &game->borrowed_item->inventory;
        const uint16_t id = block_drop_items[item].item;
        if (wear == 0u) {
            return inventory_add(inventory, id, count);
        }
        /* What wears stacks to one, while a drop may be several as worn as each other. */
        uint32_t taken = 0u;
        while (taken < count) {
            item_stack_t one = item_stack_make(id, 1u);
            one.wear = wear;
            if (inventory_add_stack(inventory, &one) == 0u) {
                break;
            }
            taken++;
        }
        return taken;
    }
    return player_interaction_hand_take(&game->borrowed_player->interaction, item, count);
}

/* The hands place from the hotbar slot in hand, and know nothing of it: they see its block, or nothing if it places none. */
static void game_hand_from_hotbar(game_t *game) {
    const item_stack_t *const held = inventory_selected(&game->borrowed_item->inventory);
    const uint16_t block = item_defs[held->item].block;
    player_interaction_hand_set(&game->borrowed_player->interaction, block == ITEM_NO_BLOCK ? BLOCK_ID_AIR : block,
                                block == ITEM_NO_BLOCK ? 0u : held->count);
    player_survival_hold_food(&game->borrowed_player->survival, held->item, item_defs[held->item].food,
                              item_defs[held->item].saturation);
}

/* Mining counts as what is in hand. A different tool starts the block again, so that the last touch cannot be the one that wears least. */
static void game_tool_from_hotbar(game_t *game) {
    player_interaction_t *const hands = &game->borrowed_player->interaction;
    uint8_t tool = BLOCK_TOOL_NONE, tier = BLOCK_TIER_NONE;
    equipment_tool_held(&game->borrowed_item->inventory, &tool, &tier);
    const uint32_t speed = equipment_tool_speed_milli(inventory_selected(&game->borrowed_item->inventory)->item);
    if (hands->held_tool != tool || hands->held_tier != tier || hands->held_speed_milli != speed) {
        hands->progress = 0u;
        player_interaction_hold_tool(hands, tool, tier);
        player_interaction_hold_speed(hands, speed);
    }
}

/* The right button with no left, as for placing. Only a look: player_interact_tick takes the latch next. A press and release that fall
 * between the two reach placing alone, and the next press is not far behind. */
static bool game_use_held(const player_t *player, int64_t tick_ns) {
    if (tick_ns - atomic_load_explicit(&player->intent_sent_ns, memory_order_relaxed) > PLAYER_INTENT_STALE_NS) {
        return false;
    }
    const uint32_t hands = atomic_load_explicit(&player->hands_word, memory_order_relaxed) |
                           atomic_load_explicit(&player->hands_latch, memory_order_relaxed);
    return (hands & PLAYER_HAND_PLACE) != 0u && (hands & PLAYER_HAND_ATTACK) == 0u;
}

/* What the hands broke this tick wears what broke it, and shears bring the block itself out as well. */
static void game_tool_after_break(game_t *game, uint16_t held_item) {
    const player_interaction_t *const hands = &game->borrowed_player->interaction;
    item_t *const item = game->borrowed_item;
    const uint16_t own = equipment_own_drop(held_item, hands->last_mined);
    if (own != ITEM_ID_NONE && game->borrowed_entity != nullptr) {
        const float64_t middle[3] = {(float64_t)hands->cell[0] + 0.5f64, (float64_t)hands->cell[1] + 0.5f64,
                                     (float64_t)hands->cell[2] + 0.5f64};
        item_drop_spawn(&game->borrowed_entity->item_drops, item_stack_drop_row(own), 1u, middle);
    }
    (void)equipment_tool_block_broken(&item->equipment, &item->inventory, hands->last_mined);
}

/* What the hands did this tick is heard from where they did it. Taken with or without anyone to hear, so that a run with sound and one without go the same. */
static void game_sounds_tick(game_t *game) {
    player_t *const player = game->borrowed_player;
    player_sound_t sound;
    while (player_sounds_next(game->borrowed_sounds, &sound)) {
        if (game->borrowed_audio == nullptr) {
            continue;
        }
        const float64_t ear[3] = {player->movement.position[0], player->movement.position[1],
                                  player->movement.position[2] + player->movement.eye_height};
        const float64_t source[3] = {(float64_t)sound.cell[0] + 0.5f64, (float64_t)sound.cell[1] + 0.5f64,
                                     (float64_t)sound.cell[2] + 0.5f64};
        float64_t forward[3];
        player_camera_direction(player_yaw_sent(player), 0.0f32, forward);
        uint16_t left = 0u, right = 0u;
        audio_mixer_gains(ear, forward, source, &left, &right);
        (void)audio_post(game->borrowed_audio, sound.family, sound.kind, left, right);
    }
}

/* A tree coming down: one crunch a tick and not eight, heard from where it was cut. */
static void game_fell_sound(game_t *game, uint32_t felled) {
    if (felled != 0u) {
        const int32_t *const cell = game->borrowed_player->interaction.cell;
        player_sounds_post(game->borrowed_sounds, game->borrowed_felling->last_block, SOUND_KIND_BREAK, cell[0],
                           cell[1], cell[2]);
    }
}

/* What is left of a tree the hands cut comes down, a few logs a tick, whatever is in hand. Only the log the hands cut wears what they hold,
 * as any block does: what falls with it costs nothing (Ahmed, plan 005 phase 1). */
static void game_fell_tick(game_t *game) {
    player_t *const player = game->borrowed_player;
    world_t *const world = game->borrowed_world;
    game_fell_sound(game,
                    player_felling_tick(game->borrowed_felling, &player->interaction, &world->edit, &world->tick));
}

/* The right button on a block with a screen brings it up, as it goes down. A held button that comes to rest on one does not, or a screen
 * just closed would come straight back. While a screen is up, or the button is on such a block, the hands do nothing: a chest is not built on,
 * and sneaking is how to build on one. */
static void game_screen_hands(game_t *game, int64_t tick_ns) {
    player_t *const player = game->borrowed_player;
    ui_t *const ui = game->borrowed_ui;
    const raycast_hit_t *const hit = &player->target.hit;
    const bool held = game_use_held(player, tick_ns);
    const bool pressed = held && game->use_was_held == 0u;
    /* main sends no buttons while a screen is up, and one still held as it goes is no new press. */
    game->use_was_held = held || ui_screen_sim_kind(ui) != UI_SCREEN_NONE ? 1u : 0u;
    const bool sneaking = player_sneak_sent(player);
    const ui_screen_kind_t kind =
        hit->result == RAYCAST_RESULT_HIT && !sneaking ? ui_screen_kind_of_block(hit->block) : UI_SCREEN_NONE;
    if (pressed && kind != UI_SCREEN_NONE && !player_survival_holds_body(&player->survival)) {
        /* A station of more than one part keeps what it holds at its first part, a tanning rack's slots (plan 005 phase 6), so its screen
         * is of that cell whichever part was aimed at, if the first part is there to be it. */
        int32_t cell[3] = {hit->cell[0], hit->cell[1], hit->cell[2]};
        const world_map_t *const map = &game->borrowed_world->map;
        uint16_t voxel = BLOCK_ID_AIR, first = BLOCK_ID_AIR;
        int32_t steps[BENCH_PARTS_MAX][2];
        uint16_t parts[BENCH_PARTS_MAX];
        if (world_map_voxel_get(map, cell[0], cell[1], cell[2], &voxel) == WORLD_MAP_STATUS_OK &&
            bench_parts(voxel, steps, parts) != 0u &&
            world_map_voxel_get(map, cell[0] + steps[0][0], cell[1] + steps[0][1], cell[2], &first) ==
                WORLD_MAP_STATUS_OK &&
            first == parts[0]) {
            cell[0] += steps[0][0];
            cell[1] += steps[0][1];
        }
        (void)ui_screen_sim_open(ui, game->borrowed_item, kind, cell);
    }
    player->hands_held_off = ui_screen_sim_kind(ui) != UI_SCREEN_NONE || (held && kind != UI_SCREEN_NONE) ? 1u : 0u;
}

/* A press of the left button on a body in reach is a blow on it, with whatever is in hand, which it wears if it lands. Before the hands, who
 * then leave alone what is behind the body. */
static void game_attack_tick(game_t *game, int64_t tick_ns) {
    player_t *const player = game->borrowed_player;
    entity_t *const entity = game->borrowed_entity;
    mob_t *const mob = game->borrowed_mob;
    if (entity == nullptr || mob == nullptr || mob->combat.borrowed_bodies == nullptr) {
        return;
    }
    item_t *const item = game->borrowed_item;
    const item_stack_t held =
        item != nullptr ? *inventory_selected(&item->inventory) : item_stack_make(ITEM_ID_NONE, 0u);
    const player_combat_weapon_t weapon = {
        .damage = equipment_attack_damage(&held),
        .speed = player_combat_attack_speed(held.item),
        .item = held.item,
        ._pad0 = {0},
    };
    player_combat_swing_t swing;
    if (!player_attack_tick(player, &entity->store, &game->borrowed_world->map, &weapon, tick_ns, &swing)) {
        return;
    }
    const mob_combat_blow_t blow = {
        .from = {player->movement.position[0], player->movement.position[1]},
        .facing = {swing.facing[0], swing.facing[1]},
        .damage = swing.damage,
        .knockback = swing.knockback,
    };
    mob_combat_outcome_t outcome;
    if (mob_combat_strike(&mob->combat, &entity->store, swing.handle, &blow, &outcome)) {
        player_attack_landed(player, &swing);
        if (item != nullptr) {
            (void)equipment_tool_attacked(&item->equipment, &item->inventory);
        }
    }
}

/* What an animal gave into the hand: milk into the slot its bucket was in, or with more buckets there beside them, or on the ground at the
 * feet if there is no room. */
static void game_animal_gave_to_hand(game_t *game, uint16_t item_id, uint32_t count) {
    item_t *const item = game->borrowed_item;
    item_stack_t *const held = inventory_slot(&item->inventory, item->inventory.selected);
    (void)inventory_selected_take(&item->inventory, 1u);
    if (item_stack_is_empty(held)) {
        *held = item_stack_make(item_id, count);
        item->inventory.added_count++;
        return;
    }
    const uint32_t added = inventory_add(&item->inventory, item_id, count);
    if (added < count && game->borrowed_entity != nullptr) {
        const player_movement_t *const movement = &game->borrowed_player->movement;
        item_drop_spawn(&game->borrowed_entity->item_drops, item_stack_drop_row(item_id), count - added,
                        movement->position);
    }
}

/* A press of the right button on an animal with what it yields to in hand: shears on a sheep bring its wool out onto the ground and wear the
 * shears by one, and a bucket on a cow or a nanny comes back full of milk. Males give nothing alive (plan 005 phase 4). From that press until the
 * button is let go the hands do nothing more, ready or not, so that milk just taken is not drunk at once. Before the hands. */
static void game_animal_use_tick(game_t *game, int64_t tick_ns) {
    player_t *const player = game->borrowed_player;
    item_t *const item = game->borrowed_item;
    entity_t *const entity = game->borrowed_entity;
    mob_t *const mob = game->borrowed_mob;
    const bool held = game_use_held(player, tick_ns) && !player_survival_holds_body(&player->survival) &&
                      player->hands_held_off == 0u;
    const bool pressed = held && (player->animal_use & PLAYER_ANIMAL_USE_HELD) == 0u;
    if (!held) {
        player->animal_use = 0u;
    } else {
        player->animal_use |= PLAYER_ANIMAL_USE_HELD;
    }
    if (pressed && entity != nullptr && mob != nullptr && mob->passive.borrowed_bodies != nullptr) {
        entity_store_t *const store = &entity->store;
        const uint16_t tool = inventory_selected(&item->inventory)->item;
        const entity_handle_t handle = player_body_aimed(player, store, &game->borrowed_world->map);
        uint32_t index = 0u;
        if (handle != ENTITY_HANDLE_NONE && entity_store_index_of(store, handle, &index) &&
            mob_passive_yields_to(store, index, tool)) {
            player->animal_use |= PLAYER_ANIMAL_USE_ON;
            uint16_t given = ITEM_ID_NONE;
            uint32_t count = 0u;
            if (mob_passive_take(&mob->passive, store, index, tool, &given, &count)) {
                /* A tool that does not wear, a bucket, is what the yield comes in. One that wears, shears, brings it out. */
                if (item_defs[tool].durability == 0u) {
                    game_animal_gave_to_hand(game, given, count);
                } else {
                    const float64_t at[3] = {store->borrowed_position[0][index], store->borrowed_position[1][index],
                                             store->borrowed_position[2][index] + 0.5f64};
                    item_drop_spawn(&entity->item_drops, item_stack_drop_row(given), count, at);
                    (void)equipment_tool_wear(&item->equipment, &item->inventory, 1u);
                }
            }
        }
    }
    if ((player->animal_use & PLAYER_ANIMAL_USE_ON) != 0u) {
        player->hands_held_off = 1u;
    } else if (!game_has_screens(game)) {
        player->hands_held_off = 0u;
    }
}

/* One tick of the hands: which slot is in hand first, so that a key and a click in one tick act in that order, then what the right button
 * does with armor or a hoe; what they placed comes off the slot, and what they broke wears it. */
static void game_interact_tick(game_t *game, int64_t tick_ns) {
    player_t *const player = game->borrowed_player;
    item_t *const item = game->borrowed_item;
    if (item == nullptr) {
        game_attack_tick(game, tick_ns);
        player_interact_fell_tick(player, &game->borrowed_world->edit, game->borrowed_felling, game->borrowed_sounds,
                                  tick_ns);
        game_fell_tick(game);
        return;
    }
    item_hotbar_apply(item);
    if (game_has_screens(game)) {
        game_screen_hands(game, tick_ns);
    }
    game_animal_use_tick(game, tick_ns);
    const raycast_hit_t *const hit = &player->target.hit;
    /* A press that brings a body back or wakes it is used up by that. */
    equipment_use_tick(&item->equipment, &item->inventory, &game->borrowed_world->edit,
                       game_use_held(player, tick_ns) && !player_survival_holds_body(&player->survival) &&
                           player->hands_held_off == 0u,
                       hit->result == RAYCAST_RESULT_HIT ? hit->cell : nullptr);
    /* Before the hand is read: a tool that breaks on this blow is no longer what the hands hold. */
    game_attack_tick(game, tick_ns);
    game_hand_from_hotbar(game);
    game_tool_from_hotbar(game);
    const uint32_t before = player->interaction.hand_count;
    const uint64_t mined_before = player->interaction.mined_count;
    const uint64_t eaten = player->survival.eaten_count;
    const uint16_t held_item = inventory_selected(&item->inventory)->item;
    player_interact_fell_tick(player, &game->borrowed_world->edit, game->borrowed_felling, game->borrowed_sounds,
                              tick_ns);
    (void)inventory_selected_take(&item->inventory, before - (uint32_t)player->interaction.hand_count);
    if (player->interaction.mined_count != mined_before) {
        game_tool_after_break(game, held_item);
    }
    game_fell_tick(game);
    if (player->survival.eaten_count != eaten) {
        (void)inventory_selected_take(&item->inventory, 1u);
        /* What a stew or milk came in stays in hand. Each stacks to one, so the slot it left is empty. */
        const uint16_t left = held_item == ITEM_ID_MUSHROOM_STEW ? (uint16_t)ITEM_ID_BOWL
                              : held_item == ITEM_ID_MILK_BUCKET ? (uint16_t)ITEM_ID_BUCKET
                                                                 : (uint16_t)ITEM_ID_NONE;
        if (left != ITEM_ID_NONE) {
            *inventory_slot(&item->inventory, item->inventory.selected) = item_stack_make(left, 1u);
            item->inventory.added_count++;
        }
    }
}

/* What a broken chest or furnace held comes out where it stood. With nothing to lie on, as in a run with no entities, it is gone. */
void game_spill(void *borrowed_game, const item_stack_t *stack, const int32_t cell[3]) {
    game_t *const game = borrowed_game;
    if (game->borrowed_entity != nullptr) {
        const float64_t middle[3] = {(float64_t)cell[0] + 0.5f64, (float64_t)cell[1] + 0.5f64,
                                     (float64_t)cell[2] + 0.5f64};
        item_drop_spawn_worn(&game->borrowed_entity->item_drops, item_stack_drop_row(stack->item), stack->wear,
                             stack->count, middle);
    }
}

static void game_drops_spawn(game_t *game) {
    player_drop_t drop;
    while (player_interaction_drop_next(&game->borrowed_player->interaction, &drop)) {
        item_drop_spawn(&game->borrowed_entity->item_drops, drop.item, drop.count, drop.position);
    }
}

/* An arrow has hit the player: armor and a moment's invulnerability are the body's own business, and a blow it did not feel moves it not. */
static void game_arrow_hurt(void *borrowed_context, float32_t amount, const float64_t direction[COLLISION_AXES]) {
    player_t *const player = ((game_t *)borrowed_context)->borrowed_player;
    if (player_survival_hurt(&player->survival, amount, PLAYER_SURVIVAL_CAUSE_ATTACK) > 0.0f32) {
        projectile_knockback(player->movement.velocity, direction, player->movement.on_ground != 0u);
    }
}

/* A body that is down is no more to be aimed at than to be hit. */
static bool game_player_is_a_target(const player_t *player) {
    return player->survival.state != PLAYER_SURVIVAL_DEAD && player->survival.placing == 0u;
}

static float64_t game_player_height(const player_t *player) {
    return player->movement.crouched != 0u ? PLAYER_MOVEMENT_HEIGHT_CROUCHED : PLAYER_MOVEMENT_HEIGHT_STANDING;
}

/* The skeletons think about where the player is now, and shoot. Before the bodies move, as their walking is a wish for this tick. */
static void game_skeletons_tick(game_t *game) {
    if (game->borrowed_mob == nullptr) {
        return;
    }
    const player_t *const player = game->borrowed_player;
    const player_movement_t *const movement = &player->movement;
    const mob_skeleton_view_t view = {
        .position = {movement->position[0], movement->position[1], movement->position[2]},
        .eye_height = movement->eye_height,
        .height = game_player_height(player),
        .alive = (uint8_t)(game_player_is_a_target(player) ? 1u : 0u),
    };
    mob_skeleton_tick(&game->borrowed_mob->skeleton, &game->borrowed_mob->path, game->borrowed_entity,
                      &game->borrowed_world->map, &view);
}

static void game_arrows_tick(game_t *game) {
    entity_t *const entity = game->borrowed_entity;
    const player_t *const player = game->borrowed_player;
    collision_box_t box;
    player_movement_box(player->movement.position, game_player_height(player), &box);
    projectile_tick(&entity->projectiles, &entity->store, &entity->grid, &game->borrowed_world->map,
                    game_player_is_a_target(player) ? &box : nullptr, game_arrow_hurt, game);
}

/* What the hands just broke, and what went by itself, comes out into the world, everything there moves, and the body picks up what it
 * now reaches. One block at a time out of the tick's ring: all of it at once could be more than the hands' ring holds. */
static void game_items_tick(game_t *game) {
    entity_t *const entity = game->borrowed_entity;
    player_t *const player = game->borrowed_player;
    game_drops_spawn(game);
    world_tick_broken_t broken;
    while (world_tick_broken_next(&game->borrowed_world->tick, &broken)) {
        player_interaction_drops_spawn(&player->interaction, broken.block, broken.x, broken.y, broken.z);
        game_drops_spawn(game);
    }
    /* Before the tick, so that the grid it builds knows what came and went. */
    /* The world clock counts days from the midnight before its first morning, so day 0 is the first day, when herds come near. */
    const calendar_clock_t clock = atomic_load_explicit(&game->clock_units, memory_order_relaxed);
    const mob_spawn_view_t view = {
        .position = {player->movement.position[0], player->movement.position[1], player->movement.position[2]},
        .sun_height = player->survival.sun_height,
        .biomes = game->borrowed_terrain != nullptr ? game->borrowed_terrain->borrowed_biome : nullptr,
        .first_day = (uint8_t)(clock < CALENDAR_CLOCK_DAY ? 1u : 0u),
    };
    mob_spawn_tick(&game->mob_spawn, &entity->store, entity->borrowed_physics, &game->borrowed_world->map, &view);
    game_skeletons_tick(game);
    if (game->borrowed_mob != nullptr) {
        const mob_creeper_scene_t scene = {
            .borrowed_entity = entity,
            .borrowed_path = &game->borrowed_mob->path,
            .borrowed_edit = &game->borrowed_world->edit,
            .borrowed_movement = &player->movement,
            .borrowed_survival = &player->survival,
        };
        mob_creeper_tick(&game->borrowed_mob->creeper, &scene);
    }
    entity_tick(entity, &game->borrowed_world->map);
    if (game->borrowed_mob != nullptr) {
        mob_combat_tick(&game->borrowed_mob->combat, &entity->store);
    }
    game_arrows_tick(game);
    collision_box_t reach;
    player_pickup_reach(player, &reach);
    /* A dead body takes nothing, or it would take back what it just spilled. */
    if (!player_survival_holds_body(&player->survival)) {
        (void)item_drop_pickup(&entity->item_drops, &reach, game_pickup_take, game);
    }
    /* What was just picked up may be what the hand holds, and whoever looks before the next tick should see so. */
    if (game->borrowed_item != nullptr) {
        game_hand_from_hotbar(game);
    }
}

/* A body that has died leaves what it carried where it lies, what it wore and what was in its crafting grids too, each as worn as it was.
 * With no inventory there is nothing to leave but the one-slot hand of a test, which it keeps. */
static void game_death_spill(game_t *game) {
    item_t *const item = game->borrowed_item;
    if (item == nullptr) {
        return;
    }
    const player_movement_t *const movement = &game->borrowed_player->movement;
    const int32_t cell[3] = {(int32_t)floor(movement->position[0]), (int32_t)floor(movement->position[1]),
                             (int32_t)floor(movement->position[2])};
    for (uint32_t slot = 0u; slot < INVENTORY_SLOTS; slot++) {
        const item_stack_t *const stack = inventory_slot_read(&item->inventory, slot);
        if (!item_stack_is_empty(stack)) {
            item_stack_t out = {.item = ITEM_ID_NONE, .wear = 0u, .count = 0u, ._pad0 = {0}};
            (void)inventory_slot_move_out(&item->inventory, slot, &out, stack->count);
            game_spill(game, &out, cell);
        }
    }
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        item_stack_t *const stack = &item->equipment.armor[slot];
        if (!item_stack_is_empty(stack)) {
            game_spill(game, stack, cell);
            *stack = (item_stack_t){0};
        }
    }
    crafting_grid_t *const grids[3] = {&item->crafting_hands, &item->crafting_table, item->borrowed_crafting_bench};
    for (uint32_t g = 0u; g < 3u; g++) {
        for (uint32_t c = 0u; c < crafting_grid_cells(grids[g]); c++) {
            item_stack_t *const stack = crafting_grid_cell(grids[g], c);
            if (!item_stack_is_empty(stack)) {
                game_spill(game, stack, cell);
                memset(stack, 0, sizeof(*stack));
            }
        }
        crafting_grid_changed(grids[g]);
    }
}

/* One tick of what keeps the body going, and what that means for everything else. */
static void game_survive_tick(game_t *game) {
    player_t *const player = game->borrowed_player;
    item_t *const item = game->borrowed_item;
    if (item != nullptr) {
        player_survival_armor_set(&player->survival, (float32_t)equipment_armor_points(&item->equipment),
                                  (float32_t)equipment_armor_toughness(&item->equipment));
    }
    player_survive_tick(player, &game->borrowed_world->map);
    const float32_t armor_hit = player_survival_armor_hit_take(&player->survival);
    if (item != nullptr && armor_hit > 0.0f32) {
        (void)equipment_armor_hurt(&item->equipment, armor_hit);
    }
    if (player_survival_died_take(&player->survival)) {
        /* What a screen had out goes back first, so that it comes out with the rest. */
        if (game_has_screens(game)) {
            ui_screen_sim_t sim;
            game_screen_sim(game, &sim);
            ui_screen_sim_close(game->borrowed_ui, &sim);
        }
        game_death_spill(game);
    }
    if (player_survival_morning_take(&player->survival)) {
        atomic_store_explicit(&game->morning_latch, 1u, memory_order_relaxed);
    }
}

/* The light in the air at a place. A column not loaded holds none, and whatever is there is not drawn. */
static uint8_t game_stored_light(const world_map_t *map, const float64_t position[3]) {
    uint8_t stored = SHADE_LIGHT_OPEN_SKY;
    if (world_map_light_get(map, (int32_t)floor(position[0]), (int32_t)floor(position[1]), (int32_t)floor(position[2]),
                            &stored) != WORLD_MAP_STATUS_OK) {
        stored = SHADE_LIGHT_OPEN_SKY;
    }
    return stored;
}

/* Every mob, as its parts stand now: one box for each, with its share of the mob's skin. A body waiting for its column is not there. */
static void game_mobs_publish(game_t *game, render_entities_t *entities, const world_map_t *map) {
    const entity_store_t *const store = &game->borrowed_entity->store;
    const mob_t *const mob = game->borrowed_mob;
    for (uint32_t i = 0u; i < store->count; i++) {
        const uint16_t kind = store->borrowed_kind[i];
        if (kind == (uint16_t)MOB_ID_NONE || (uint32_t)kind >= MOB_COUNT ||
            (store->borrowed_flags[i] & ENTITY_STORE_FLAG_WAITING) != 0u) {
            continue;
        }
        const float64_t middle[3] = {store->borrowed_position[0][i], store->borrowed_position[1][i],
                                     store->borrowed_position[2][i] + (float64_t)store->borrowed_height[i] * 0.5f64};
        const uint8_t stored = game_stored_light(map, middle);
        /* Its own boxes and its sex's, as config/mobs/ has them, at its sex's size. */
        entity_animation_pose_t poses[ENTITY_ANIMATION_PARTS_MAX];
        const entity_animation_part_t *rows[ENTITY_ANIMATION_PARTS_MAX];
        const uint32_t parts = entity_animation_body_pose(store, i, poses, rows);
        /* A sheep's wool is drawn while it can be shorn, and a shorn one shows its skin until it grows back. */
        const bool fleece = mob == nullptr || mob->passive.borrowed_bodies == nullptr ||
                            mob_defs[kind].mind != MOB_MIND_PASSIVE ||
                            mob_passive_fleece_shown(&mob->passive, store, i);
        for (uint32_t p = 0u; p < parts; p++) {
            const entity_animation_part_t *const part = rows[p];
            if (!fleece && (part->flags & MOB_PART_FLEECE) != 0u) {
                continue;
            }
            render_entities_add_mob_part(entities, kind, poses[p].center, poses[p].half, poses[p].yaw, poses[p].pitch,
                                         part->texture_u, part->texture_v, part->size, stored);
        }
    }
}

/* Plan 005 phase 10: where the block in hand would go down, and as what, from the same plan the next tick's right button places by, so the
 * two cannot disagree. Blue if it would go, red if it would not. Every cell the plan gives, whatever gives them: a bench's two halves, a
 * door's, and any cell a later plan adds. */
static void game_preview_publish(game_t *game, render_entities_t *entities, const player_t *player) {
    static_assert((uint32_t)PLAYER_PLACE_CELLS <= (uint32_t)ENTITY_RAYS_PREVIEW_CELLS,
                  "a plan's cells must all fit in the preview");
    player_place_plan_t plan;
    if (player_place_preview(player, &game->borrowed_world->map, &plan) == PLAYER_PLACE_NONE) {
        return;
    }
    render_entities_preview_t preview = {.count = plan.count, .blocked = plan.status == PLAYER_PLACE_BLOCKED ? 1u : 0u};
    for (uint32_t c = 0u; c < plan.count && c < PLAYER_PLACE_CELLS; c++) {
        memcpy(preview.cells[c], plan.cells[c], sizeof(preview.cells[c]));
        preview.voxels[c] = plan.voxels[c];
    }
    render_entities_preview_set(entities, &preview);
}

/* What the next frames draw: every item as it now lies, what is aimed at and how far along mining it is, and where the block in hand would
 * go. Nobody draws anything before a frame is open, and with neither a body nor items there is nothing to say. */
static void game_entities_publish(game_t *game) {
    render_entities_t *const entities = game->borrowed_render->borrowed_entities;
    if (entities == nullptr || (game->borrowed_entity == nullptr && game->borrowed_player == nullptr)) {
        return;
    }
    render_entities_begin(entities);
    if (game->borrowed_entity != nullptr) {
        const world_map_t *const map = &game->borrowed_world->map;
        const item_drop_store_t *const drops = &game->borrowed_entity->item_drops;
        for (uint32_t i = 0u; i < drops->count; i++) {
            const item_drop_t *const drop = &drops->borrowed_items[i];
            render_entities_add_item_drop(entities, drop->item, drop->position, (float32_t)ITEM_DROP_SIZE, drop->age,
                                          game_stored_light(map, drop->position));
        }
        /* An arrow is drawn as the item it is, not turning. */
        const projectile_store_t *const arrows = &game->borrowed_entity->projectiles;
        for (uint32_t i = 0u; i < arrows->count; i++) {
            const projectile_t *const arrow = &arrows->borrowed_items[i];
            render_entities_add_item_drop(entities, item_stack_drop_row(ITEM_ID_ARROW), arrow->position,
                                          (float32_t)ITEM_DROP_SIZE, 0u, game_stored_light(map, arrow->position));
        }
        game_mobs_publish(game, entities, map);
    }
    if (game->borrowed_player != nullptr) {
        const player_t *const player = game->borrowed_player;
        render_entities_target_t target;
        memset(&target, 0, sizeof(target));
        const raycast_hit_t *const hit = &player->target.hit;
        if (hit->result == RAYCAST_RESULT_HIT) {
            target.outline = 1u;
            memcpy(target.box_min, hit->bounds.min, sizeof(target.box_min));
            memcpy(target.box_max, hit->bounds.max, sizeof(target.box_max));
        }
        target.crack_frame = render_entities_crack_frame(player_interaction_progress(&player->interaction));
        memcpy(target.crack_cell, player->interaction.cell, sizeof(target.crack_cell));
        render_entities_target_set(entities, &target);
        game_preview_publish(game, entities, player);
    }
    render_entities_publish(entities);
}

/* A screen whose block has gone goes with it, and with no one asking main is told how things are, as a furnace burns. */
static void game_screen_tick(game_t *game) {
    ui_t *const ui = game->borrowed_ui;
    ui_screen_sim_t sim;
    game_screen_sim(game, &sim);
    const ui_screen_kind_t kind = ui_screen_sim_kind(ui);
    if (kind != UI_SCREEN_NONE && kind != UI_SCREEN_INVENTORY) {
        const int32_t *const cell = ui_screen_sim_cell(ui);
        uint16_t block = BLOCK_ID_AIR;
        if (world_map_block_get(&game->borrowed_world->map, cell[0], cell[1], cell[2], &block) != WORLD_MAP_STATUS_OK ||
            ui_screen_kind_of_block(block) != kind) {
            ui_screen_sim_close(ui, &sim);
        }
    }
    ui_screen_sim_step(ui, &sim, true);
}

void game_hud_position_publish(game_t *game) {
    if (game->borrowed_player == nullptr) {
        return;
    }
    const float64_t *const position = game->borrowed_player->movement.position;
    atomic_store_explicit(&game->hud_block[0], (int32_t)floor(position[0]), memory_order_relaxed);
    atomic_store_explicit(&game->hud_block[1], (int32_t)floor(position[1]), memory_order_relaxed);
    atomic_store_explicit(&game->hud_block[2], (int32_t)floor(position[2]) - MC_WORLD_Z_SEA, memory_order_relaxed);
}

/* What the HUD shows of the inventory goes to main as it stands after this tick. */
static void game_hud_publish(game_t *game) {
    game_hud_position_publish(game);
    /* Plan 004 phase 10: the corner's climate line and storm warning, each as often as it says. */
    game_hud_climate_publish(game, false);
    if (game->borrowed_item == nullptr || game->borrowed_item->inventory.borrowed_slots == nullptr) {
        return;
    }
    const inventory_t *const inventory = &game->borrowed_item->inventory;
    for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
        uint64_t word = 0u;
        memcpy(&word, inventory_slot_read(inventory, slot), sizeof(word));
        atomic_store_explicit(&game->hud_hotbar[slot], word, memory_order_relaxed);
    }
    atomic_store_explicit(&game->hud_selected, inventory->selected, memory_order_relaxed);
}

/* What every mob does, and what comes of it for the body. Before the bodies move and before the body's own tick, whose armor and death
 * logic then see the blow. */
static void game_mobs_tick(game_t *game) {
    player_t *const player = game->borrowed_player;
    const player_movement_t *const movement = &player->movement;
    const mob_zombie_view_t view = {
        .position = {movement->position[0], movement->position[1], movement->position[2]},
        .eye = movement->eye_height,
        .half_width = PLAYER_MOVEMENT_WIDTH * 0.5f64,
        .height = movement->crouched != 0u ? PLAYER_MOVEMENT_HEIGHT_CROUCHED : PLAYER_MOVEMENT_HEIGHT_STANDING,
        .alive = player->survival.state != PLAYER_SURVIVAL_DEAD && player->survival.placing == 0u ? 1u : 0u,
    };
    const float32_t blow = mob_minds_tick(game->borrowed_mob, game->borrowed_entity, &view);
    if (blow > 0.0f32) {
        (void)player_survival_hurt(&player->survival, blow, PLAYER_SURVIVAL_CAUSE_ATTACK);
    }
}

/* Plan 004 section 4.2: has the week number gone up since the last census? Never "is it 06:00 this tick", so a tick just past
 * 06:00, a night slept through and a jump of many days each run every week they passed, once, in order. True if a census has
 * just stopped the world for its screen, and this tick must not run. */
static bool game_census_check(game_t *game) {
    const calendar_clock_t clock = atomic_load_explicit(&game->clock_units, memory_order_relaxed);
    const uint64_t week = calendar_week_of(game->day_zero, clock);
    if (week <= game->last_census_week) {
        return false;
    }
    const calendar_census_plan_t plan = calendar_census_plan(game->last_census_week, week);
    for (uint64_t w = plan.first_week; w <= plan.last_week; w++) {
        if (game->census_week != nullptr) {
            game->census_week(game->borrowed_census_context, w, w < plan.first_week + plan.fast_weeks);
        }
        game->census_runs++;
    }
    /* Before the screen is answered: a save while it is up already holds this week, so being killed then cannot run it twice. */
    game->last_census_week = week;
    LOG_INFO("calendar: census for week %" PRIu64 " (%" PRIu64 " weeks, %" PRIu64 " of them fast)", week, plan.count,
             plan.fast_weeks);
    if (game->census_screens == 0u) {
        return false; /* nobody to show it to: the world goes on */
    }
    atomic_store_explicit(&game->paused, 1u, memory_order_relaxed);
    atomic_store_explicit(&game->census_latch, week + 1u, memory_order_release);
    return true;
}

bool game_census_take(game_t *game, uint64_t *out_week) {
    const uint64_t latch = atomic_exchange_explicit(&game->census_latch, 0u, memory_order_acquire);
    if (latch == 0u) {
        return false;
    }
    *out_week = latch - 1u;
    return true;
}

/* Plan 005 phase 7: the time since mark_ns goes to a part of the tick, and the next part starts now. */
static void game_tick_lap(int64_t *mark_ns, int64_t stage_ns[GAME_WATCH_TICK_STAGES], game_watch_tick_stage_t stage) {
    const int64_t now_ns = time_now_ns();
    stage_ns[stage] += now_ns - *mark_ns;
    *mark_ns = now_ns;
}

/* What the tick took, part by part, and what is in the world after it, for the overlay and the log. */
static void game_tick_watch(game_t *game, const int64_t stage_ns[GAME_WATCH_TICK_STAGES], int64_t now_ns) {
    game_watch_t *const watch = game_watch_of(game);
    if (watch == nullptr) {
        return;
    }
    const entity_t *const entity = game->borrowed_entity;
    game_watch_tick_note(watch, stage_ns, game->tick_count, entity != nullptr ? entity->store.count : 0u,
                         entity != nullptr ? entity->item_drops.count : 0u, now_ns);
}

/* One tick of simulated time, due at tick_ns: this is where Stages 11 to 15 add theirs. */
void game_sim_tick(game_t *game, int64_t tick_ns) {
    profiler_lane_t *const lane = game->borrowed_lane_sim;
    PROFILER_BEGIN(lane, "sim_tick");
    arena_reset(&game->arena_tick);
    if (game_census_check(game)) {
        PROFILER_END(lane);
        return; /* this tick never happened: tick_count stays */
    }
    int64_t stage_ns[GAME_WATCH_TICK_STAGES] = {0};
    int64_t mark_ns = time_now_ns();
    /* Plan 004 phase 08: the weather where the eye is, before the body moves in its wind. */
    game_weather_tick(game, tick_ns);
    if (game->borrowed_player != nullptr) {
        /* The sun at the body's own feet, from the clock main last said: beds and mobs go by how dark it really is there. */
        player_survival_sun_set(
            &game->borrowed_player->survival,
            climate_sun_height_of_clock(game->day_zero, atomic_load_explicit(&game->clock_units, memory_order_relaxed),
                                        game->borrowed_player->movement.position[1]));
        game_tick_lap(&mark_ns, stage_ns, GAME_WATCH_TICK_WEATHER);
        PROFILER_BEGIN(lane, "tick_hands");
        /* First, so that the body and its next aim both meet the world as the hands leave it. */
        game_interact_tick(game, tick_ns);
        game_sounds_tick(game);
        if (game_has_screens(game)) {
            game_screen_tick(game);
        }
        if (game->borrowed_item != nullptr) {
            item_world_tick(game->borrowed_item, &game->borrowed_world->edit, game_spill, game);
        }
        PROFILER_END(lane);
        game_tick_lap(&mark_ns, stage_ns, GAME_WATCH_TICK_HANDS);
        PROFILER_BEGIN(lane, "tick_mobs");
        /* Before the bodies move, so that what it answers is of the world as the hands left it. */
        if (game->borrowed_mob != nullptr) {
            mob_tick(game->borrowed_mob);
        }
        if (game->borrowed_entity != nullptr) {
            if (game->borrowed_mob != nullptr) {
                game_mobs_tick(game);
            }
            game_items_tick(game);
        }
        PROFILER_END(lane);
        game_tick_lap(&mark_ns, stage_ns, GAME_WATCH_TICK_MOBS);
        PROFILER_BEGIN(lane, "tick_player");
        float32_t wind[2];
        game_weather_wind(game, wind);
        player_tick_wind(game->borrowed_player, &game->borrowed_world->map, tick_ns, wind[0], wind[1]);
        game_survive_tick(game);
        PROFILER_END(lane);
        game_tick_lap(&mark_ns, stage_ns, GAME_WATCH_TICK_PLAYER);
    } else {
        game_tick_lap(&mark_ns, stage_ns, GAME_WATCH_TICK_WEATHER);
    }
    PROFILER_BEGIN(lane, "tick_world");
    world_tick_step(&game->borrowed_world->tick);
    PROFILER_END(lane);
    game_tick_lap(&mark_ns, stage_ns, GAME_WATCH_TICK_WORLD);
    game_entities_publish(game);
    game_hud_publish(game);
    game->tick_count++;
    game_far_capture_tick(game);
    game_tick_lap(&mark_ns, stage_ns, GAME_WATCH_TICK_PUBLISH);
    game_tick_watch(game, stage_ns, mark_ns);
    PROFILER_END(lane);
}
