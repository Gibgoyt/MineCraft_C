/* See README.md. */
#include "ui/widget/index.h"

#include "assert/index.h"
#include "log/index.h"

#include <string.h>

enum : uint32_t {
    UI_WIDGET_TEXT_COLOR = 0xFFFFFFFFu,
    UI_WIDGET_HOVER_COLOR = 0xFFFFFFA0u,
    UI_WIDGET_DISABLED_COLOR = 0xFFA0A0A0u,
    UI_WIDGET_SHADOW_COLOR = 0xFF3F3F3Fu,
    UI_WIDGET_CARET_COLOR = 0xFFFFFFFFu,
    UI_WIDGET_BORDER = 2u,         /* of the textures made for stretching: an outline and a bevel */
    UI_WIDGET_PANEL_BORDER = 4u,   /* of panel, and tooltip's own nine parts of 4 by 4 */
    UI_WIDGET_FIELD_BORDER = 1u,   /* text_field's outline */
    UI_WIDGET_TEXT_INSET = 4u,     /* from a field's or a tooltip's edge to its text */
    UI_WIDGET_LABEL_GAP = 4u,      /* from a checkbox's box to its label */
    UI_WIDGET_TOOLTIP_OFFSET = 8u, /* from the pointer to a tooltip */
    UI_WIDGET_POINTER_ROWS = 11u,
    UI_WIDGET_POINTER_COLUMNS = 8u,
    UI_WIDGET_KEYS = KEY_SPACE + 1u, /* the printable part of the evdev codes */
};

/* A US layout: nothing here can ask which is plugged in, and printable ASCII is all the font has. A zero is no character. */
static const uint8_t UI_WIDGET_KEYS_PLAIN[UI_WIDGET_KEYS] = {
    [KEY_1] = '1',     [KEY_2] = '2',          [KEY_3] = '3',         [KEY_4] = '4',          [KEY_5] = '5',
    [KEY_6] = '6',     [KEY_7] = '7',          [KEY_8] = '8',         [KEY_9] = '9',          [KEY_0] = '0',
    [KEY_MINUS] = '-', [KEY_EQUAL] = '=',      [KEY_Q] = 'q',         [KEY_W] = 'w',          [KEY_E] = 'e',
    [KEY_R] = 'r',     [KEY_T] = 't',          [KEY_Y] = 'y',         [KEY_U] = 'u',          [KEY_I] = 'i',
    [KEY_O] = 'o',     [KEY_P] = 'p',          [KEY_LEFTBRACE] = '[', [KEY_RIGHTBRACE] = ']', [KEY_A] = 'a',
    [KEY_S] = 's',     [KEY_D] = 'd',          [KEY_F] = 'f',         [KEY_G] = 'g',          [KEY_H] = 'h',
    [KEY_J] = 'j',     [KEY_K] = 'k',          [KEY_L] = 'l',         [KEY_SEMICOLON] = ';',  [KEY_APOSTROPHE] = '\'',
    [KEY_GRAVE] = '`', [KEY_BACKSLASH] = '\\', [KEY_Z] = 'z',         [KEY_X] = 'x',          [KEY_C] = 'c',
    [KEY_V] = 'v',     [KEY_B] = 'b',          [KEY_N] = 'n',         [KEY_M] = 'm',          [KEY_COMMA] = ',',
    [KEY_DOT] = '.',   [KEY_SLASH] = '/',      [KEY_SPACE] = ' ',
};
static const uint8_t UI_WIDGET_KEYS_SHIFT[UI_WIDGET_KEYS] = {
    [KEY_1] = '!',     [KEY_2] = '@',         [KEY_3] = '#',         [KEY_4] = '$',          [KEY_5] = '%',
    [KEY_6] = '^',     [KEY_7] = '&',         [KEY_8] = '*',         [KEY_9] = '(',          [KEY_0] = ')',
    [KEY_MINUS] = '_', [KEY_EQUAL] = '+',     [KEY_Q] = 'Q',         [KEY_W] = 'W',          [KEY_E] = 'E',
    [KEY_R] = 'R',     [KEY_T] = 'T',         [KEY_Y] = 'Y',         [KEY_U] = 'U',          [KEY_I] = 'I',
    [KEY_O] = 'O',     [KEY_P] = 'P',         [KEY_LEFTBRACE] = '{', [KEY_RIGHTBRACE] = '}', [KEY_A] = 'A',
    [KEY_S] = 'S',     [KEY_D] = 'D',         [KEY_F] = 'F',         [KEY_G] = 'G',          [KEY_H] = 'H',
    [KEY_J] = 'J',     [KEY_K] = 'K',         [KEY_L] = 'L',         [KEY_SEMICOLON] = ':',  [KEY_APOSTROPHE] = '"',
    [KEY_GRAVE] = '~', [KEY_BACKSLASH] = '|', [KEY_Z] = 'Z',         [KEY_X] = 'X',          [KEY_C] = 'C',
    [KEY_V] = 'V',     [KEY_B] = 'B',         [KEY_N] = 'N',         [KEY_M] = 'M',          [KEY_COMMA] = '<',
    [KEY_DOT] = '>',   [KEY_SLASH] = '?',     [KEY_SPACE] = ' ',
};

