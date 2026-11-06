/* See run/src/ui/font/README.md. */
#ifndef MC_RUN_UI_FONT_INDEX_H
#define MC_RUN_UI_FONT_INDEX_H

#include "arena/index.h"
#include "texture/index.h"
#include "ui/compose/index.h"

#include <stdint.h>

enum : uint32_t {
    UI_FONT_ATLAS_WIDTH = 128u,
    UI_FONT_ATLAS_HEIGHT = 56u, /* plan 004 phase 09: a seventh row, for characters past ASCII */
    UI_FONT_CELL = 8u,
    UI_FONT_GLYPH_WIDTH = 5u,
    UI_FONT_GLYPH_HEIGHT = 7u,
    UI_FONT_FIRST = 32u,  /* the atlas starts at space */
    UI_FONT_GLYPHS = 96u, /* ASCII: 16 to a row, 6 rows: the last cell is DEL's, and empty */
    UI_FONT_EXTRA = 2u,   /* the seventh row's first cells: the degree sign (U+00B0) and the middle dot (U+00B7) */
    UI_FONT_EXTRA_ROW_CELLS = 16u, /* the seventh row; the cells past UI_FONT_EXTRA stay empty for later */
    UI_FONT_LINE = 9u,             /* from one line's top to the next's, at scale 1 */
    UI_FONT_SPACING = 1u,          /* empty columns after each glyph */
    UI_FONT_SPACE_WIDTH = 3u,
};

/* One character: for each row, a bit per column, the lowest on the left. */
typedef struct ui_font_glyph_t {
    uint8_t rows[UI_FONT_GLYPH_HEIGHT]; /* 0 */
    uint8_t advance;                    /* 7: from this glyph's left to the next's, at scale 1 */
} ui_font_glyph_t;
static_assert(sizeof(ui_font_glyph_t) == 8);

typedef struct ui_font_t {
    const ui_font_glyph_t *borrowed_glyphs; /* UI_FONT_GLYPHS + UI_FONT_EXTRA of them, from the arena its Owner gave */
} ui_font_t;
static_assert(sizeof(ui_font_t) == 8);

/* How to draw text. Colours are 0xAARRGGBB, straight alpha. */
typedef struct ui_text_style_t {
    uint32_t color;  /* 0 */
    uint32_t shadow; /* 4: drawn first, one font pixel down and right; alpha 0 means none */
    uint32_t scale;  /* 8: screen pixels per font pixel; 0 draws nothing */
} ui_text_style_t;
static_assert(sizeof(ui_text_style_t) == 12);

typedef struct ui_text_size_t {
    uint32_t width;  /* 0: of the widest line, without the spacing after its last glyph or a shadow */
    uint32_t height; /* 4: from the first line's top to the last line's bottom, without a shadow */
} ui_text_size_t;
static_assert(sizeof(ui_text_size_t) == 8);

/* Reads ui/font into glyphs, from arena. False, having logged why, if the texture is missing, is not the size this expects,
 * or has ink outside a glyph's box. */
bool ui_font_init(ui_font_t *font, arena_t *arena, const texture_t *texture);

/* The same from an atlas of RGBA8 pixels, width by height, top row first. */
bool ui_font_load(ui_font_t *font, arena_t *arena, const uint8_t *pixels, uint32_t width, uint32_t height);

/* Empties the handle. The memory stays with the arena. */
void ui_font_deinit(ui_font_t *font);

/* The glyph a byte of ASCII shows. Anything the atlas has no picture for shows '?'. */
const ui_font_glyph_t *ui_font_glyph(const ui_font_t *font, uint8_t character);

/* The glyph of a character past ASCII, by its index in the seventh row: 0 the degree sign, 1 the middle dot. */
const ui_font_glyph_t *ui_font_glyph_extra(const ui_font_t *font, uint32_t index);

/* Text is NUL-terminated UTF-8, and '\n' starts a line at x. A tab is a space; the degree sign (bytes 0xC2 0xB0) and the
 * middle dot (0xC2 0xB7) are one character each, from the seventh row; any other character the atlas does not have,
 * including every other one past ASCII, is one '?'. */
ui_text_size_t ui_font_measure(const ui_font_t *font, const char *text, uint32_t scale);

/* Draws text with its first line's top left at (x, y). Each scanline of a line is built whole and goes to the blend kernel
 * in one call. No allocation. */
void ui_font_draw(const ui_font_t *font, ui_canvas_t *canvas, int32_t x, int32_t y, const char *text,
                  ui_text_style_t style);

#endif
