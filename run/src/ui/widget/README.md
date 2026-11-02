# run/src/ui/widget

**Purpose:** the parts screens are made of, in immediate mode: panels, labels, buttons, checkboxes, sliders, text fields, slots, bars, tooltips and the pointer.
**Tier:** Arena: their textures come from one arena its Owner gives, and a tooltip's text from another, for one frame.
**Owns:** nothing.
**Lifetime:** Process for the textures, and Frame for a tooltip's text.
**Threads:** `main`.
**Public functions** (`run/include/ui/widget/index.h`):

- `ui_widget_init`, `ui_widget_deinit`: read the textures; empty the handle.
- `ui_widget_scale_for`: how many screen pixels to a texture pixel.
- `ui_widget_input_feed`: what a poll found, from `input/events`.
- `ui_widget_frame_begin`, `ui_widget_frame_end`: start and finish a frame on a canvas. `ui_frame_begin` and `ui_frame_end` call these.
- `ui_widget_panel`, `ui_widget_label`, `ui_widget_label_centered`, `ui_widget_button`, `ui_widget_checkbox`, `ui_widget_slider`, `ui_widget_text_field`, `ui_widget_slot`, `ui_widget_progress`, `ui_widget_tooltip`.

**May be called by:** `ui` and the screens under it.
**Depends on:** `arena`, `assert`, `log`, `texture`, `input/events`, and its siblings `ui/compose` and `ui/font`, as their READMEs allow.

## How to use one

There is no widget object. A screen calls each widget it wants, each frame, in the order it should be drawn, and gets back what happened to it this frame:

```c
if (ui_widget_button(&ui->widget, MENU_ID_PLAY, rect, "Play", true)) { ... }
```

An id is the caller's choice, not 0, and the same from frame to frame. That is all the state there is: `active_id`, what a press landed on, and `focus_id`, the text field being typed in.

- **A click** is a press and a release both on the widget, even across frames. A press holds the widget until the release, so dragging over another does nothing to that one.
- **What cannot be seen** cannot be hovered: the part of a widget outside the clip does not count.
- **Widgets must not overlap.** The pointer acts on each widget it is over, because none knows of those drawn after it.
- **Input** is what `ui_widget_input_feed` was given since the last frame ended. Several polls may be fed to one frame, and the edges of all of them count. Keys go through a US layout. A poll says which keys, not in what order, so two inside one poll come out by key code, and Backspace before any character. This matters at under 8 ms between two keys, and in a world's name at worst swaps two letters.
- **The pointer** moves one pixel for each raw count, stays on the canvas, and starts in its middle. A canvas of a new size puts it back there.

## Sizes

`ui_widget_scale_for` picks the most screen pixels to a texture pixel that leaves 320 by 240 of room, which is the scale at 1920 by 1080 of 4. Text and corners follow it. A rect is always in screen pixels, and is the caller's to choose: `UI_WIDGET_BUTTON_HEIGHT * scale` is how tall a button's texture wants to be.

A slider's label is drawn on its track, because what a value means is the caller's: "Render distance: 32". A text field's caret is at the end of its text and does not blink, and nothing moves it. That is all naming a world needs.

Tested by ctest `run_ui_font`, on input frames made by the test, never a real device.
