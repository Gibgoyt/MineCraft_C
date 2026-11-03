/* See run/src/render/entities/README.md. */
#ifndef MC_RUN_RENDER_ENTITIES_INDEX_H
#define MC_RUN_RENDER_ENTITIES_INDEX_H

#include "arena/index.h"
#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/entity_rays/index.h"
#include "gpu_bridge/shade/index.h"
#include "machine/index.h"
#include "render/frame/index.h"
#include "tables/mobs/index.h"
#include "texture/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* A box nearer than this has first claim on the links, so that what runs out of them is what is far. */
#define RENDER_ENTITIES_NEAR_BLOCKS 16.0f64
/* How far a list reaches past a box, in pixels: the most a frame's jitter moves a ray, and a pixel for its size. */
#define RENDER_ENTITIES_MARGIN_PIXELS 1.5f32
/* What is nearer than this along the view counts as behind the eye: no box a ray can meet is that thin and that near. */
#define RENDER_ENTITIES_NEAR 1e-3f32

/* One box as the sim knows it. */
typedef struct render_entities_item_t {
    float64_t center[3];  /* 0 */
    float32_t half[3];    /* 24 */
    float32_t yaw;        /* 36 */
    float32_t pitch;      /* 40: about its own left axis, before the yaw */
    uint16_t material;    /* 44: ENTITY_RAYS_NO_MATERIAL for none */
    uint16_t texture;     /* 46: with no material: a row of the frame's own table */
    uint16_t skin_u;      /* 48: see entity_rays_box_t */
    uint16_t skin_v;      /* 50 */
    uint8_t stored;       /* 52 */
    uint8_t skin_size[3]; /* 53: all 0 for no skin */
} render_entities_item_t;
static_assert(sizeof(render_entities_item_t) == 56);

/* What the player is aiming at and mining as the sim knows it. */
typedef struct render_entities_target_t {
    float64_t box_min[3];  /* 0 */
    float64_t box_max[3];  /* 24 */
    int32_t crack_cell[3]; /* 48 */
    uint32_t crack_frame;  /* 60: 0 for none, or 1 and on */
    uint8_t outline;       /* 64: whether box_min and box_max mean anything */
    uint8_t _pad0[7];      /* 65 */
} render_entities_target_t;
static_assert(sizeof(render_entities_target_t) == 72);

/* What one sim tick leaves for the frames after it. */
typedef struct render_entities_slot_t {
    render_entities_item_t *borrowed_items; /* 0: ENTITY_RAYS_BOXES_MAX */
    render_entities_target_t target;        /* 8 */
    uint32_t count;                         /* 80 */
    uint8_t _pad0[4];                       /* 84 */
} render_entities_slot_t;
static_assert(sizeof(render_entities_slot_t) == 88);

/* What the frames took. The gpu thread's, but for the last. */
typedef struct render_entities_stats_t {
    uint64_t frames;           /* 0: with at least one box */
    uint64_t boxes;            /* 8: drawn, in all */
    uint64_t links;            /* 16 */
    uint64_t out_of_sight;     /* 24: boxes off the picture or past the fog, that nothing was sent for */
    uint64_t out_of_links;     /* 32: boxes left out of a frame, for want of links */
    uint64_t out_of_snapshots; /* 40: boxes that did not fit in a tick's snapshot: the sim's */
} render_entities_stats_t;
static_assert(sizeof(render_entities_stats_t) == 48);

