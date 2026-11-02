# run/src/ui

**Purpose:** what the player sees on top of the world. The Owner of `compose`, `font` and `widget`, and of the screens later stages add.
**Tier:** Owner.
**Owns:** `owned_region`, 2 MiB, with `arena_tables` (1.5 MiB) and `arena_frame` (0.5 MiB) over it, and `owned_composite`, one frame's pixels, reserved the first time a picture of that size needs it and grown only if a larger one comes. A mode changes far less often than a frame comes, so the heap's counters stay flat in play: ctest `run_ui_font` checks it.
**Lifetime:** Process for `ui_t`, its region and `arena_tables`. Frame for `arena_frame`, reset by each `ui_frame_begin`.
**Threads:** `main`, as the plan's thread table says. Nothing here is safe from another thread.
**Public functions** (`run/include/ui/index.h`):

- `ui_init`, `ui_deinit`: reserve the region and read the font and the widgets' textures; release it all. Deinit is safe on a zeroed `ui_t`.
- `ui_frame_begin`: start a frame drawing straight onto pixels in ordinary memory.
- `ui_frame_begin_over`: start a frame over a finished picture, leaving the picture as it was.
- `ui_frame_end`: end it, and say what was drawn on.

Between begin and end, `ui->canvas`, `ui->font` and `ui->widget` are what to draw with.

**May be called by:** `run/src/main/` for the pair, and `run/src/game/` for the frame.
**Depends on:** `heap`, `arena`, `assert`, `texture`, and its parts below.

**Parts:** `compose`: rects, images, and every pixel through the blend kernel. `font`: the bitmap font. `widget`: panels, labels, buttons, checkboxes, sliders, text fields, slots, bars, tooltips and the pointer.

## The path of a frame

The plan's own words are "copy to pinned host memory, CPU composes HUD with NASM blend, copy into dumb buffer, page flip". Here:

```c
const uint8_t *picture = render_frame_take(frame);          /* or the last one again */
ui_frame_begin_over(ui, picture, width, height);            /* copies it: see below */
/* ...widgets, text, images on ui->canvas... */
render_frame_present(frame, ui_frame_end(ui, show_pointer), target);
```

The copy is there because `game_loop_frame` shows the same picture again whenever the gpu thread has no newer one. Drawing on the picture itself would leave last frame's UI in it, and a translucent widget blended twice comes out darker. Drawing on the dumb buffer is not an answer either, since reading it back to blend is what write-combined memory is worst at.

`game` does not call this yet: nothing draws until Stage 14.2's HUD, which `run/src/render/frame/README.md` puts in that spot, and a copy of every frame with nothing on it would only cost time. `ui_init` and `ui_deinit` do run in the program, from `app_t`.
