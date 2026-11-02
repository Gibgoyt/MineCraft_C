/* See run/src/ui/compose/README.md. */
#ifndef MC_RUN_UI_COMPOSE_INDEX_H
#define MC_RUN_UI_COMPOSE_INDEX_H

#include "arena/index.h"
#include "texture/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* The widest canvas, and so the most pixels the row scratch must hold. It is the widest picture display_png writes. */
    UI_CANVAS_MAX_WIDTH = 16384u,
    UI_CANVAS_ROW_BYTES = UI_CANVAS_MAX_WIDTH * 4u,
};

/* In pixels of the canvas, top left first. Empty if either side is 0 or less. May lie partly or wholly off the canvas. */
typedef struct ui_rect_t {
    int32_t x;      /* 0 */
    int32_t y;      /* 4 */
    int32_t width;  /* 8 */
    int32_t height; /* 12 */
} ui_rect_t;
static_assert(sizeof(ui_rect_t) == 16);

/* Pixels to draw from: 0xAARRGGBB, straight alpha, top row first, no gap between rows. */
typedef struct ui_image_t {
    const uint32_t *borrowed_pixels; /* 0 */
    uint32_t width;                  /* 8 */
    uint32_t height;                 /* 12 */
} ui_image_t;
static_assert(sizeof(ui_image_t) == 16);

/* Where drawing lands: 32-bit pixels whose top byte is left alone, as blend_over_batch promises. A frame in ordinary memory,
 * never a dumb buffer, which is too slow to read back. */
typedef struct ui_canvas_t {
    uint8_t *borrowed_pixels; /* 0 */
    uint32_t *borrowed_row;   /* 8: UI_CANVAS_ROW_BYTES, to build a run of source pixels in; on a uint32_t boundary */
    uint64_t pitch;           /* 16: bytes from one row to the next */
    ui_rect_t clip;           /* 24: always inside the canvas; nothing outside it is touched */
    uint32_t width;           /* 40 */
    uint32_t height;          /* 44 */
    uint64_t blend_calls;     /* 48: of the kernel, since ui_canvas_start */
    uint64_t blended_pixels;  /* 56: what those calls were given */
} ui_canvas_t;
static_assert(sizeof(ui_canvas_t) == 64);

/* Points a canvas at pixels, clipped to all of them. False for a size of 0, a width past UI_CANVAS_MAX_WIDTH, a pitch that
 * cannot hold a row or is no whole number of pixels, or memory not on a pixel boundary. */
bool ui_canvas_start(ui_canvas_t *canvas, uint8_t *pixels, uint64_t pitch, uint32_t width, uint32_t height,
                     uint32_t *row);

/* Sets the clip to rect's part of the canvas, and returns the clip it replaces, to put back with this function. */
ui_rect_t ui_canvas_clip_set(ui_canvas_t *canvas, ui_rect_t rect);

/* What the two share. Its width and height are 0 if they share nothing. */
ui_rect_t ui_rect_intersect(ui_rect_t a, ui_rect_t b);

/* Whether a point is inside. */
bool ui_rect_contains(ui_rect_t rect, int32_t x, int32_t y);

/* A rect of this size in the middle of another, rounding toward the top left. */
ui_rect_t ui_rect_center(ui_rect_t outer, int32_t width, int32_t height);

/* Makes a texture's first frame at full size into an image, in memory from arena. False, having logged why, if there is no
 * such texture. */
bool ui_image_load(ui_image_t *image, arena_t *arena, const texture_t *texture, const char *name);

/* Blends count source pixels at (x, y). All of them must lie inside the clip: this is the step the rest is made of, for
 * callers that have already clipped. */
void ui_compose_span(ui_canvas_t *canvas, int32_t x, int32_t y, const uint32_t *source, uint32_t count);

/* One colour, 0xAARRGGBB straight alpha, over a rect. */
void ui_compose_fill(ui_canvas_t *canvas, ui_rect_t rect, uint32_t color);

/* source's part of image, stretched to rect by nearest pixel, over it. source must lie inside the image. */
void ui_compose_image(ui_canvas_t *canvas, const ui_image_t *image, ui_rect_t source, ui_rect_t rect);

/* All of image, stretched to rect. */
void ui_compose_image_whole(ui_canvas_t *canvas, const ui_image_t *image, ui_rect_t rect);

/* An image in nine parts: corners border pixels square drawn scale times their size and never stretched, edges stretched
 * one way, the middle both ways. In a rect too small for two corners, each takes half. border must leave a middle. */
void ui_compose_nine(ui_canvas_t *canvas, const ui_image_t *image, uint32_t border, uint32_t scale, ui_rect_t rect);

#endif
