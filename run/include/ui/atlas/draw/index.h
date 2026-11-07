/* See run/src/ui/atlas/draw/README.md. */
#ifndef MC_RUN_UI_ATLAS_DRAW_INDEX_H
#define MC_RUN_UI_ATLAS_DRAW_INDEX_H

#include "types/index.h"
#include "ui/compose/index.h"
#include "ui/font/index.h"

#include <stdint.h>

/* Plan 004 phase 09: the drawing tools the Atlas's layers share, for what climate/layer does not already draw. */

enum : uint32_t {
    UI_ATLAS_COLOUR_UNGENERATED = 0xFF808080u, /* climate/layer's grey for a tile not built, so the two never differ */
    UI_ATLAS_COLOUR_DARK = 0xFF101010u,        /* the edge a pan uncovers before the new picture lands */
};
#define UI_ATLAS_SHADE_EXAGGERATION 1.0f32 /* ratio: vertical stretch for hill shading, for Ahmed to tune by eye */
#define UI_ATLAS_SHADE_MIN 0.4f32
#define UI_ATLAS_SHADE_MAX 1.3f32
#define UI_ATLAS_CONE_REACH                                                                                            \
    1.1774100225154747f64 /* sqrt(2 ln 2): the 50% footprint's radius over sqrt(sigma^2 + R^2) */

/* A colour key: values rising, colours 0x00RRGGBB, straight-line mixing between, held past the ends. */
typedef struct ui_atlas_scale_t {
    const float32_t *values; /* 0 */
    const uint32_t *colours; /* 8 */
    uint32_t count;          /* 16 */
    uint8_t _pad0[4];        /* 20 */
} ui_atlas_scale_t;
static_assert(sizeof(ui_atlas_scale_t) == 24);

/* One row of a list legend, such as a climate class. */
typedef struct ui_atlas_key_entry_t {
    const char *label; /* 0 */
    uint32_t colour;   /* 8: 0x00RRGGBB */
    uint8_t _pad0[4];  /* 12 */
} ui_atlas_key_entry_t;
static_assert(sizeof(ui_atlas_key_entry_t) == 16);

/* A value along a scale: c = c0 + t (c1 - c0) each channel, rounded half away from zero; NaN takes the first colour. */
uint32_t ui_atlas_scale_colour(const ui_atlas_scale_t *scale, float32_t value);

/* Background, "Hill shading": b = clamp(max(0, n . L) / 0.707, 0.4, 1.3), with n from the four neighbours' heights s blocks
 * apart, stretched by k, and L from the north-west 45 degrees up. Flat ground gives 1. */
float32_t ui_atlas_shade(float32_t west, float32_t east, float32_t north, float32_t south, float32_t blocks_per_pixel,
                         float32_t exaggeration);
/* Each channel of 0x..RRGGBB times b, rounded half away from zero, held at 255; alpha set to 0xFF. */
uint32_t ui_atlas_shade_apply(uint32_t rgb, float32_t b);

/* One straight piece of an isoline, in the square's own pixels: x from its left, y from its top. */
typedef struct ui_atlas_segment_t {
    float32_t x0; /* 0 */
    float32_t y0; /* 4 */
    float32_t x1; /* 8 */
    float32_t y1; /* 12 */
} ui_atlas_segment_t;
static_assert(sizeof(ui_atlas_segment_t) == 16);

/* Marching squares on one square of side size: corners top-left, top-right, bottom-right, bottom-left. A corner counts as
 * above when its value is greater than level. Writes 0, 1 or 2 segments and returns how many. The two saddle cases are
 * settled by the mean of the four corners: above the level, the two above corners are joined. NaN corners give none. */
uint32_t ui_atlas_march(const float32_t corners[4], float32_t level, float32_t size, ui_atlas_segment_t out[2]);

