/* See run/src/game/view/README.md. */
#ifndef MC_RUN_GAME_VIEW_INDEX_H
#define MC_RUN_GAME_VIEW_INDEX_H

#include "game/index.h"
#include "types/index.h"

#include <stdint.h>

/* How far the map must reach, in chunks, for this config: plan 009 phase 04, the level 0 the narrowest field of view (30 degrees) would want
 * at this config's picture and render distance, up to RENDER_LOD_BLOCK_RADIUS_MAX_CHUNKS, or the config's table_chunks if that is more. So a
 * change of field of view never needs a new world. */
uint32_t game_view_table_chunks(const game_config_t *config);

/* What a config comes to, on a table of table_chunks: the bands by the pixel error, the field of view and the picture's rows; level 0's radius,
 * d_1 and a margin (render_lod_block_radius), never more than the table holds; and the far levels by the same bands. The pictures are the
 * config's size; its render scale is the top of plan 008 phase 05's ladder (scale_percent), drawn smaller and brought up on the card, and
 * its floor the ladder's least (floor_percent). The bands are worked out for the rows drawn at the render scale. */
void game_view_work_out(const game_config_t *config, uint32_t table_chunks, game_view_t *out);

/* The fog of a world drawn this far, in blocks, whole a chunk short of what is loaded: the eye moves about inside a chunk before the map
 * follows. */
float32_t game_view_fog_end(uint32_t reach_chunks);

/* The tangent a field of view comes to. 0, or the 70 degrees of the default, is exactly RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT. */
float32_t game_view_half_fov_tangent(uint32_t fov_degrees);

/* Mouse counts at a sensitivity, with what is past a whole count kept in rest for next time, so that a slow hand still turns. At 100 percent or
 * 0 they come back as they went in. */
int64_t game_view_look_scaled(int64_t counts, uint32_t percent, float64_t *rest);

#endif
