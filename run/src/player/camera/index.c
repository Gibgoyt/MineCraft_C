/* See README.md. Everything a look needs, and the outline. Nothing here allocates. */
#include "player/camera/index.h"

#include "thread/index.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

#define PLAYER_CAMERA_TURN 6.283185307f32
#define PLAYER_CAMERA_HALF_TURN 3.141592654f32
/* A piece of an edge is seen if nothing is in the way up to this short of it: the edge itself lies on the block's own box. */
#define PLAYER_CAMERA_SIGHT_MARGIN 1.0e-5f64

enum : uint32_t {
    PLAYER_CAMERA_AXIS_EDGES = 4u, /* along each of the three axes */
    /* Lines a pixel wide at this many rows, and wider in proportion, so that an outline looks the same at any size. */
    PLAYER_CAMERA_ROWS_PER_PIXEL = 720u,
};

void player_camera_look(render_camera_t *camera, int64_t mouse_dx, int64_t mouse_dy) {
    camera->yaw -= (float32_t)mouse_dx * RENDER_CAMERA_RADIANS_PER_COUNT;
    camera->pitch -= (float32_t)mouse_dy * RENDER_CAMERA_RADIANS_PER_COUNT;
    camera->pitch = camera->pitch > RENDER_CAMERA_PITCH_LIMIT ? RENDER_CAMERA_PITCH_LIMIT : camera->pitch;
    camera->pitch = camera->pitch < -RENDER_CAMERA_PITCH_LIMIT ? -RENDER_CAMERA_PITCH_LIMIT : camera->pitch;
    if (camera->yaw > PLAYER_CAMERA_HALF_TURN || camera->yaw < -PLAYER_CAMERA_HALF_TURN) {
        camera->yaw -= PLAYER_CAMERA_TURN * roundf(camera->yaw / PLAYER_CAMERA_TURN);
    }
}

void player_camera_direction(float32_t yaw, float32_t pitch, float64_t out[COLLISION_AXES]) {
    const float32_t cos_pitch = cosf(pitch);
    out[0] = (float64_t)(cos_pitch * cosf(yaw));
    out[1] = (float64_t)(cos_pitch * sinf(yaw));
    out[2] = (float64_t)sinf(pitch);
}

static float64_t player_camera_dot(const float64_t a[COLLISION_AXES], const float32_t b[COLLISION_AXES]) {
    return a[0] * (float64_t)b[0] + a[1] * (float64_t)b[1] + a[2] * (float64_t)b[2];
}

static float64_t player_camera_length_squared(const float32_t a[COLLISION_AXES]) {
    const float64_t wide[COLLISION_AXES] = {(float64_t)a[0], (float64_t)a[1], (float64_t)a[2]};
    return player_camera_dot(wide, a);
}

/* Where a point is from the eye, with the very numbers the rays start from. */
static void player_camera_from_eye(const traverse_camera_t *camera, const float64_t point[COLLISION_AXES],
                                   float64_t out[COLLISION_AXES]) {
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        out[a] = point[a] - (float64_t)camera->eye_cell[a] - (float64_t)camera->eye_frac[a];
    }
}

/* The other way from a ray: forward, right and up are at right angles, so each dot takes out one of them. */
static void player_camera_screen(const traverse_camera_t *camera, const float64_t from_eye[COLLISION_AXES],
                                 float64_t out[2]) {
    const float64_t along = player_camera_dot(from_eye, camera->forward);
    const float64_t u =
        player_camera_dot(from_eye, camera->right) / (along * player_camera_length_squared(camera->right));
    const float64_t v = player_camera_dot(from_eye, camera->up) / (along * player_camera_length_squared(camera->up));
    out[0] = (u + 1.0f64) * 0.5f64 * (float64_t)camera->width;
    out[1] = (1.0f64 - v) * 0.5f64 * (float64_t)camera->height;
}

bool player_camera_project(const traverse_camera_t *camera, const float64_t point[COLLISION_AXES], float64_t out[2]) {
    float64_t from_eye[COLLISION_AXES];
    player_camera_from_eye(camera, point, from_eye);
    if (player_camera_dot(from_eye, camera->forward) <= 0.0f64) {
        return false;
    }
    player_camera_screen(camera, from_eye, out);
    return true;
}

