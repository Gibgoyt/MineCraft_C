/* See run/src/player/camera/README.md. */
#ifndef MC_RUN_PLAYER_CAMERA_INDEX_H
#define MC_RUN_PLAYER_CAMERA_INDEX_H

#include "display/flip/index.h"
#include "gpu_bridge/traverse/index.h"
#include "physics/raycast/index.h"
#include "render/frame/index.h"
#include "types/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

/* How far from the eye a player can aim, in blocks: Minecraft's published survival reach. */
#define PLAYER_CAMERA_REACH 4.5f64
/* What the outline's lines are: black, as Minecraft's. Written and never blended, since the buffer may be scanout memory. */
#define PLAYER_CAMERA_OUTLINE_XRGB 0x00000000u
/* A line this close to the eye is cut off there, in blocks along the view. */
#define PLAYER_CAMERA_NEAR 0.05f64

enum : uint32_t {
    PLAYER_CAMERA_EDGES = 12u,
    PLAYER_CAMERA_EDGE_PIECES = 8u, /* the bits of a byte: each edge is seen or not, piece by piece */
};

/* What the player is aiming at, worked out on the sim thread, with the parts of its outline that can be seen from there. */
typedef struct player_target_t {
    raycast_hit_t hit;                         /* 0: result says whether there is anything at all */
    uint8_t edge_visible[PLAYER_CAMERA_EDGES]; /* 80: a bit per piece of each edge */
    uint8_t _pad0[4];                          /* 92 */
} player_target_t;
static_assert(sizeof(player_target_t) == 96);

/* The mouse turns the camera, the same way as flying: right is a smaller yaw, away from the player is a larger pitch. Stops at
 * RENDER_CAMERA_PITCH_LIMIT up and down, and keeps the yaw within a half turn of 0, where float32_t is at its best. */
void player_camera_look(render_camera_t *camera, int64_t mouse_dx, int64_t mouse_dy);

/* The unit vector a yaw and pitch look along: the numbers of render_camera_to_traverse's forward, so that the middle of the
 * picture and the aim are one line. */
void player_camera_direction(float32_t yaw, float32_t pitch, float64_t out[COLLISION_AXES]);

/* Where a world position lands on a camera's picture, in pixels with (0, 0) the top left corner of the first, along the rays
 * traverse casts. False, with out left alone, if it is not in front of the eye. */
bool player_camera_project(const traverse_camera_t *camera, const float64_t point[COLLISION_AXES], float64_t out[2]);

/* The two ends of edge number edge, 0 to PLAYER_CAMERA_EDGES - 1, of a box. */
void player_camera_edge(const collision_box_t *box, uint32_t edge, float64_t out_from[COLLISION_AXES],
                        float64_t out_to[COLLISION_AXES]);

/* What an eye looking this way aims at, within reach, and which pieces of its outline it can see past the block itself and
 * anything else that can be aimed at. Whichever thread holds the map. A hundred short casts, and nothing allocated. */
void player_camera_target(const world_map_t *map, const float64_t eye[COLLISION_AXES], float32_t yaw, float32_t pitch,
                          player_target_t *out);

/* Draws the seen parts of a target's outline over a buffer already holding the picture camera drew, so that the lines sit on
 * the picture's blocks to the pixel. Nothing if there is no target. Only ever writes, so scanout memory costs it nothing. */
void player_camera_outline_draw(const render_camera_t *camera, const player_target_t *target, display_buffer_t *buffer);

#endif
