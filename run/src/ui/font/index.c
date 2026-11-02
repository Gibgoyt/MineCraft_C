/* See README.md. */
#include "ui/font/index.h"

#include "log/index.h"

enum : uint32_t {
    UI_FONT_BYTES_PER_PIXEL = 4u,
    UI_FONT_SOLID_ALPHA =
        128u, /* TEXTURE_BLOB_CUTOUT_THRESHOLD's rule, though the font's alpha is only ever 0 or 255 */
    UI_FONT_REPLACEMENT = '?',
    UI_FONT_ATLAS_COLUMNS = UI_FONT_ATLAS_WIDTH / UI_FONT_CELL,
};
static_assert(UI_FONT_ATLAS_COLUMNS * (UI_FONT_ATLAS_HEIGHT / UI_FONT_CELL) == UI_FONT_GLYPHS);

/* What ui_font_next found, past any character. */
enum : uint32_t {
    UI_FONT_END = UINT32_MAX,
    UI_FONT_NEWLINE = UINT32_MAX - 1u,
};

bool ui_font_init(ui_font_t *font, arena_t *arena, const texture_t *texture) {
    font->borrowed_glyphs = nullptr;
    uint32_t id = 0u;
    texture_mip_t mip;
    if (!texture_find(texture, "ui/font", &id) || !texture_mip_get(texture, id, 0u, 0u, &mip)) {
        LOG_ERROR("ui: no texture ui/font");
        return false;
    }
    return ui_font_load(font, arena, mip.borrowed_pixels, mip.width, mip.height);
}

bool ui_font_load(ui_font_t *font, arena_t *arena, const uint8_t *pixels, uint32_t width, uint32_t height) {
    font->borrowed_glyphs = nullptr;
    if (width != UI_FONT_ATLAS_WIDTH || height != UI_FONT_ATLAS_HEIGHT) {
        LOG_ERROR("ui: ui/font is %ux%u, not %ux%u", width, height, UI_FONT_ATLAS_WIDTH, UI_FONT_ATLAS_HEIGHT);
        return false;
    }
    ui_font_glyph_t *const glyphs = arena_alloc_array(arena, UI_FONT_GLYPHS, sizeof(*glyphs), alignof(ui_font_glyph_t));
    for (uint32_t glyph = 0u; glyph < UI_FONT_GLYPHS; glyph++) {
        const uint32_t cell_x = glyph % UI_FONT_ATLAS_COLUMNS * UI_FONT_CELL;
        const uint32_t cell_y = glyph / UI_FONT_ATLAS_COLUMNS * UI_FONT_CELL;
        ui_font_glyph_t *const out = &glyphs[glyph];
        *out = (ui_font_glyph_t){0};
        uint32_t ink_width = 0u;
        for (uint32_t y = 0u; y < UI_FONT_CELL; y++) {
            for (uint32_t x = 0u; x < UI_FONT_CELL; x++) {
                const uint64_t pixel = (uint64_t)(cell_y + y) * width + cell_x + x;
                if (pixels[pixel * UI_FONT_BYTES_PER_PIXEL + 3u] < UI_FONT_SOLID_ALPHA) {
                    continue;
                }
                if (x >= UI_FONT_GLYPH_WIDTH || y >= UI_FONT_GLYPH_HEIGHT) {
                    LOG_ERROR("ui: ui/font has ink outside glyph %u, at %u,%u of its cell", glyph + UI_FONT_FIRST, x,
                              y);
                    return false;
                }
                out->rows[y] |= (uint8_t)(1u << x);
                ink_width = x + 1u > ink_width ? x + 1u : ink_width;
            }
        }
        out->advance = (uint8_t)((ink_width == 0u ? UI_FONT_SPACE_WIDTH : ink_width) + UI_FONT_SPACING);
    }
    font->borrowed_glyphs = glyphs;
    return true;
}

void ui_font_deinit(ui_font_t *font) {
    font->borrowed_glyphs = nullptr;
}

const ui_font_glyph_t *ui_font_glyph(const ui_font_t *font, uint8_t character) {
    /* DEL has a cell, and it is empty: a blank where text went wrong would hide it. */
    const bool printable = character >= UI_FONT_FIRST && character < UI_FONT_FIRST + UI_FONT_GLYPHS - 1u;
    const uint32_t shown = printable ? (uint32_t)character : (uint32_t)UI_FONT_REPLACEMENT;
    return &font->borrowed_glyphs[shown - UI_FONT_FIRST];
}

/* One step along text: the character to show, or that the text or the line has ended. A sequence past ASCII is one '?': its
 * first byte shows it and the bytes that continue it are passed over, so that a name in any language is the right length. */
static uint32_t ui_font_next(const char **cursor) {
    const uint8_t byte = (uint8_t)**cursor;
    if (byte == 0u) {
        return UI_FONT_END;
    }
    (*cursor)++;
    if (byte == '\n') {
        return UI_FONT_NEWLINE;
    }
    if (byte == '\t') {
        return ' ';
    }
    if (byte < 0x80u) {
        return byte;
    }
    while (((uint8_t)**cursor & 0xC0u) == 0x80u) {
        (*cursor)++;
    }
    return UI_FONT_REPLACEMENT;
}