void player_camera_edge(const collision_box_t *box, uint32_t edge, float64_t out_from[COLLISION_AXES],
                        float64_t out_to[COLLISION_AXES]) {
    const uint32_t axis = edge / PLAYER_CAMERA_AXIS_EDGES;
    const uint32_t sides = edge % PLAYER_CAMERA_AXIS_EDGES;
    const uint32_t first = (axis + 1u) % COLLISION_AXES, second = (axis + 2u) % COLLISION_AXES;
    out_from[axis] = box->min[axis];
    out_to[axis] = box->max[axis];
    out_from[first] = out_to[first] = (sides & 1u) != 0u ? box->max[first] : box->min[first];
    out_from[second] = out_to[second] = (sides & 2u) != 0u ? box->max[second] : box->min[second];
}

static void player_camera_along(const float64_t from[COLLISION_AXES], const float64_t to[COLLISION_AXES],
                                float64_t along, float64_t out[COLLISION_AXES]) {
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        out[a] = from[a] + (to[a] - from[a]) * along;
    }
}

void player_camera_target(const world_map_t *map, const float64_t eye[COLLISION_AXES], float32_t yaw, float32_t pitch,
                          player_target_t *out) {
    memset(out, 0, sizeof(*out));
    float64_t direction[COLLISION_AXES];
    player_camera_direction(yaw, pitch, direction);
    if (raycast_cast(map, eye, direction, PLAYER_CAMERA_REACH, &out->hit) != RAYCAST_RESULT_HIT) {
        return;
    }
    for (uint32_t edge = 0u; edge < PLAYER_CAMERA_EDGES; edge++) {
        float64_t from[COLLISION_AXES], to[COLLISION_AXES];
        player_camera_edge(&out->hit.bounds, edge, from, to);
        for (uint32_t piece = 0u; piece < PLAYER_CAMERA_EDGE_PIECES; piece++) {
            float64_t middle[COLLISION_AXES];
            player_camera_along(from, to, ((float64_t)piece + 0.5f64) / (float64_t)PLAYER_CAMERA_EDGE_PIECES, middle);
            const float64_t sight[COLLISION_AXES] = {middle[0] - eye[0], middle[1] - eye[1], middle[2] - eye[2]};
            const float64_t distance = sqrt(sight[0] * sight[0] + sight[1] * sight[1] + sight[2] * sight[2]);
            const float64_t clear = distance - PLAYER_CAMERA_SIGHT_MARGIN;
            raycast_hit_t blocker;
            /* Right up against the eye, or with nothing between: seen. A column not loaded is nothing that was drawn. */
            if (clear <= 0.0f64 || raycast_cast(map, eye, sight, clear, &blocker) != RAYCAST_RESULT_HIT) {
                out->edge_visible[edge] |= (uint8_t)(1u << piece);
            }
        }
    }
}

/* Cuts a line, from the eye, to where it is at least PLAYER_CAMERA_NEAR ahead. False if none of it is left. */
static bool player_camera_clip_near(const traverse_camera_t *camera, float64_t from[COLLISION_AXES],
                                    float64_t to[COLLISION_AXES]) {
    const float64_t from_along = player_camera_dot(from, camera->forward);
    const float64_t to_along = player_camera_dot(to, camera->forward);
    if (from_along < PLAYER_CAMERA_NEAR && to_along < PLAYER_CAMERA_NEAR) {
        return false;
    }
    if (from_along < PLAYER_CAMERA_NEAR || to_along < PLAYER_CAMERA_NEAR) {
        const float64_t cut = (PLAYER_CAMERA_NEAR - from_along) / (to_along - from_along);
        float64_t point[COLLISION_AXES];
        player_camera_along(from, to, cut, point);
        float64_t *const moved = from_along < PLAYER_CAMERA_NEAR ? from : to;
        moved[0] = point[0];
        moved[1] = point[1];
        moved[2] = point[2];
    }
    return true;
}

