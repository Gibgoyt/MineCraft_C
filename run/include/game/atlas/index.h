/* See run/src/game/atlas/README.md. */
#ifndef MC_RUN_GAME_ATLAS_INDEX_H
#define MC_RUN_GAME_ATLAS_INDEX_H

#include "game/index.h"
#include "input/index.h"
#include "ui/atlas/index.h"

#include <stdint.h>

/* Plan 004 phase 09: the game's side of the Atlas, kept out of game/index.c: opening its store with the world, what the
 * world tells it each frame, its keys, and drawing it in place of the HUD. */

/* The game's Atlas, or nullptr with no ui. */
ui_atlas_t *game_atlas(const game_t *game);
/* As a world opens, after everything else: the store, with <world>/atlas if the world has files, else nothing on disk. */
void game_atlas_open(game_t *game);
/* As it closes, with no thread running: every dirty tile written. */
void game_atlas_close(game_t *game);
/* What the world is now, for the Atlas: main's. */
void game_atlas_world(const game_t *game, ui_atlas_world_t *out);
/* Main: one poll's input. True if the Atlas is up after it and has the input. */
bool game_atlas_input(game_t *game, const input_frame_t *frame);
/* Whether it is up. */
bool game_atlas_up(const game_t *game);
/* Main: the Atlas drawn over a copy of picture, the F3 overlay over it. What to show; picture itself if it cannot. */
const uint8_t *game_atlas_compose(game_t *game, const uint8_t *picture);

#endif
