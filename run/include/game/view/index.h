/* See run/src/game/view/README.md. */
#ifndef MC_RUN_GAME_VIEW_INDEX_H
#define MC_RUN_GAME_VIEW_INDEX_H

#include "game/index.h"
#include "types/index.h"

#include <stdint.h>

/* How far the map must reach, in chunks, for this config: what it draws block by block, or what it may later, whichever is more. */
uint32_t game_view_table_chunks(const game_config_t *config);

/* What a config comes to, on a table of table_chunks. Full detail is never more than the table holds. */
void game_view_work_out(const game_config_t *config, uint32_t table_chunks, game_view_t *out);

/* The fog of a world drawn this far, in blocks, whole a chunk short of what is loaded: the eye moves about inside a chunk before the map
 * follows. */
float32_t game_view_fog_end(uint32_t reach_chunks);

/* Plan 009 phase 03: the pyramid's levels for a config and what it comes to: banded by the tuning file's pixel error and near guarantee, the
 * field of view and the picture's own height, out to the render distance. Nothing draws them yet. */
render_lod_plan_t game_view_pyramid(const game_config_t *config, const game_view_t *view);

/* The tangent a field of view comes to. 0, or the 70 degrees of the default, is exactly RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT. */
float32_t game_view_half_fov_tangent(uint32_t fov_degrees);

/* Mouse counts at a sensitivity, with what is past a whole count kept in rest for next time, so that a slow hand still turns. At 100 percent or
 * 0 they come back as they went in. */
int64_t game_view_look_scaled(int64_t counts, uint32_t percent, float64_t *rest);

/* A picture of 4 bytes a pixel, its rows side by side, brought up to a larger one by the nearest pixel. A row that repeats is a copy. */
void game_view_bring_up(const uint8_t *from, uint32_t from_width, uint32_t from_height, uint8_t *to, uint32_t to_width,
                        uint32_t to_height);

#endif
