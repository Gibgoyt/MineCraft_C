# run/src/ui

**Purpose:** what the player sees on top of the world. The Owner of `compose`, `font`, `widget`, `hud`, the game screens and `menu`, and of the screens later stages add.
**Tier:** Owner.
**Owns:** `owned_region`, 2 MiB, with `arena_tables` (1.5 MiB, of which about 0.95 is in use with the HUD's and the game screens' item pictures) and `arena_frame` (0.5 MiB) over it, and `owned_composite`, one frame's pixels, reserved the first time a picture of that size needs it and grown only if a larger one comes. A mode changes far less often than a frame comes, so the heap's counters stay flat in play: ctest `run_ui_font` checks it.
**Lifetime:** Process for `ui_t`, its region and `arena_tables`. Frame for `arena_frame`, reset by each `ui_frame_begin`.
**Threads:** `main`, as the plan's thread table says, and nothing here is safe from another thread, but for the `ui_screen_sim_*` functions, which are the `sim`'s: see "The game screens" below.
**Public functions** (`run/include/ui/index.h`):

- `ui_init`, `ui_deinit`: reserve the region and read the font and the widgets' textures; release it all. Deinit is safe on a zeroed `ui_t`.
- `ui_frame_begin`: start a frame drawing straight onto pixels in ordinary memory.
- `ui_frame_begin_over`: start a frame over a finished picture, leaving the picture as it was.
- `ui_frame_begin_blank`: start a frame on black, for screens with no world behind them.
- `ui_frame_end`: end it, and say what was drawn on.
- `ui_screen_world_begin`, `ui_screen_input`, `ui_screen_over`, `ui_screen_up`, `ui_screen_draw`, `ui_screen_kind_of_block`, `ui_screen_sim_kind`, `ui_screen_sim_cell`, `ui_screen_sim_open`, `ui_screen_sim_close`, `ui_screen_sim_step`: the game screens, below.

Between begin and end, `ui->canvas`, `ui->font`, `ui->widget`, `ui->hud` and `ui->menu` are what to draw with.

**May be called by:** `run/src/main/` for the pair, and `run/src/game/` for the frame.
**Depends on:** `heap`, `arena`, `assert`, `texture`, and its parts below.

**Parts:** `compose`: rects, images, and every pixel through the blend kernel. `font`: the bitmap font. `widget`: panels, labels, buttons, checkboxes, sliders, text fields, slots, bars, tooltips and the pointer. `hud`: the crosshair, hotbar, hearts, drumsticks and debug overlay. `screen_inventory`: the inventory, and what all the game screens share. `screen_crafting`, `screen_furnace`, `screen_chest`: the others. `menu`: the main menu, the list of worlds, making a world, and pause.

## The path of a frame

The plan's own words are "copy to pinned host memory, CPU composes HUD with NASM blend, copy into dumb buffer, page flip". Here:

```c
const uint8_t *picture = render_frame_take(frame);          /* or the last one again */
ui_frame_begin_over(ui, picture, width, height);            /* copies it: see below */
/* ...widgets, text, images on ui->canvas... */
render_frame_present(frame, ui_frame_end(ui, show_pointer), target);
```

The copy is there because `game_loop_frame` shows the same picture again whenever the gpu thread has no newer one. Drawing on the picture itself would leave last frame's UI in it, and a translucent widget blended twice comes out darker. Drawing on the dumb buffer is not an answer either, since reading it back to blend is what write-combined memory is worst at.

`game` does this in `game_hud_compose`, with `run/src/main/` giving it the `ui_t` through `game_ui_set`. A frame with nothing to draw, as when flying with the overlay off, skips the copy, since it would only cost time. A game screen goes on the same copy, over the HUD, by `ui_screen_up` and `ui_screen_draw`; `ui_screen_over` is the two with a copy of their own, for a screen alone. `run/src/game/session/` does this for the pause screen, and for the menus with `ui_frame_begin_blank`.

## The game screens

Everything a screen shows is the `sim`'s, and `main` draws. So they meet in `ui_screens_t` (`run/include/ui/screen_inventory/index.h`), one from this Owner's table arena:

| | `main` | `sim` |
|---|---|---|
| Asks and is told | `ui_screens_send`: a ring of clicks, opens and closes | `ui_screens_view_next`, `ui_screens_view_publish`: a triple buffer of whole copies |
| Calls | `ui_screen_input` for each poll, `ui_screen_up` and `ui_screen_draw` for each frame | `ui_screen_sim_step` in each step, with `tick` in each tick; `ui_screen_sim_open` for the right button on a block; `ui_screen_sim_close` for one that has gone or a body that has died |

So nothing here holds a lock or waits, and the sim stays the only one writing to an inventory.

Who calls them is `run/src/game/`, and only with a player and an inventory, as `--walk` has: see its README. A world starts with nothing up, by `ui_screen_world_begin`. The first screen a mode shows reserves `owned_composite`, once.

Closing brings home what was out: a grid's cells and the pointer go back to the inventory, and what has no room falls out through `ui_screen_sim_t.spill`.