/* The boxes from the sim to the gpu thread, and each frame's lists on their way to the card. */
typedef struct render_entities_t {
    render_exchange_t exchange;                          /* 0: sim to gpu, over slots */
    render_entities_slot_t slots[RENDER_EXCHANGE_SLOTS]; /* 192 */
    device_memory_t *borrowed_memory;                    /* 456 */
    const device_block_t *borrowed_lists;                /* 464 */
    const device_block_t *borrowed_hits;                 /* 472 */
    entity_rays_box_t *borrowed_boxes;                   /* 480: what goes to the card */
    uint32_t *borrowed_starts;                           /* 488: the same */
    uint16_t *borrowed_links;                            /* 496: the same */
    uint16_t *borrowed_rects;                            /* 504: 4 for each box kept: the tiles it covers */
    uint16_t *borrowed_item_textures;                    /* 512: BLOCK_DROP_ITEM_COUNT rows of the frame's own table */
    render_entities_stats_t stats;                       /* 520 */
    uint32_t width;                                      /* 568 */
    uint32_t height;                                     /* 572 */
    uint32_t reading;                                    /* 576: the gpu thread's: the slot it last took */
    uint16_t mob_textures[MOB_COUNT]; /* 580: each mob's row of the frame's own table; set once, by init */
    uint8_t _pad0[52];                /* 588 */
} render_entities_t;
static_assert(sizeof(render_entities_t) == 640);
static_assert(alignof(render_entities_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(render_entities_t, slots) == 192);
static_assert(offsetof(render_entities_t, borrowed_memory) == 456);
static_assert(offsetof(render_entities_t, stats) == 520);
static_assert(offsetof(render_entities_t, _pad0) == 588);

/* Sizes, for the Owner to take memory and blocks. The lists hold the frame's own textures after everything else. */
uint64_t render_entities_arena_bytes(uint32_t width, uint32_t height);
uint64_t render_entities_lists_bytes(uint32_t width, uint32_t height);

/* Takes what it needs from the arena, and sends the frame's own textures to the card: the crack first, and then each item that is
 * no block, from the blob pixel offsets the scene's pixels already hold. All of it must outlive this. On failure logs why and
 * returns what went wrong. */
gpu_status_t render_entities_init(render_entities_t *entities, arena_t *arena, device_memory_t *memory,
                                  const texture_t *texture, const device_block_t *lists, const device_block_t *hits,
                                  uint32_t width, uint32_t height);

/* Forgets it all. What it took is the arena's, and the blocks are the Owner's. Safe on zero bytes. No kernel may be running. */
void render_entities_deinit(render_entities_t *entities);

/* The sim, or whichever one thread: begin, add each box and say what is aimed at, and publish, which is what the next frames draw. A
 * snapshot that is full counts what is left out. Never waits and never touches the heap. */
void render_entities_begin(render_entities_t *entities);
void render_entities_add(render_entities_t *entities, const render_entities_item_t *item);
/* A dropped item: its box at this place, by what it is, and turning by its age as it lies there. stored is the light around it. */
void render_entities_add_item_drop(render_entities_t *entities, uint16_t item, const float64_t position[3],
                                   float32_t size, uint32_t age, uint8_t stored);
/* One part of a mob's body, by what mob it is, as entity_animation_pose places it, with that part's share of the skin. size is in texels.
 * stored is the light around it. */
void render_entities_add_mob_part(render_entities_t *entities, uint16_t mob, const float64_t center[3],
                                  const float32_t half[3], float32_t yaw, float32_t pitch, uint16_t skin_u,
                                  uint16_t skin_v, const float32_t size[3], uint8_t stored);
void render_entities_target_set(render_entities_t *entities, const render_entities_target_t *target);
void render_entities_publish(render_entities_t *entities);

/* Which frame of the crack, as render_entities_target_t wants it, for how far along mining is, from 0 to 1. */
uint32_t render_entities_crack_frame(float32_t progress);

/* Which pixels a box can touch on a camera, by its middle from the eye, its half size and its yaw: x, y and past the last of each.
 * False if none. margin is in pixels. */
bool render_entities_pixels(const traverse_camera_t *camera, const float32_t center[3], const float32_t half[3],
                            float32_t cos_yaw, float32_t sin_yaw, float32_t margin, uint32_t out[4]);

/* The gpu thread. Makes the newest snapshot this frame's lists, against camera with no jitter on it, sends them to the card and says
 * what the frame's kernels want. With features lacking SHADE_FEATURE_ENTITIES it has no boxes, and with no SHADE_FEATURE_OUTLINE no
 * mark. far is where a box is past seeing, or 0 for never. Allocates nothing. Anything but GPU_STATUS_OK is in memory's last_error. */
gpu_status_t render_entities_frame(render_entities_t *entities, const traverse_camera_t *camera, uint32_t features,
                                   float32_t far, entity_rays_scene_t *out);

#endif
