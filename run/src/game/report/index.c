/* See README.md. */
#include "game/index.h"

#include "climate/index.h"
#include "climate/sun/index.h"
#include "game/fill/index.h"
#include "log/index.h"
#include "render/entities/index.h"
#include "tables/blocks/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

void game_hud_state_read(const game_t *game, ui_hud_state_t *out) {
    memset(out, 0, sizeof(*out));
    for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
        const uint64_t word = atomic_load_explicit(&game->hud_hotbar[slot], memory_order_relaxed);
        memcpy(&out->hotbar[slot], &word, sizeof(word));
    }
    out->selected = (uint8_t)atomic_load_explicit(&game->hud_selected, memory_order_relaxed);
    if (game->borrowed_player != nullptr) {
        const player_vitals_t vitals = player_vitals_read(game->borrowed_player);
        out->health = (uint8_t)ui_hud_half_units(vitals.health, PLAYER_SURVIVAL_HEALTH_MAX);
        out->food = (uint8_t)ui_hud_half_units((float32_t)vitals.food, PLAYER_SURVIVAL_FOOD_MAX);
        out->bubbles = (uint8_t)ui_hud_bubbles(vitals.air, PLAYER_SURVIVAL_AIR_MAX);
        /* A tick under water always leaves it short of the most, and a tick out of it reaches the most only when full again. So this is "under water or
         * still refilling", with nothing more to send. A dead body's air stands still until it comes back, and means nothing. */
        out->bubbles_shown = vitals.air < PLAYER_SURVIVAL_AIR_MAX && vitals.state != (uint8_t)PLAYER_SURVIVAL_DEAD;
        /* What the sim last told survival is worn. A dead body has spilled it, and the next tick says 0. */
        out->armor = (uint8_t)player_armor_points_read(game->borrowed_player);
    }
}

void game_hud_block_read(const game_t *game, int32_t out[3]) {
    if (game->borrowed_player != nullptr) {
        for (uint32_t axis = 0u; axis < 3u; axis++) {
            out[axis] = atomic_load_explicit(&game->hud_block[axis], memory_order_relaxed);
        }
        return;
    }
    /* Flying, there is no sim to ask: the eye is main's own. */
    const float64_t *const position = game->camera.position;
    out[0] = (int32_t)floor(position[0]);
    out[1] = (int32_t)floor(position[1]);
    out[2] = (int32_t)floor(position[2]) - MC_WORLD_Z_SEA;
}

calendar_date_t game_calendar_date(const game_t *game) {
    return calendar_date_of(game->day_zero, atomic_load_explicit(&game->clock_units, memory_order_relaxed));
}

/* The latitude of the block the HUD shows: the body's feet, or flying the eye. */
static float64_t game_calendar_latitude(const game_t *game) {
    int32_t block[3];
    game_hud_block_read(game, block);
    return climate_latitude_deg((float64_t)block[1]);
}

bool game_calendar_south(const game_t *game) {
    /* By the latitude, not by y: past y = 76,596 the north is behind you (plan 004 phase 02). */
    return game_calendar_latitude(game) < 0.0f64;
}

/* Master plan 4.1's third row: no sunrise or no sunset today, by the same continuous day the sun is drawn by. nullptr on other
 * days. */
static const char *game_calendar_polar(const game_t *game, calendar_clock_t clock, float64_t latitude) {
    const float64_t day_hours =
        climate_sun_day_hours(latitude, climate_sun_declination_deg(calendar_year_position(game->day_zero, clock)));
    if (day_hours <= 0.0f64) {
        return "Polar night";
    }
    return day_hours >= 24.0f64 ? "Midnight sun" : nullptr;
}