/* The pointer: b is its outline and w its body. It is drawn here because no texture holds one. */
static const char UI_WIDGET_POINTER[UI_WIDGET_POINTER_ROWS][UI_WIDGET_POINTER_COLUMNS + 1u] = {
    "b.......", "bb......", "bwb.....", "bwwb....", "bwwwb...", "bwwwwb..",
    "bwwwwwb.", "bwwwbbbb", "bwbwb...", "bb.bwb..", "....bb..",
};

bool ui_widget_init(ui_widget_t *widget, arena_t *arena, const texture_t *texture, const ui_font_t *font,
                    arena_t *arena_frame) {
    memset(widget, 0, sizeof(*widget));
    ui_widget_skin_t *const skin = &widget->skin;
    const bool loaded = ui_image_load(&skin->button, arena, texture, "ui/button") &&
                        ui_image_load(&skin->button_hover, arena, texture, "ui/button_hover") &&
                        ui_image_load(&skin->button_disabled, arena, texture, "ui/button_disabled") &&
                        ui_image_load(&skin->panel, arena, texture, "ui/panel") &&
                        ui_image_load(&skin->checkbox, arena, texture, "ui/checkbox") &&
                        ui_image_load(&skin->checkbox_checked, arena, texture, "ui/checkbox_checked") &&
                        ui_image_load(&skin->slider_track, arena, texture, "ui/slider_track") &&
                        ui_image_load(&skin->slider_knob, arena, texture, "ui/slider_knob") &&
                        ui_image_load(&skin->text_field, arena, texture, "ui/text_field") &&
                        ui_image_load(&skin->tooltip, arena, texture, "ui/tooltip") &&
                        ui_image_load(&skin->slot, arena, texture, "ui/slot") &&
                        ui_image_load(&skin->slot_highlight, arena, texture, "ui/slot_highlight") &&
                        ui_image_load(&skin->progress_empty, arena, texture, "ui/progress_bar_empty") &&
                        ui_image_load(&skin->progress_full, arena, texture, "ui/progress_bar_full");
    if (!loaded) {
        return false;
    }
    widget->borrowed_font = font;
    widget->borrowed_arena_frame = arena_frame;
    widget->scale = 1u;
    return true;
}

void ui_widget_deinit(ui_widget_t *widget) {
    memset(widget, 0, sizeof(*widget));
}

uint32_t ui_widget_scale_for(uint32_t canvas_width, uint32_t canvas_height) {
    const uint32_t by_width = canvas_width / UI_WIDGET_SCALE_BASE_W;
    const uint32_t by_height = canvas_height / UI_WIDGET_SCALE_BASE_H;
    const uint32_t scale = by_width < by_height ? by_width : by_height;
    return scale == 0u ? 1u : scale;
}

static int32_t ui_widget_clamp(int64_t value, int64_t minimum, int64_t maximum) {
    return (int32_t)(value < minimum ? minimum : (value > maximum ? maximum : value));
}

