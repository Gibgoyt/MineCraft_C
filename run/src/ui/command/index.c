/* See README.md. The command box's look and the tuning lines' panel. Allocates nothing. */
#include "ui/command/index.h"

#include <string.h>

enum : uint32_t {
    UI_COMMAND_TEXT_COLOR = 0xFFFFFFFFu,
    UI_COMMAND_LINE_COLOR = 0xFFE0E0A0u,  /* what the text means, or what a command said */
    UI_COMMAND_ERROR_COLOR = 0xFFFF5555u, /* a bad command */
    UI_COMMAND_SHADOW_COLOR = 0xFF3F3F3Fu,
    UI_COMMAND_PANEL_COLOR = 0xA0000000u, /* the F3 overlay's */
    UI_COMMAND_MARGIN = 2u,               /* font pixels, from the canvas to a panel and from a panel to its text */
};

ui_field_event_t ui_command_keys(ui_widget_t *widget, char *buffer, uint32_t capacity) {
    ui_widget_input_t *const input = &widget->input;
    ui_field_event_t event = UI_FIELD_NONE;
    uint64_t length = strlen(buffer);
    if (input->backspace != 0u && length != 0u) {
        buffer[--length] = '\0';
        event = UI_FIELD_CHANGED;
    }
    for (uint32_t i = 0u; i < input->typed_count; i++) {
        const uint8_t typed = input->typed[i];
        if (typed >= 0x20u && typed < 0x7Fu && length + 1u < capacity) {
            buffer[length++] = (char)typed;
            buffer[length] = '\0';
            event = UI_FIELD_CHANGED;
        }
    }
    if (input->enter != 0u) {
        event = UI_FIELD_SUBMITTED;
    }
    input->typed_count = 0u;
    input->backspace = 0u;
    input->enter = 0u;
    return event;
}

/* Half the widgets' scale, rounding up, as the F3 overlay is drawn. */
static uint32_t ui_command_small_scale(const ui_canvas_t *canvas) {
    return (ui_widget_scale_for(canvas->width, canvas->height) + 1u) / 2u;
}

/* One line on a dark band from the left edge, as wide as the text, with its bottom at bottom. Returns the band's top. */
static int32_t ui_command_band(ui_canvas_t *canvas, const ui_font_t *font, int32_t bottom, const char *text,
                               uint32_t color, uint32_t scale) {
    const int32_t margin = (int32_t)UI_COMMAND_MARGIN * (int32_t)scale;
    const ui_text_size_t size = ui_font_measure(font, text, scale);
    const int32_t height = (int32_t)UI_FONT_LINE * (int32_t)scale + margin;
    const ui_rect_t band = {margin, bottom - height, (int32_t)size.width + 2 * margin, height};
    ui_compose_fill(canvas, band, UI_COMMAND_PANEL_COLOR);
    const ui_text_style_t style = {.color = color, .shadow = UI_COMMAND_SHADOW_COLOR, .scale = scale};
    ui_font_draw(font, canvas, band.x + margin, band.y + margin / 2, text, style);
    return band.y;
}

void ui_command_draw(ui_widget_t *widget, ui_canvas_t *canvas, const ui_font_t *font, const ui_command_view_t *view) {
    const int32_t scale = (int32_t)widget->scale;
    const int32_t margin = (int32_t)UI_COMMAND_MARGIN * scale;
    const int32_t height = (int32_t)UI_WIDGET_BUTTON_HEIGHT * scale;
    const ui_rect_t field = {margin, (int32_t)canvas->height - height - margin, (int32_t)canvas->width - 2 * margin,
                             height};
    if (view->borrowed_text != nullptr) {
        /* Always focused while it is up: nothing has to be clicked first. What was typed has been taken already. */
        widget->focus_id = UI_COMMAND_FIELD_ID;
        (void)ui_widget_text_field(widget, UI_COMMAND_FIELD_ID, field, view->borrowed_text,
                                   (uint32_t)strlen(view->borrowed_text) + 1u);
        widget->focus_id = UI_COMMAND_FIELD_ID;
    }
    const uint32_t small = (uint32_t)scale;
    int32_t bottom = field.y - margin;
    if (view->borrowed_error != nullptr && view->borrowed_error[0] != '\0') {
        bottom = ui_command_band(canvas, font, bottom, view->borrowed_error, UI_COMMAND_ERROR_COLOR, small);
    }
    if (view->borrowed_line != nullptr && view->borrowed_line[0] != '\0') {
        bottom = ui_command_band(canvas, font, bottom, view->borrowed_line, UI_COMMAND_LINE_COLOR, small);
    }
    const uint32_t count = view->line_count < UI_COMMAND_LINES_MAX ? view->line_count : UI_COMMAND_LINES_MAX;
    for (uint32_t i = count; i > 0u; i--) {
        const char *const line = view->borrowed_lines[i - 1u];
        if (line != nullptr && line[0] != '\0') {
            bottom = ui_command_band(canvas, font, bottom, line, UI_COMMAND_TEXT_COLOR, small);
        }
    }
}

ui_rect_t ui_command_lines_draw(ui_canvas_t *canvas, const ui_font_t *font, int32_t x, int32_t y,
                                const char *const *lines, uint32_t count) {
    const uint32_t scale = ui_command_small_scale(canvas);
    const int32_t margin = (int32_t)UI_COMMAND_MARGIN * (int32_t)scale;
    const uint32_t shown = count < UI_COMMAND_LINES_MAX ? count : UI_COMMAND_LINES_MAX;
    uint32_t widest = 0u;
    for (uint32_t i = 0u; i < shown; i++) {
        const ui_text_size_t size = ui_font_measure(font, lines[i], scale);
        widest = size.width > widest ? size.width : widest;
    }
    const ui_rect_t panel = {x, y, (int32_t)widest + 2 * margin, (int32_t)(shown * UI_FONT_LINE * scale) + 2 * margin};
    ui_compose_fill(canvas, panel, UI_COMMAND_PANEL_COLOR);
    const ui_text_style_t style = {.color = UI_COMMAND_TEXT_COLOR, .shadow = UI_COMMAND_SHADOW_COLOR, .scale = scale};
    for (uint32_t i = 0u; i < shown; i++) {
        ui_font_draw(font, canvas, x + margin, y + margin + (int32_t)(i * UI_FONT_LINE * scale), lines[i], style);
    }
    return panel;
}