/* Liang and Barsky's clip to the picture, so that however long a line is it costs a picture's width at most. */
static bool player_camera_clip_screen(float64_t from[2], float64_t to[2], uint32_t width, uint32_t height) {
    const float64_t dx = to[0] - from[0], dy = to[1] - from[1];
    const float64_t p[4] = {-dx, dx, -dy, dy};
    const float64_t q[4] = {from[0], (float64_t)width - from[0], from[1], (float64_t)height - from[1]};
    float64_t t_from = 0.0f64, t_to = 1.0f64;
    for (uint32_t i = 0u; i < 4u; i++) {
        if (p[i] == 0.0f64) {
            if (q[i] < 0.0f64) {
                return false;
            }
            continue;
        }
        const float64_t t = q[i] / p[i];
        if (p[i] < 0.0f64) {
            t_from = t > t_from ? t : t_from;
        } else {
            t_to = t < t_to ? t : t_to;
        }
    }
    if (t_from > t_to) {
        return false;
    }
    const float64_t x = from[0], y = from[1];
    from[0] = x + dx * t_from;
    from[1] = y + dy * t_from;
    to[0] = x + dx * t_to;
    to[1] = y + dy * t_to;
    return true;
}

static void player_camera_plot(display_buffer_t *buffer, int64_t x, int64_t y, int64_t thickness) {
    const int64_t x_from = x - thickness / 2, y_from = y - thickness / 2;
    for (int64_t row = y_from < 0 ? 0 : y_from; row < y_from + thickness && row < (int64_t)buffer->height; row++) {
        /* A buffer's rows are 4-aligned: XRGB8888. */
        uint32_t *const pixels = (uint32_t *)(void *)(buffer->owned_map + (uint64_t)row * buffer->pitch);
        for (int64_t column = x_from < 0 ? 0 : x_from; column < x_from + thickness && column < (int64_t)buffer->width;
             column++) {
            pixels[column] = PLAYER_CAMERA_OUTLINE_XRGB;
        }
    }
}

static void player_camera_line(display_buffer_t *buffer, const float64_t from[2], const float64_t to[2],
                               int64_t thickness) {
    const float64_t dx = to[0] - from[0], dy = to[1] - from[1];
    const float64_t longest = fabs(dx) > fabs(dy) ? fabs(dx) : fabs(dy);
    const int64_t steps = (int64_t)ceil(longest);
    for (int64_t i = 0; i <= steps; i++) {
        const float64_t along = steps == 0 ? 0.0f64 : (float64_t)i / (float64_t)steps;
        player_camera_plot(buffer, (int64_t)floor(from[0] + dx * along), (int64_t)floor(from[1] + dy * along),
                           thickness);
    }
}

void player_camera_outline_draw(const render_camera_t *camera, const player_target_t *target,
                                display_buffer_t *buffer) {
    if (target->hit.result != RAYCAST_RESULT_HIT) {
        return;
    }
    traverse_camera_t view;
    render_camera_to_traverse(camera, buffer->width, buffer->height, &view);
    const uint32_t wide = (buffer->height + PLAYER_CAMERA_ROWS_PER_PIXEL / 2u) / PLAYER_CAMERA_ROWS_PER_PIXEL;
    const int64_t thickness = wide == 0u ? 1 : (int64_t)wide;
    for (uint32_t edge = 0u; edge < PLAYER_CAMERA_EDGES; edge++) {
        float64_t from[COLLISION_AXES], to[COLLISION_AXES];
        player_camera_edge(&target->hit.bounds, edge, from, to);
        for (uint32_t piece = 0u; piece < PLAYER_CAMERA_EDGE_PIECES; piece++) {
            if ((target->edge_visible[edge] & (1u << piece)) == 0u) {
                continue;
            }
            float64_t world[COLLISION_AXES], piece_from[COLLISION_AXES], piece_to[COLLISION_AXES];
            player_camera_along(from, to, (float64_t)piece / (float64_t)PLAYER_CAMERA_EDGE_PIECES, world);
            player_camera_from_eye(&view, world, piece_from);
            player_camera_along(from, to, (float64_t)(piece + 1u) / (float64_t)PLAYER_CAMERA_EDGE_PIECES, world);
            player_camera_from_eye(&view, world, piece_to);
            if (!player_camera_clip_near(&view, piece_from, piece_to)) {
                continue;
            }
            float64_t screen_from[2], screen_to[2];
            player_camera_screen(&view, piece_from, screen_from);
            player_camera_screen(&view, piece_to, screen_to);
            if (player_camera_clip_screen(screen_from, screen_to, buffer->width, buffer->height)) {
                player_camera_line(buffer, screen_from, screen_to, thickness);
            }
        }
    }
    /* Scanout memory is write-combined: the picture's copy ended with a fence, and these writes are after it. A seq_cst fence is
     * a locked instruction on this machine, which is what empties those buffers. */
    thread_fence_seq_cst();
}