void ui_widget_input_feed(ui_widget_t *widget, const input_frame_t *frame) {
    ui_widget_input_t *const input = &widget->input;
    if (widget->canvas_width != 0u) {
        input->pointer_x =
            ui_widget_clamp((int64_t)input->pointer_x + frame->mouse_dx, 0, (int64_t)widget->canvas_width - 1);
        input->pointer_y =
            ui_widget_clamp((int64_t)input->pointer_y + frame->mouse_dy, 0, (int64_t)widget->canvas_height - 1);
    }
    input->wheel = ui_widget_clamp((int64_t)input->wheel + frame->wheel, INT32_MIN, INT32_MAX);
    input->left_down = input_keys_has(&frame->down, BTN_LEFT) ? 1u : 0u;
    input->left_pressed |= input_keys_has(&frame->pressed, BTN_LEFT) ? 1u : 0u;
    input->left_released |= input_keys_has(&frame->released, BTN_LEFT) ? 1u : 0u;
    input->right_pressed |= input_keys_has(&frame->pressed, BTN_RIGHT) ? 1u : 0u;
    input->backspace |=
        input_keys_has(&frame->pressed, KEY_BACKSPACE) || input_keys_has(&frame->repeated, KEY_BACKSPACE) ? 1u : 0u;
    input->enter |=
        input_keys_has(&frame->pressed, KEY_ENTER) || input_keys_has(&frame->pressed, KEY_KPENTER) ? 1u : 0u;
    const bool shift = input_keys_has(&frame->down, KEY_LEFTSHIFT) || input_keys_has(&frame->down, KEY_RIGHTSHIFT);
    const uint8_t *const keys = shift ? UI_WIDGET_KEYS_SHIFT : UI_WIDGET_KEYS_PLAIN;
    /* By key code, since a poll keeps no order: two keys inside one poll, a few milliseconds, could swap. */
    for (uint32_t code = 0u; code < UI_WIDGET_KEYS && input->typed_count < UI_WIDGET_TYPED_MAX; code++) {
        if (keys[code] != 0u && (input_keys_has(&frame->pressed, code) || input_keys_has(&frame->repeated, code))) {
            input->typed[input->typed_count++] = keys[code];
        }
    }
}

void ui_widget_frame_begin(ui_widget_t *widget, ui_canvas_t *canvas) {
    widget->borrowed_canvas = canvas;
    widget->borrowed_tooltip = nullptr;
    widget->scale = ui_widget_scale_for(canvas->width, canvas->height);
    if (canvas->width != widget->canvas_width || canvas->height != widget->canvas_height) {
        widget->canvas_width = canvas->width;
        widget->canvas_height = canvas->height;
        widget->input.pointer_x = (int32_t)(canvas->width / 2u);
        widget->input.pointer_y = (int32_t)(canvas->height / 2u);
    }
}

static ui_text_style_t ui_widget_style(const ui_widget_t *widget, uint32_t color, bool shadowed) {
    return (ui_text_style_t){.color = color, .shadow = shadowed ? UI_WIDGET_SHADOW_COLOR : 0u, .scale = widget->scale};
}

static void ui_widget_pointer_draw(ui_widget_t *widget) {
    const int32_t scale = (int32_t)widget->scale;
    for (int32_t row = 0; row < (int32_t)UI_WIDGET_POINTER_ROWS; row++) {
        for (int32_t column = 0; column < (int32_t)UI_WIDGET_POINTER_COLUMNS; column++) {
            const char pixel = UI_WIDGET_POINTER[row][column];
            if (pixel == '.') {
                continue;
            }
            const ui_rect_t rect = {widget->input.pointer_x + column * scale, widget->input.pointer_y + row * scale,
                                    scale, scale};
            ui_compose_fill(widget->borrowed_canvas, rect, pixel == 'b' ? 0xFF000000u : 0xFFFFFFFFu);
        }
    }
}

/* Backing and text, kept on the canvas whole unless the canvas is too small for that. */
static void ui_widget_tooltip_draw(ui_widget_t *widget) {
    ui_canvas_t *const canvas = widget->borrowed_canvas;
    const int32_t scale = (int32_t)widget->scale;
    const ui_text_size_t size = ui_font_measure(widget->borrowed_font, widget->borrowed_tooltip, widget->scale);
    const int32_t inset = (int32_t)UI_WIDGET_TEXT_INSET * scale;
    const int32_t width = (int32_t)size.width + 2 * inset;
    const int32_t height = (int32_t)size.height + 2 * inset;
    const int32_t offset = (int32_t)UI_WIDGET_TOOLTIP_OFFSET * scale;
    const int64_t room_x = (int64_t)canvas->width - width;
    const int64_t room_y = (int64_t)canvas->height - height;
    const ui_rect_t rect = {
        ui_widget_clamp((int64_t)widget->input.pointer_x + offset, 0, room_x > 0 ? room_x : 0),
        ui_widget_clamp((int64_t)widget->input.pointer_y + offset, 0, room_y > 0 ? room_y : 0),
        width,
        height,
    };
    ui_compose_nine(canvas, &widget->skin.tooltip, UI_WIDGET_PANEL_BORDER, widget->scale, rect);
    ui_font_draw(widget->borrowed_font, canvas, rect.x + inset, rect.y + inset, widget->borrowed_tooltip,
                 ui_widget_style(widget, UI_WIDGET_TEXT_COLOR, true));
}

