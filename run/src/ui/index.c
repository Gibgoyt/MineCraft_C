/* See README.md. The Owner: it holds the memory its parts borrow, and decides when that dies. */
#include "ui/index.h"

#include "assert/index.h"

#include <string.h>

enum : uint64_t {
    UI_REGION_BYTES = 2ull << 20,
    UI_ARENA_TABLES_BYTES =
        3ull
        << 19, /* 1.5 MiB: the widgets' textures take 0.4, the row 64 KiB, the HUD's and every item's 0.3, and later parts the rest */
    UI_ARENA_FRAME_BYTES = UI_REGION_BYTES - UI_ARENA_TABLES_BYTES,
};
static_assert((uint64_t)UI_WIDGET_TOOLTIP_MAX <= (uint64_t)UI_ARENA_FRAME_BYTES);

bool ui_init(ui_t *ui, const texture_t *texture) {
    memset(ui, 0, sizeof(*ui));
    if (!heap_reserve(UI_REGION_BYTES, &ui->owned_region)) {
        return false; /* it has said why */
    }
    arena_init(&ui->arena_tables, "ui_tables", ui->owned_region.owned_base, UI_ARENA_TABLES_BYTES);
    arena_init(&ui->arena_frame, "ui_frame", ui->owned_region.owned_base + UI_ARENA_TABLES_BYTES, UI_ARENA_FRAME_BYTES);
    ui->borrowed_row = arena_alloc(&ui->arena_tables, UI_CANVAS_ROW_BYTES, alignof(uint32_t));
    if (!ui_font_init(&ui->font, &ui->arena_tables, texture) ||
        !ui_widget_init(&ui->widget, &ui->arena_tables, texture, &ui->font, &ui->arena_frame) ||
        !ui_hud_init(&ui->hud, &ui->arena_tables, texture)) {
        ui_deinit(ui);
        return false; /* it has said why */
    }
    return true;
}

void ui_deinit(ui_t *ui) {
    ui_hud_deinit(&ui->hud);
    ui_widget_deinit(&ui->widget);
    ui_font_deinit(&ui->font);
    arena_deinit(&ui->arena_frame);
    arena_deinit(&ui->arena_tables);
    heap_release(&ui->owned_composite);
    heap_release(&ui->owned_region);
    memset(ui, 0, sizeof(*ui));
}

bool ui_frame_begin(ui_t *ui, uint8_t *pixels, uint64_t pitch, uint32_t width, uint32_t height) {
    ASSERT(ui->widget.borrowed_canvas == nullptr);
    if (!ui_canvas_start(&ui->canvas, pixels, pitch, width, height, ui->borrowed_row)) {
        return false;
    }
    arena_reset(&ui->arena_frame);
    ui_widget_frame_begin(&ui->widget, &ui->canvas);
    return true;
}

bool ui_frame_begin_over(ui_t *ui, const uint8_t *picture, uint32_t width, uint32_t height) {
    const uint64_t pitch = (uint64_t)width * sizeof(uint32_t);
    const uint64_t bytes = pitch * height;
    if (bytes == 0u || width > UI_CANVAS_MAX_WIDTH) {
        return false;
    }
    /* A size that has been seen before costs nothing: a mode changes far less often than a frame comes. */
    if (bytes > ui->owned_composite.bytes) {
        heap_release(&ui->owned_composite);
        if (!heap_reserve(bytes, &ui->owned_composite)) {
            return false; /* it has said why */
        }
    }
    memcpy(ui->owned_composite.owned_base, picture, bytes);
    return ui_frame_begin(ui, ui->owned_composite.owned_base, pitch, width, height);
}

const uint8_t *ui_frame_end(ui_t *ui, bool show_pointer) {
    ui_widget_frame_end(&ui->widget, show_pointer);
    return ui->canvas.borrowed_pixels;
}
