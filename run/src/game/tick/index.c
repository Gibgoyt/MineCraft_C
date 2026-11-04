/* See README.md. */
#include "game/index.h"

#include "entity/animation/index.h"
#include "item/stack/index.h"
#include "render/entities/index.h"
#include "tables/blocks/index.h"

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
        (void)ui_screen_sim_open(ui, game->borrowed_item, kind, hit->cell);
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

/* One tick of the hands: which slot is in hand first, so that a key and a click in one tick act in that order, then what the right button
 * does with armor or a hoe; what they placed comes off the slot, and what they broke wears it. */
static void game_interact_tick(game_t *game, int64_t tick_ns) {
    player_t *const player = game->borrowed_player;
    item_t *const item = game->borrowed_item;
    if (item == nullptr) {
        game_attack_tick(game, tick_ns);
        player_interact_tick(player, &game->borrowed_world->edit, tick_ns);
        return;
    }
    item_hotbar_apply(item);
    if (game_has_screens(game)) {
        game_screen_hands(game, tick_ns);
    }
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
    player_interact_tick(player, &game->borrowed_world->edit, tick_ns);
    (void)inventory_selected_take(&item->inventory, before - (uint32_t)player->interaction.hand_count);
    if (player->interaction.mined_count != mined_before) {
        game_tool_after_break(game, held_item);
    }
    if (player->survival.eaten_count != eaten) {
        (void)inventory_selected_take(&item->inventory, 1u);
        /* What a stew came in stays in hand. A stew stacks to one, so the slot it left is empty. */
        if (held_item == ITEM_ID_MUSHROOM_STEW) {
            *inventory_slot(&item->inventory, item->inventory.selected) = item_stack_make(ITEM_ID_BOWL, 1u);
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
    const mob_spawn_view_t view = {
        .position = {player->movement.position[0], player->movement.position[1], player->movement.position[2]},
        .day_fraction = player->survival.day_fraction,
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
    crafting_grid_t *const grids[2] = {&item->crafting_hands, &item->crafting_table};
    for (uint32_t g = 0u; g < 2u; g++) {
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
    for (uint32_t i = 0u; i < store->count; i++) {
        const uint16_t kind = store->borrowed_kind[i];
        if (kind == (uint16_t)MOB_ID_NONE || (uint32_t)kind >= MOB_COUNT ||
            (store->borrowed_flags[i] & ENTITY_STORE_FLAG_WAITING) != 0u) {
            continue;
        }
        const entity_animation_rig_t *const rig =
            entity_animation_rig(kind == (uint16_t)MOB_ID_ZOMBIE     ? ENTITY_ANIMATION_RIG_ZOMBIE
                                 : kind == (uint16_t)MOB_ID_SKELETON ? ENTITY_ANIMATION_RIG_SKELETON
                                                                     : ENTITY_ANIMATION_RIG_CREEPER);
        const float64_t feet[3] = {store->borrowed_position[0][i], store->borrowed_position[1][i],
                                   store->borrowed_position[2][i]};
        const float64_t middle[3] = {feet[0], feet[1], feet[2] + (float64_t)store->borrowed_height[i] * 0.5f64};
        const uint8_t stored = game_stored_light(map, middle);
        entity_animation_state_t state;
        entity_animation_state_read(store, i, &state);
        entity_animation_pose_t poses[ENTITY_ANIMATION_PARTS_MAX];
        const uint32_t parts = entity_animation_pose(rig, &state, feet, poses);
        for (uint32_t p = 0u; p < parts; p++) {
            const entity_animation_part_t *const part = &rig->borrowed_parts[p];
            render_entities_add_mob_part(entities, kind, poses[p].center, poses[p].half, poses[p].yaw, poses[p].pitch,
                                         part->texture_u, part->texture_v, part->size, stored);
        }
    }
}

/* What the next frames draw: every item as it now lies, what is aimed at and how far along mining it is. Nobody draws anything before
 * a frame is open, and with neither a body nor items there is nothing to say. */
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

/* One tick of simulated time, due at tick_ns: this is where Stages 11 to 15 add theirs. */
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

void game_sim_tick(game_t *game, int64_t tick_ns) {
    profiler_lane_t *const lane = game->borrowed_lane_sim;
    PROFILER_BEGIN(lane, "sim_tick");
    arena_reset(&game->arena_tick);
    if (game->borrowed_player != nullptr) {
        player_survival_day_set(&game->borrowed_player->survival,
                                (float64_t)atomic_load_explicit(&game->day_units, memory_order_relaxed) /
                                    4294967296.0f64);
        PROFILER_BEGIN(lane, "tick_hands");
        /* First, so that the body and its next aim both meet the world as the hands leave it. */
        game_interact_tick(game, tick_ns);
        if (game_has_screens(game)) {
            game_screen_tick(game);
        }
        if (game->borrowed_item != nullptr) {
            item_world_tick(game->borrowed_item, &game->borrowed_world->edit, game_spill, game);
        }
        PROFILER_END(lane);
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
        PROFILER_BEGIN(lane, "tick_player");
        player_tick(game->borrowed_player, &game->borrowed_world->map, tick_ns);
        game_survive_tick(game);
        PROFILER_END(lane);
    }
    PROFILER_BEGIN(lane, "tick_world");
    world_tick_step(&game->borrowed_world->tick);
    PROFILER_END(lane);
    game_entities_publish(game);
    game_hud_publish(game);
    game->tick_count++;
    PROFILER_END(lane);
}