void ui_widget_frame_end(ui_widget_t *widget, bool show_pointer) {
    const ui_rect_t whole = {0, 0, (int32_t)widget->canvas_width, (int32_t)widget->canvas_height};
    (void)ui_canvas_clip_set(widget->borrowed_canvas, whole);
    if (widget->borrowed_tooltip != nullptr) {
        ui_widget_tooltip_draw(widget);
    }
    if (show_pointer) {
        ui_widget_pointer_draw(widget);
    }
    ui_widget_input_t *const input = &widget->input;
    if (input->left_down == 0u) {
        widget->active_id = 0u;
    }
    input->wheel = 0;
    input->typed_count = 0u;
    input->left_pressed = 0u;
    input->left_released = 0u;
    input->right_pressed = 0u;
    input->backspace = 0u;
    input->enter = 0u;
    widget->borrowed_tooltip = nullptr;
    widget->borrowed_canvas = nullptr;
}

/* Whether the pointer may act on this widget: it is over the part that can be seen, and no other widget holds a press. */
static bool ui_widget_hovered(const ui_widget_t *widget, uint32_t id, ui_rect_t rect) {
    if (widget->active_id != 0u && widget->active_id != id) {
        return false;
    }
    const ui_rect_t seen = ui_rect_intersect(rect, widget->borrowed_canvas->clip);
    return ui_rect_contains(seen, widget->input.pointer_x, widget->input.pointer_y);
}

/* True in the frame of a whole click: pressed here, released here. */
static bool ui_widget_click(ui_widget_t *widget, uint32_t id, bool hovered) {
    if (hovered && widget->input.left_pressed != 0u) {
        widget->active_id = id;
    }
    return hovered && widget->active_id == id && widget->input.left_released != 0u;
}

void ui_widget_panel(ui_widget_t *widget, ui_rect_t rect) {
    ui_compose_nine(widget->borrowed_canvas, &widget->skin.panel, UI_WIDGET_PANEL_BORDER, widget->scale, rect);
}

void ui_widget_label(ui_widget_t *widget, int32_t x, int32_t y, const char *text, uint32_t color, bool shadowed) {
    ui_font_draw(widget->borrowed_font, widget->borrowed_canvas, x, y, text, ui_widget_style(widget, color, shadowed));
}

void ui_widget_label_centered(ui_widget_t *widget, ui_rect_t rect, const char *text, uint32_t color, bool shadowed) {
    const ui_text_size_t size = ui_font_measure(widget->borrowed_font, text, widget->scale);
    const ui_rect_t centered = ui_rect_center(rect, (int32_t)size.width, (int32_t)size.height);
    ui_widget_label(widget, centered.x, centered.y, text, color, shadowed);
}

bool ui_widget_button(ui_widget_t *widget, uint32_t id, ui_rect_t rect, const char *label, bool enabled) {
    ASSERT(id != 0u);
    const bool hovered = enabled && ui_widget_hovered(widget, id, rect);
    const bool clicked = enabled && ui_widget_click(widget, id, hovered);
    const ui_image_t *const image =
        !enabled ? &widget->skin.button_disabled : (hovered ? &widget->skin.button_hover : &widget->skin.button);
    ui_canvas_t *const canvas = widget->borrowed_canvas;
    ui_compose_nine(canvas, image, UI_WIDGET_BORDER, widget->scale, rect);
    const ui_rect_t outer = ui_canvas_clip_set(canvas, ui_rect_intersect(rect, canvas->clip));
    const uint32_t color =
        !enabled ? UI_WIDGET_DISABLED_COLOR : (hovered ? UI_WIDGET_HOVER_COLOR : UI_WIDGET_TEXT_COLOR);
    ui_widget_label_centered(widget, rect, label, color, enabled);
    (void)ui_canvas_clip_set(canvas, outer);
    return clicked;
}