void game_calendar_line(const game_t *game, char *out, uint64_t bytes) {
    const calendar_clock_t clock = atomic_load_explicit(&game->clock_units, memory_order_relaxed);
    const calendar_date_t date = calendar_date_of(game->day_zero, clock);
    const float64_t latitude = game_calendar_latitude(game);
    const char *const polar = game_calendar_polar(game, clock, latitude);
    if (polar != nullptr && bytes != 0u) {
        const int32_t written =
            snprintf(out, bytes, "Year %u, Day %u, %s", (uint32_t)date.year, (uint32_t)date.day_of_year + 1u, polar);
        if (written < 0) {
            out[0] = '\0';
        }
        return;
    }
    (void)calendar_date_line(&date, calendar_season_temperate(date.day_of_year, latitude < 0.0f64), out, bytes);
}

const char *game_calendar_season_name(const game_t *game) {
    const calendar_clock_t clock = atomic_load_explicit(&game->clock_units, memory_order_relaxed);
    const calendar_date_t date = calendar_date_of(game->day_zero, clock);
    const float64_t latitude = game_calendar_latitude(game);
    const char *const polar = game_calendar_polar(game, clock, latitude);
    return polar != nullptr ? polar
                            : calendar_season_name(calendar_season_temperate(date.day_of_year, latitude < 0.0f64));
}

static float32_t game_hud_ms(uint32_t microseconds) {
    return (float32_t)microseconds / 1000.0f32;
}

void game_hud_debug_read(const game_t *game, ui_hud_debug_t *out) {
    memset(out, 0, sizeof(*out));
    out->fps = game->hud_fps;
    for (uint32_t pass = 0u; pass < FRAME_PASS_COUNT; pass++) {
        out->stage_ms[pass] = game_hud_ms(atomic_load_explicit(&game->hud_pass_us[pass], memory_order_relaxed));
    }
    out->stage_ms[UI_HUD_STAGE_GPU_TOTAL] =
        game_hud_ms(atomic_load_explicit(&game->hud_total_us, memory_order_relaxed));
    out->stage_ms[UI_HUD_STAGE_SIM_TICK] = game_hud_ms(atomic_load_explicit(&game->hud_tick_us, memory_order_relaxed));
    out->stage_ms[UI_HUD_STAGE_HUD] = game_hud_ms(game->hud_compose_us);
    out->stage_ms[UI_HUD_STAGE_PRESENT] = game_hud_ms(game->hud_present_us);
    game_hud_block_read(game, out->block);
    /* Main's own eye and the clock main last said: plan 004 phase 02's F3 line. */
    climate_sun_t sun;
    climate_sun_of_clock(game->day_zero, atomic_load_explicit(&game->clock_units, memory_order_relaxed),
                         game->camera.position[1], &sun);
    out->latitude_deg = (float32_t)climate_latitude_deg(game->camera.position[1]);
    out->sun_elevation_deg = (float32_t)sun.elevation_deg;
}