/* The width of the line at *cursor at scale 1, the spacing after its last glyph left out, and moves past the line. Says
 * through out_more whether a newline ended it, and so whether another line follows. */
static uint64_t ui_font_line_width(const ui_font_t *font, const char **cursor, bool *out_more) {
    uint64_t width = 0u;
    for (;;) {
        const uint32_t next = ui_font_next(cursor);
        if (next == UI_FONT_END || next == UI_FONT_NEWLINE) {
            *out_more = next == UI_FONT_NEWLINE;
            return width == 0u ? 0u : width - UI_FONT_SPACING;
        }
        width += ui_font_glyph(font, (uint8_t)next)->advance;
    }
}

ui_text_size_t ui_font_measure(const ui_font_t *font, const char *text, uint32_t scale) {
    if (*text == '\0') {
        return (ui_text_size_t){0u, 0u};
    }
    uint64_t widest = 0u;
    uint64_t lines = 0u;
    const char *cursor = text;
    bool more = true;
    while (more) {
        const uint64_t width = ui_font_line_width(font, &cursor, &more);
        widest = width > widest ? width : widest;
        lines++;
    }
    const uint64_t height = (lines - 1u) * UI_FONT_LINE + UI_FONT_GLYPH_HEIGHT;
    return (ui_text_size_t){(uint32_t)(widest * scale), (uint32_t)(height * scale)};
}

/* One line, in one colour. Each scanline is built in the row scratch, clear where there is no ink, which the kernel leaves
 * as it found it. */
static void ui_font_draw_line(const ui_font_t *font, ui_canvas_t *canvas, int64_t x, int64_t y, const char *line,
                              uint64_t line_width, uint32_t color, uint32_t scale) {
    const ui_rect_t clip = canvas->clip;
    const int64_t right = x + (int64_t)(line_width * scale);
    const int64_t bottom = y + (int64_t)((uint64_t)UI_FONT_GLYPH_HEIGHT * scale);
    const int64_t clip_right = (int64_t)clip.x + clip.width;
    const int64_t clip_bottom = (int64_t)clip.y + clip.height;
    const int64_t x0 = x > clip.x ? x : clip.x;
    const int64_t x1 = right < clip_right ? right : clip_right;
    const int64_t y0 = y > clip.y ? y : clip.y;
    const int64_t y1 = bottom < clip_bottom ? bottom : clip_bottom;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    uint32_t *const row = canvas->borrowed_row;
    const uint32_t count = (uint32_t)(x1 - x0);
    for (int64_t screen_y = y0; screen_y < y1; screen_y++) {
        const uint32_t glyph_row = (uint32_t)((screen_y - y) / (int64_t)scale);
        for (uint32_t i = 0u; i < count; i++) {
            row[i] = 0u;
        }
        bool any_ink = false;
        int64_t pen = x;
        const char *cursor = line;
        while (pen < x1) {
            const uint32_t next = ui_font_next(&cursor);
            if (next == UI_FONT_END || next == UI_FONT_NEWLINE) {
                break;
            }
            const ui_font_glyph_t *const glyph = ui_font_glyph(font, (uint8_t)next);
            const uint32_t bits = glyph->rows[glyph_row];
            for (uint32_t column = 0u; bits != 0u && column < UI_FONT_GLYPH_WIDTH; column++) {
                if ((bits >> column & 1u) == 0u) {
                    continue;
                }
                const int64_t left = pen + (int64_t)((uint64_t)column * scale);
                const int64_t limit = left + (int64_t)scale;
                const int64_t from = left > x0 ? left : x0;
                const int64_t to = limit < x1 ? limit : x1;
                for (int64_t screen_x = from; screen_x < to; screen_x++) {
                    row[screen_x - x0] = color;
                    any_ink = true;
                }
            }
            pen += (int64_t)((uint64_t)glyph->advance * scale);
        }
        if (any_ink) {
            ui_compose_span(canvas, (int32_t)x0, (int32_t)screen_y, row, count);
        }
    }
}

static void ui_font_draw_color(const ui_font_t *font, ui_canvas_t *canvas, int64_t x, int64_t y, const char *text,
                               uint32_t color, uint32_t scale) {
    const char *line = text;
    int64_t line_y = y;
    bool more = true;
    while (more) {
        const char *cursor = line;
        const uint64_t width = ui_font_line_width(font, &cursor, &more);
        ui_font_draw_line(font, canvas, x, line_y, line, width, color, scale);
        line = cursor;
        line_y += (int64_t)((uint64_t)UI_FONT_LINE * scale);
    }
}

void ui_font_draw(const ui_font_t *font, ui_canvas_t *canvas, int32_t x, int32_t y, const char *text,
                  ui_text_style_t style) {
    if (style.scale == 0u) {
        return;
    }
    if (style.shadow >> 24 != 0u) {
        const int64_t offset = (int64_t)style.scale;
        ui_font_draw_color(font, canvas, (int64_t)x + offset, (int64_t)y + offset, text, style.shadow, style.scale);
    }
    if (style.color >> 24 != 0u) {
        ui_font_draw_color(font, canvas, x, y, text, style.color, style.scale);
    }
}