bool ui_widget_checkbox(ui_widget_t *widget, uint32_t id, ui_rect_t rect, const char *label, bool *value) {
    ASSERT(id != 0u);
    const bool hovered = ui_widget_hovered(widget, id, rect);
    const bool clicked = ui_widget_click(widget, id, hovered);
    if (clicked) {
        *value = !*value;
    }
    const ui_image_t *const image = *value ? &widget->skin.checkbox_checked : &widget->skin.checkbox;
    const int32_t scale = (int32_t)widget->scale;
    const int32_t box = (int32_t)image->width * scale;
    const ui_rect_t box_rect = {rect.x, ui_rect_center(rect, box, box).y, box, box};
    ui_compose_image_whole(widget->borrowed_canvas, image, box_rect);
    const ui_text_size_t size = ui_font_measure(widget->borrowed_font, label, widget->scale);
    ui_widget_label(widget, rect.x + box + (int32_t)UI_WIDGET_LABEL_GAP * scale,
                    ui_rect_center(rect, (int32_t)size.width, (int32_t)size.height).y, label,
                    hovered ? UI_WIDGET_HOVER_COLOR : UI_WIDGET_TEXT_COLOR, true);
    return clicked;
}

bool ui_widget_slider(ui_widget_t *widget, uint32_t id, ui_rect_t rect, const char *label, int32_t *value,
                      int32_t minimum, int32_t maximum) {
    ASSERT(id != 0u);
    ASSERT_ALWAYS(minimum <= maximum);
    const int32_t before = *value;
    const int64_t range = (int64_t)maximum - minimum;
    int64_t now = ui_widget_clamp(*value, minimum, maximum);
    const bool hovered = ui_widget_hovered(widget, id, rect);
    const ui_widget_input_t *const input = &widget->input;
    if (hovered && input->left_pressed != 0u) {
        widget->active_id = id;
    }
    const int32_t knob_width = (int32_t)widget->skin.slider_knob.width * (int32_t)widget->scale;
    const int64_t travel = (int64_t)rect.width - knob_width;
    if (widget->active_id == id && travel > 0) {
        /* Rounded to the nearest value, with the pointer at the knob's middle. */
        const int64_t along = (int64_t)input->pointer_x - rect.x - knob_width / 2;
        const int64_t clamped = along < 0 ? 0 : (along > travel ? travel : along);
        now = minimum + (clamped * range + travel / 2) / travel;
    }
    if (hovered && widget->active_id != id && input->wheel != 0) {
        now = ui_widget_clamp(now + input->wheel, minimum, maximum);
    }
    *value = (int32_t)now;
    ui_canvas_t *const canvas = widget->borrowed_canvas;
    ui_compose_nine(canvas, &widget->skin.slider_track, UI_WIDGET_BORDER, widget->scale, rect);
    const int64_t knob_x = range == 0 || travel <= 0 ? 0 : (now - minimum) * travel / range;
    const ui_rect_t knob = {rect.x + (int32_t)knob_x, rect.y, knob_width, rect.height};
    ui_compose_image_whole(canvas, &widget->skin.slider_knob, knob);
    const ui_rect_t outer = ui_canvas_clip_set(canvas, ui_rect_intersect(rect, canvas->clip));
    ui_widget_label_centered(widget, rect, label, hovered ? UI_WIDGET_HOVER_COLOR : UI_WIDGET_TEXT_COLOR, true);
    (void)ui_canvas_clip_set(canvas, outer);
    return *value != before;
}

/* Applies this frame's keys to a focused field, Backspace first, since a poll does not say which came first. */
static ui_field_event_t ui_widget_field_keys(ui_widget_t *widget, char *buffer, uint32_t capacity) {
    const ui_widget_input_t *const input = &widget->input;
    ui_field_event_t event = UI_FIELD_NONE;
    uint64_t length = strlen(buffer);
    if (input->backspace != 0u && length > 0u) {
        buffer[--length] = '\0';
        event = UI_FIELD_CHANGED;
    }
    for (uint32_t i = 0u; i < input->typed_count && length + 1u < capacity; i++) {
        buffer[length++] = (char)input->typed[i];
        buffer[length] = '\0';
        event = UI_FIELD_CHANGED;
    }
    if (input->enter != 0u) {
        widget->focus_id = 0u;
        event = UI_FIELD_SUBMITTED;
    }
    return event;
}