/* Where a mark may land on a picture with a grey mask: always (an outline, a name), or only off grey (fills, arrows). */
typedef enum ui_atlas_mode_t : uint8_t {
    UI_ATLAS_MODE_OUTLINE = 0, /* drawn over ungenerated land too */
    UI_ATLAS_MODE_FILL = 1,    /* only where the land is generated */
    UI_ATLAS_MODE_ON_GREY = 2, /* only over ungenerated land: another layer's outlines */
} ui_atlas_mode_t;

/* A picture being drawn: 0xFFRRGGBB pixels, width a row, and a byte a pixel that is 1 over ungenerated land. */
typedef struct ui_atlas_picture_t {
    uint32_t *borrowed_pixels;    /* 0 */
    const uint8_t *borrowed_grey; /* 8: or nullptr: nothing is grey */
    uint32_t width;               /* 16 */
    uint32_t height;              /* 20 */
} ui_atlas_picture_t;
static_assert(sizeof(ui_atlas_picture_t) == 24);

/* Whether a pixel is on the picture and may take a mark in this mode. */
bool ui_atlas_draw_allowed(const ui_atlas_picture_t *picture, int32_t x, int32_t y, ui_atlas_mode_t mode);
void ui_atlas_draw_point(const ui_atlas_picture_t *picture, int32_t x, int32_t y, uint32_t colour,
                         ui_atlas_mode_t mode);
/* The colour at alpha / 255 over what is there. */
void ui_atlas_draw_point_blend(const ui_atlas_picture_t *picture, int32_t x, int32_t y, uint32_t colour, uint32_t alpha,
                               ui_atlas_mode_t mode);
/* Bresenham's line, both ends included. dash 0 is solid; else dash pixels on, dash off, along the line. */
void ui_atlas_draw_line(const ui_atlas_picture_t *picture, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                        uint32_t colour, uint32_t dash, ui_atlas_mode_t mode);
/* A circle by the midpoint rule, radius in pixels, at least 1. */
void ui_atlas_draw_circle(const ui_atlas_picture_t *picture, int32_t cx, int32_t cy, int32_t radius, uint32_t colour,
                          ui_atlas_mode_t mode);
/* A filled disc: every pixel whose centre is within radius, blended at alpha / 255 (255 is opaque). */
void ui_atlas_draw_disc(const ui_atlas_picture_t *picture, int32_t cx, int32_t cy, int32_t radius, uint32_t colour,
                        uint32_t alpha, ui_atlas_mode_t mode);
/* An arrow from (x, y), length pixels along (u, v) with v north, so drawn upward on screen; two 3-pixel barbs at its head
 * turned 150 degrees each way, as climate/layer draws them. */
void ui_atlas_draw_arrow(const ui_atlas_picture_t *picture, int32_t x, int32_t y, float32_t u, float32_t v,
                         float32_t length, uint32_t colour, ui_atlas_mode_t mode);

/* A forecast cone: the union of circles (centres in pixels, radii in pixels). Its edge, every pixel inside with a
 * 4-neighbour outside, in edge colour as an outline; its inside filled at fill_alpha / 255 off grey. scratch holds
 * 3 x picture width bytes. */
void ui_atlas_draw_cone(const ui_atlas_picture_t *picture, const float32_t *centres_x, const float32_t *centres_y,
                        const float32_t *radii, uint32_t count, uint32_t edge, uint32_t fill, uint32_t fill_alpha,
                        uint8_t *scratch);

/* A legend in rect on a canvas: a dark panel, the title, then either a bar of the scale with five values under it and the
 * unit, or the entries as swatches with labels, or (arrow_speed above 0) an arrow of that many blocks a second for length.
 * text_scale is the font's. Returns the height used. */
int32_t ui_atlas_draw_legend(ui_canvas_t *canvas, const ui_font_t *font, ui_rect_t rect, uint32_t text_scale,
                             const char *title, const char *unit, const ui_atlas_scale_t *scale,
                             const ui_atlas_key_entry_t *entries, uint32_t entry_count, float32_t arrow_speed);

#endif