void game_report(game_t *game) {
    if (game_fill_of(game) != nullptr) {
        game_fill_report(game_fill_of(game)); /* plan 005 phase 14: how long the world took to fill in, each time */
    }
    if (game->borrowed_player != nullptr) {
        const player_movement_t *const movement = &game->borrowed_player->movement;
        LOG_INFO("game: the player is at %.3f, %.3f, %.3f after %" PRIu64 " ticks",
                 (libc_double_t)movement->position[0], (libc_double_t)movement->position[1],
                 (libc_double_t)movement->position[2], game->borrowed_player->tick_count);
        const player_survival_t *const survival = &game->borrowed_player->survival;
        LOG_INFO("game: the player has %.1f health, %" PRIu32 " food and %.1f saturation; hurt %" PRIu64
                 " times, died %" PRIu64 ", came back %" PRIu64 ", ate %" PRIu64 " and slept through %" PRIu64
                 " nights",
                 (libc_double_t)survival->health, (uint32_t)survival->food, (libc_double_t)survival->saturation,
                 survival->hurt_count, survival->died_count, survival->respawned_count, survival->eaten_count,
                 survival->slept_count);
        const player_interaction_t *const hands = &game->borrowed_player->interaction;
        LOG_INFO("game: the player mined %" PRIu64 " blocks and placed %" PRIu64
                 "; the last mined was %s, and the hand "
                 "holds %" PRIu32 " of %s",
                 hands->mined_count, hands->placed_count, block_defs[hands->last_mined].name,
                 (uint32_t)hands->hand_count, hands->hand_count != 0u ? block_defs[hands->hand_block].name : "nothing");
        if (game->borrowed_felling != nullptr) {
            LOG_INFO("game: an axe felled %" PRIu64 " logs past what it cut", game->borrowed_felling->felled_count);
        }
    }
    if (game->borrowed_entity != nullptr && game->mob_spawn.borrowed_idle != nullptr) {
        LOG_INFO("game: %" PRIu64 " mobs came, %" PRIu64 " went, and %" PRIu32 " are in the world",
                 game->mob_spawn.spawned_count, game->mob_spawn.despawned_count, game->borrowed_entity->store.count);
        if (game->borrowed_mob != nullptr) {
            const mob_zombie_t *const zombie = &game->borrowed_mob->zombie;
            LOG_INFO("game: zombies struck %" PRIu64 " blows, %" PRIu64 " died and left %" PRIu64 " items",
                     zombie->struck_count, zombie->died_count, zombie->dropped_count);
        }
        if (game->borrowed_mob != nullptr) {
            const mob_skeleton_t *const skeleton = &game->borrowed_mob->skeleton;
            LOG_INFO("game: skeletons loosed %" PRIu64 " arrows, %" PRIu64 " died and left %" PRIu64 " items",
                     (uint64_t)skeleton->shot_count, (uint64_t)skeleton->died_count, (uint64_t)skeleton->dropped_count);
        }
        LOG_INFO("game: %" PRIu32 " blasts broke %" PRIu64 " blocks", game->borrowed_entity->explosion.blast_count,
                 game->borrowed_entity->explosion.broken_count);
    }
    if (game->borrowed_mob != nullptr && game->borrowed_mob->combat.borrowed_bodies != nullptr) {
        LOG_INFO("game: %" PRIu64 " hits landed on mobs, and took the last health of %" PRIu64,
                 game->borrowed_mob->combat.landed_count, game->borrowed_mob->combat.killed_count);
    }
    if (game->borrowed_mob != nullptr && game->borrowed_mob->creeper.borrowed_bodies != nullptr) {
        LOG_INFO("game: %" PRIu64 " creepers burst, and %" PRIu64 " died, leaving %" PRIu64 " items",
                 game->borrowed_mob->creeper.burst_count, game->borrowed_mob->creeper.died_count,
                 game->borrowed_mob->creeper.dropped_count);
    }
    if (game->borrowed_item != nullptr && game->borrowed_item->inventory.borrowed_slots != nullptr) {
        const inventory_t *const inventory = &game->borrowed_item->inventory;
        const item_stack_t *const held = inventory_selected(inventory);
        LOG_INFO("game: the inventory holds %" PRIu64 " items in %" PRIu32 " stacks; %" PRIu64 " added, %" PRIu64
                 " removed, %" PRIu64 " refused; hotbar slot %" PRIu32 " is in hand, with %" PRIu32 " of %s",
                 inventory_items_held(inventory), inventory_stacks_held(inventory), inventory->added_count,
                 inventory->removed_count, inventory->refused_count, (uint32_t)inventory->selected + 1u,
                 (uint32_t)held->count, item_defs[held->item].name);
    }
    if (game->borrowed_item != nullptr && game->borrowed_item->inventory.borrowed_slots != nullptr) {
        const item_t *const item = game->borrowed_item;
        LOG_INFO("game: %" PRIu32 " chests and %" PRIu32 " furnaces stand; %" PRIu64 " items smelted, %" PRIu64
                 " of fuel burned, %" PRIu64 " spilled",
                 item->chests.count, item->furnaces.count, item->furnaces.smelted_count, item->furnaces.burned_count,
                 item->spilled_count);
        LOG_INFO("game: %" PRIu32 " armor points worn; %" PRIu64 " tools and %" PRIu32 " pieces of armor have broken",
                 equipment_armor_points(&item->equipment), item->equipment.tools_broken_count,
                 item->equipment.armor_broken_count);
    }
    if (game->borrowed_ui != nullptr && game->borrowed_player != nullptr && game->borrowed_item != nullptr &&
        game->borrowed_item->inventory.borrowed_slots != nullptr) {
        const ui_screens_t *const screens = game->borrowed_ui->borrowed_screens;
        LOG_INFO("game: screens came up %" PRIu64 " times and took %" PRIu64 " clicks; %" PRIu64
                 " requests were lost, with %" PRIu64 " sent",
                 screens->opened_count, screens->click_count, screens->lost_count, screens->sent_count);
    }
    if (game->borrowed_entity != nullptr) {
        const item_drop_store_t *const drops = &game->borrowed_entity->item_drops;
        LOG_INFO("game: %" PRIu64 " items dropped, %" PRIu64 " picked up, %" PRIu64 " on the ground in %" PRIu32
                 " stacks; %" PRIu64 " stacks merged, %" PRIu64 " items despawned, %" PRIu64 " destroyed, %" PRIu64
                 " lost",
                 drops->dropped_count, drops->picked_up_count, item_drop_on_ground(drops), drops->count,
                 drops->merged_count, drops->despawned_count, drops->destroyed_count, drops->lost_count);
    }
    const render_entities_t *const entities = game->borrowed_render->borrowed_entities;
    if (entities != nullptr && entities->stats.frames != 0u) {
        LOG_INFO("game: %" PRIu64 " frames drew %" PRIu64 " boxes through %" PRIu64 " links; %" PRIu64
                 " out of sight, %" PRIu64 " past the links and %" PRIu64 " past the snapshots",
                 entities->stats.frames, entities->stats.boxes, entities->stats.links, entities->stats.out_of_sight,
                 entities->stats.out_of_links, entities->stats.out_of_snapshots);
    }
    LOG_INFO("game: %" PRIu64 " columns generated, %" PRIu64 " ticks, %" PRIu64
             " sections sent, %.1f MiB of slabs held",
             game->columns_made, game->tick_count, game->borrowed_render->upload.stats.sections_sent,
             (libc_double_t)game->borrowed_render->buffers.slab_bytes / (libc_double_t)(1u << 20));
    /* The threads have no one to tell at the time: nothing can log while the console shows the picture. */
    if (game->borrowed_render->buffers.pool_full_count != 0u) {
        LOG_WARN("game: the section pool was full %" PRIu64 " times, so part of the world was not drawn",
                 (uint64_t)game->borrowed_render->buffers.pool_full_count);
    }
    const render_lod_t *const far = game->borrowed_render->borrowed_lod;
    if (far != nullptr) {
        LOG_INFO("game: far field: %" PRIu32 " levels, %" PRIu64
                 " columns built in %.1f s of workers' time, %.1f MiB sent",
                 far->level_count, far->stats.columns_built, (libc_double_t)((float64_t)far->stats.build_ns * 1e-9f64),
                 (libc_double_t)((float64_t)far->stats.bytes_sent / 1048576.0f64));
        if (far->stats.columns_no_room != 0u) {
            LOG_WARN("game: %" PRIu64 " far columns had no room in their pool", far->stats.columns_no_room);
        }
    }
    /* The flip rate display_frames_run reports counts a picture shown twice as two. This is the rate of new ones. */
    if (game->pictures_shown != 0u && game->loop_ended_ns > game->loop_started_ns) {
        const float64_t seconds = time_ns_to_seconds(game->loop_ended_ns - game->loop_started_ns);
        LOG_INFO("game: %" PRIu64 " new pictures on the screen in %.1f s: %.1f FPS", game->pictures_shown,
                 (libc_double_t)seconds, (libc_double_t)((float64_t)game->pictures_shown / seconds));
    }
    render_frame_report(&game->borrowed_render->frame);
}