ui_field_event_t ui_widget_text_field(ui_widget_t *widget, uint32_t id, ui_rect_t rect, char *buffer,
                                      uint32_t capacity) {
    ASSERT(id != 0u);
    ASSERT_ALWAYS(capacity != 0u);
    const bool hovered = ui_widget_hovered(widget, id, rect);
    const bool pressed = widget->input.left_pressed != 0u;
    if (pressed) {
        if (hovered) {
            widget->focus_id = id;
            widget->active_id = id;
        } else if (widget->focus_id == id) {
            widget->focus_id = 0u;
        }
    }
    const bool focused = widget->focus_id == id;
    const ui_field_event_t event = focused ? ui_widget_field_keys(widget, buffer, capacity) : UI_FIELD_NONE;
    ui_canvas_t *const canvas = widget->borrowed_canvas;
    const int32_t scale = (int32_t)widget->scale;
    ui_compose_nine(canvas, &widget->skin.text_field, UI_WIDGET_FIELD_BORDER, widget->scale, rect);
    const int32_t inset = (int32_t)UI_WIDGET_TEXT_INSET * scale;
    const ui_rect_t inner = {rect.x + inset, rect.y, rect.width - 2 * inset, rect.height};
    const ui_rect_t outer = ui_canvas_clip_set(canvas, ui_rect_intersect(inner, canvas->clip));
    const ui_text_size_t size = ui_font_measure(widget->borrowed_font, buffer, widget->scale);
    const int32_t caret_gap = scale; /* the same as between two glyphs */
    const int32_t wide = (int32_t)size.width + caret_gap + scale;
    const int32_t x = wide > inner.width ? inner.x + inner.width - wide : inner.x;
    const int32_t glyph_height = (int32_t)UI_FONT_GLYPH_HEIGHT * scale;
    const int32_t y = ui_rect_center(inner, 0, glyph_height).y;
    ui_font_draw(widget->borrowed_font, canvas, x, y, buffer, ui_widget_style(widget, UI_WIDGET_TEXT_COLOR, true));
    if (focused) {
        ui_compose_fill(canvas, (ui_rect_t){x + (int32_t)size.width + caret_gap, y, scale, glyph_height},
                        UI_WIDGET_CARET_COLOR);
    }
    (void)ui_canvas_clip_set(canvas, outer);
    return event;
}

uint32_t ui_widget_slot(ui_widget_t *widget, ui_rect_t rect) {
    ui_canvas_t *const canvas = widget->borrowed_canvas;
    /* The texture is one flat colour to its edge, so a row of slots needs this gap not to read as one long bar. */
    const int32_t gap = (int32_t)widget->scale;
    const ui_rect_t inner = {rect.x + gap, rect.y + gap, rect.width - 2 * gap, rect.height - 2 * gap};
    ui_compose_image_whole(canvas, &widget->skin.slot, inner);
    const ui_rect_t seen = ui_rect_intersect(rect, canvas->clip);
    if (!ui_rect_contains(seen, widget->input.pointer_x, widget->input.pointer_y)) {
        return 0u;
    }
    ui_compose_image_whole(canvas, &widget->skin.slot_highlight, inner);
    return UI_SLOT_HOVER | (widget->input.left_pressed != 0u ? UI_SLOT_LEFT_PRESSED : 0u) |
           (widget->input.right_pressed != 0u ? UI_SLOT_RIGHT_PRESSED : 0u);
}

void ui_widget_progress(ui_widget_t *widget, ui_rect_t rect, int32_t value, int32_t maximum) {
    ui_canvas_t *const canvas = widget->borrowed_canvas;
    ui_compose_image_whole(canvas, &widget->skin.progress_empty, rect);
    if (maximum <= 0 || value <= 0 || rect.width <= 0) {
        return;
    }
    const int64_t clamped = value > maximum ? maximum : value;
    /* Clipped, not squeezed: the fill's own pixels stay where they are as it grows. */
    const int32_t filled = (int32_t)((int64_t)rect.width * clamped / maximum);
    const ui_rect_t shown = {rect.x, rect.y, filled, rect.height};
    const ui_rect_t outer = ui_canvas_clip_set(canvas, ui_rect_intersect(shown, canvas->clip));
    ui_compose_image_whole(canvas, &widget->skin.progress_full, rect);
    (void)ui_canvas_clip_set(canvas, outer);
}

void ui_widget_tooltip(ui_widget_t *widget, const char *text) {
    uint64_t length = strlen(text);
    if (length >= UI_WIDGET_TOOLTIP_MAX) {
        length = UI_WIDGET_TOOLTIP_MAX - 1u;
    }
    char *const kept = arena_alloc(widget->borrowed_arena_frame, length + 1u, alignof(char));
    memcpy(kept, text, length);
    kept[length] = '\0';
    widget->borrowed_tooltip = kept;
}
