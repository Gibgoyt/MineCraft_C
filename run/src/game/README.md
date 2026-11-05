# run/src/game

**Purpose:** connects the sim, gpu and main threads: streams the world around the eye, uploads it, draws it and shows it, with each system's tick and frame calls.
**Tier:** Owner.
**Owns:** `owned_region`, with `arena_world`, `arena_tick` and `arena_frame` over it. The first holds the column jobs and their scratch, reset at `game_close`. The other two are the Tick and Frame arenas of `plans/CODING_STANDARD.md` section 4, reset at those boundaries, and nothing takes from them until Stages 11 to 15.
**Lifetime:** `game_open` to `game_close`: World. `game_t` itself is Process, under `app_t`.
**Threads:** `main` calls everything but what runs on the two threads it starts, `sim` and `gpu`, or does all of it itself with no threads.
**Public functions** (`run/include/game/index.h`):

- `game_init`, `game_deinit`: remember what it runs on, taking nothing; stop everything. Deinit is safe on a zeroed `game_t`.
- `game_open`, `game_close`: a world, its blocks on the card, and its pictures, with the effects `game_config_t.features_off` leaves on.
- `game_reconfigure`: the same world goes on with another config.
- `game_view_refresh`, `game_picture_held`: what a changed config has done to the picture, and the last picture at the display's size, for what shows one with no frame going.
- `game_day_set`: the time of day, from where the clock goes on. Mid-morning unless told otherwise.
- `game_sim_step`, `game_fly`, `game_settle`: a step of streaming, a step of flying, and running the sim and upload here until the card holds all the eye's place wants.
- `game_headless_scene`: what `display_headless_run` drives. One thread, no clock, and one script gives one set of pictures.
- `game_loop_start`, `game_loop_frame`, `game_loop_stop`: the real threads, and a frame on the main thread.
- `game_loop_source_begin`, `game_loop_source`, `game_loop_discard`: what `display_frames_run` calls: the threads start on its first frame, then real keys and a real mouse to `game_loop_frame`, and Esc to stop.
- `game_loop_threads_ensure`, `game_loop_input_frame`, `game_loop_input_clear`: the same in parts, for `session`, which wants Esc for itself.
- `game_pause_set`: stops the world's clock, or starts it.
- `game_far_changes_take`: what has been captured, told to the far field, by whichever thread calls `render_lod_frame`.
- `game_save_finish`: all there is, saved here and now, as a world closes: see `run/src/save/autosave/`. A `game_open` with a save takes up where the state in it left off, and `game_sim_step` calls `save_autosave_step`.
- `game_open_alive_set`, `game_open_alive_tell`: someone to tell that a long `game_open` is still going, and telling them.
- `game_gpu_alive`, `game_gpu_lost`, `game_gpu_stuck`, `game_gpu_failed_note`, `game_gpu_pretend_set`: when the card goes. See below.
- `game_fly_from_script`, `game_fly_from_input`: either kind of input to a step of flying.
- `game_player_set`, `game_walk_send`, `game_hands_send`: walk through the world with this player and not fly; what the mouse and keys want of it; what its two buttons are doing.
- `game_entity_set`: what the player breaks comes out as items in the world, which it can pick up, and mobs come and go in it by `run/src/mob/spawn/`, which `game_t` holds, and each is drawn by the tick that moved it, a box for each part in its own skin.
- `game_audio_set`: where what the hands did this tick is heard, from the player's ear and the way it faces. Taken from the ring each tick with or without it, so that a run with sound goes the same as one without.
- `game_mob_set`: `run/src/mob/` opens and closes with the world, and has a tick before the bodies move. What its zombies strike is the player, through `player_survival_hurt`. Skeletons think just before they move and shoot at the player; arrows fly just after, and are drawn. Creepers think, with the player in view, after the spawner and before the bodies move. A press of the left button on a body in reach is a blow on it through `run/src/mob/combat/`, before the hands and with what is in hand, which it wears; its fall and lava come after the bodies move.
- `game_item_set`: what it picks up goes into an inventory, and the hotbar slot in hand is what it places.
- `game_hotbar_send`: the number keys and the wheel.
- `game_ui_set`: draw on each picture shown with this `ui_t`, and the game screens come up on it.
- `game_profiler_set`: what the three threads and the card do is timed in lanes of this `run/src/profiler/`. With no threads, main's lane has all of it.
- `game_screen_input`, `game_screen_up`: a poll's input for the game screens, and whether one is up, which then has Esc.
- `game_input_from_script`: a script's tick as a poll of real input.
- `game_debug_send`: the debug overlay's key, F3.
- `game_hud_state_read`, `game_hud_debug_read`, `game_hud_block_read`: what the HUD and the overlay show now, and where the body is.
- `game_hud_position_publish`: tells main where the body is, each tick and as a world opens.
- `game_report`: what the frames took, and the books of what was dropped.

**May be called by:** `run/src/main/`, and its part `session`.
**Depends on:** `mob`, `player`, `entity`, `item`, `ui`, `render`, `world`, `terrain`, `display`, `input`, `texture`, `job`, `thread`, `time`, `heap`, `arena`, `log`, `assert`.

**Parts:** `tick`: one tick of the world, `game_sim_tick`. `spawn`: where a new world's body starts, `game_ground_z` and `game_spawn_find`, which `bench/world_map/` asks too. `session`: from the title screen to Quit Game, on top of the rest of this. `view`: what a config comes to, and bringing a picture up to the display's size. `report`: what it says about itself. `input`: what a person's hands come to. `settle`: everything on the card, on one thread.

A column's blocks are `terrain_generate_box`, on a worker, over all of the column's storage. Each job lends it the states and a byte per voxel.

## The threads

| Thread | Does | Tells the others through |
|---|---|---|
| `main` | Input, moves the eye, keeps the time of day, shows the newest picture | A triple buffer of cameras to `gpu`; a relaxed atomic, the time, to `gpu` and `sim`; two relaxed atomics, the eye's chunk, to `sim` |
| `sim` | Columns to and from workers, into the map, their light and every edit's, then a snapshot. A tick at 20 Hz, which moves the player if there is one and runs `world/tick` | The snapshots of `world/edit` to `gpu`; each tick's eye and the body's health to `main`, through `player`; a relaxed atomic, that a night has been slept through, to `main` |
| `gpu` | The upload queue, then the frame's kernels | A triple buffer of pictures to `main` |

None of them ever waits for another, as the plan wants: a slow tick cannot stall a frame, and a slow frame cannot stall input.

A sim step spends at most `GAME_COMMIT_BUDGET_NS` on finished columns, since each gets its light there. The rest wait for the next step, so snapshots keep going out.

With no threads, as under `--headless`, `game_settle` does a sim step and an upload round in turn until all of it is on the card, before each frame. So a picture does not depend on how fast a worker was, and the same script always gives the same bytes. It is the same code as the threads run, in another order.

`tests/run/render/frame/` runs the threads for real, on a plain buffer for a display, and `tests/run/game/` through `display_frames_run` on a fake kernel with a real guardian forked. Only Ahmed can run them on KMS.

## Walking

With `game_player_set`, as `--walk` does, the eye stops flying and follows a body. See `run/src/player/README.md`.

- The body starts on the nearest dry land to the world's middle, found by looking at single columns in widening squares, and waits there for its column. Flying starts over the same place, as a range can stand at the middle itself.
- The mouse turns the camera on `main` at once. The keys go to `sim`, and move the body on its next tick. `main` draws from where the last two ticks say, between them.
- Under `--headless` it is the same code on one thread, with time counted in ticks. Each tick settles the world first, so that the body does not depend on how fast a worker was, and is drawn at the tick's end.
- What it aims at is worked out on each tick, with the look `main` last sent. After each tick the sim publishes it, with how far along mining is and where each item lies, to `run/src/render/entities/`, and whichever frame comes next draws them: the outline and the crack are in the picture by the time `main` sees it. The `gpu` thread still leaves each picture's camera beside it in `picture_cameras`, and `main` reads it as `shown_camera`.
- Each tick the hands act first, on what the last tick aimed at, then the body moves: the left button mines and the right places, all on `sim`, through `world/edit`, so that light, the renderer and `world/tick` see them as any other edit. See `run/src/player/interaction/README.md`.
- With `game_entity_set`, as `--walk` also does, what the hands break, and what `world_tick_broken_next` says went by itself, goes through the player's ring into `run/src/entity/` on that same tick, before the body moves. Then everything there moves, and the body picks up what it now reaches. `game` joins the two, so that `entity` knows nothing of a player. See `run/src/entity/item_drop/README.md`.
- With `game_item_set`, as `--walk` also does, what the body picks up goes into `run/src/item/`, and `game` joins that to the hands too: before each tick the hand is set to the hotbar slot chosen, and what the tick placed comes off that slot. So neither knows of the other. The inventory opens and closes with the world, and nothing is saved. Each tick after the hands, `item_world_tick` gives chests and furnaces their slots, burns furnaces, and throws out what a broken one held, through `game_spill`. The same join carries tools and armor: before the hands' tick the right button goes to `equipment_use_tick`, which puts armor on and tills with a hoe, and the slot in hand becomes what the hands mine with, a change of tool starting the block again. After it, what they broke wears that slot, and shears bring the block out as well. See `run/src/item/equipment/README.md`.
- With a player, `game` is also what joins `run/src/player/survival/` to everything else. Before each tick it tells it the time of day and what food is in hand, and what it ate comes off that slot, a stew leaving its bowl. After each tick, one that has died has all it carried and what is in its crafting grids thrown out through `game_spill`, and picks nothing up until it is back. A night slept through sets `morning_latch`, which main turns into `game_day_set` in `game_day_advance`.
- With `game_ui_set`, as `--walk` also does, `game` joins `run/src/ui/`'s game screens to all that. On `main`, each poll goes to `game_screen_input` first: E opens the inventory, E or Esc closes what is up, and while one is up the mouse is its pointer, the body gets no keys and no buttons but is told someone is at the controls, and Esc does not end the run. Each frame's picture goes through `game_hud_compose`, which draws what is up on top of the HUD with `ui_screen_draw`. On the `sim`, each step does what the screen's ring holds, so that a click shows in milliseconds; each tick, before the hands act, the right button as it goes down on a crafting table, furnace or chest brings that one up, unless the player sneaks, and `player_t.hands_held_off` keeps the hands from acting on it or under a screen. Each tick after the hands, a screen whose block has gone closes, and one that is up is told how things stand. A body that dies closes it first, so that what it had out comes out with the rest. A world with no `game_ui_set` runs as it did, and a picture with no screen up is what the HUD alone makes of it.
- A world whose render distance is past `game_config_t.full_detail` draws the rest as coarse grids, in `run/src/render/lod/`. The map and the sim never hear of it: the map loads only what is drawn block by block, and the gpu thread, or `game_settle` with no threads, keeps the grids up with the eye.
- Without a player everything flies as before.

## The HUD

`main` draws it, as the plan's thread table says, in `game_hud_compose`, on a copy of whichever picture is going out, because that same picture may go out again: see `run/src/ui/README.md`. With a player it draws `run/src/ui/hud/`, and with F3 on, in any run, the debug overlay. With neither it does no copy at all, so the pictures of a run that flies are the same bytes they were. Under `--headless` F3 comes from the script.

What it shows comes from the other threads as single relaxed atomics, because nothing but the screen depends on them and a word cannot tear. Each tick the sim stores each hotbar stack as one word, and which is in hand; `player_vitals_read` is the same idea for health, food and air. Whether the eyes are under water goes the same way, to the thread that draws, as `render_frame_under_water_set` before each picture. After each picture the gpu thread stores what each pass took, as the difference that picture made to `render_frame_stats_t`. It reads `stats` itself, as the only thread that writes it. Slots from two ticks can show side by side for one frame, which nobody could see. Where the body is goes the same way, as three words, and for the same reason: x from one tick beside y from the one before is gone by the next frame. `game_config_t.show_coordinates` says whether they are drawn in the corner. They are not while F3 is up, as the overlay has them too, on a panel in that corner.

With a real clock the sim's tick, the copy and drawing, and copying out are timed too. Under `--headless`, nothing reads a clock: those stay 0, and frames per second count pictures per tick's worth of script. A run with the overlay on in `--headless` is the one kind whose pictures are not the same each time, as they show the card's clock.

## Changing a config in an open world

`game_reconfigure` is how render distance, and everything else on the Settings screen, takes effect without closing anything. It is called on `main` between frames.

What needs only a word goes at once: field of view into the camera, published there and then since `game_loop_frame` publishes none in a pause, sensitivity for the next counts, and uncapped to the gpu thread as a relaxed atomic. A config that comes to the same `game_view_t` stops at that.

Anything else stops the threads, changes what they use, and starts them again. That is a frame's wait for the gpu thread, against the alternative of those two taking blocks on the card away from each other under a lock neither holds. Between the stop and the start, in order:

1. **Render scale:** the last picture is copied out to `owned_view`, since its memory is about to go and a pause may be showing it. `render_close_frame` and `render_open_frame` at the new size, on the display's size if it comes to that, by `game_view_bring_up` each frame before the HUD. If the card will not have it, the size stays.
2. **Far field:** a different plan closes the one there is and opens one anew. What it held is rebuilt by workers, from the coarsest level, as at a world's opening: a level still filling makes the picture coarser and never wrong.
3. **Map:** `world_map_set_view_radius`, with no thread holding it. Columns now out of range leave through the sim's next steps as they do when walking away, and those now in range stream in, nearest first. The columns still in range stay, with whatever has been done to them.
4. **Fog and effects:** into the frame, for the next one.

No world is closed, no player or item is touched, and the world's time goes on. Full detail cannot go past `game_config_t.table_chunks`: the table, the column table on the card and the slab pool are sized at `game_open`, so `session` opens every world with room for what full detail was then, however little of that the render distance uses. So a render distance of 4 with a full detail of 32 holds 1.8 GiB of slabs on the card, for the moment the slider goes up. What is not the session asks for no such room, as its tests always did.

If the far field will not fit on the card, the world stops at full detail, the fog comes in to meet it, and it says so. Only a frame that cannot be had back, or threads that will not start, ends the game.

## The time of day

The far field samples terrain and writes over it what the save holds: `save_world_far_line`, set by `game_open` and `game_reconfigure`. It hears of what is captured from `game_far_changes_take`, just before each `render_lod_frame`. So an edit reaches the far field when it is captured: at the next autosave, within 30 s of world time, or when its column leaves, whichever is first. Until then it is near enough to be drawn block by block in all but the ring the map keeps past `full_detail`. A game with no save has no overlay, and only terrain out there: only `--menu` has a save.

A world opened through `game/session` has a `save_t` as well: `game_save_set` before it opens. Each column job then writes what is saved over what it generates, and `game_sim_step` captures a column's changes before it unloads it. If that fails, the column stays, and is offered again next step. See `run/src/save/world/README.md`.

One day is 1200 seconds, of whatever `game_loop_frame` or a script's ticks say has passed, so a script's pictures depend on its ticks and on nothing else. `main` keeps it, as a real frame already knows how long it took, and the gpu thread reads it before each frame. A frame out of date by one is the sun a few millionths of a day behind, so a relaxed atomic is plenty. `SHADE_FEATURE_DAY_CYCLE` off stops the clock where it is. A save's time of day goes through `game_day_set` when a world opens: `run/src/save/state/`.

## The guardian's fork

Under KMS, `display/vt_guard` forks its guardian as the console is taken, with CUDA live and the pictures already pinned. So:

- The sim and gpu threads start on the first frame, after that fork, so nothing of theirs is mid-write as it happens. The job workers are already running then, with nothing to do.
- The guardian touches nothing of CUDA's and leaves by `_exit`, so no exit handler of the runtime's runs in it.
- A pinned page must stay the parent's across a fork, or the card would go on writing to a page the program no longer sees. The kernel keeps it so for pins taken with `FOLL_PIN`. Whether this driver's are was measured, not assumed: `a_world_runs_through_the_display_loop_with_a_real_guardian_forked` draws through pictures pinned before the fork, with the guardian alive, and compares with a picture made before it.

## Why `--headless` alone still shows the demo

Plain `--headless` stays what `tests/run/display/headless/` tests, with no world and no card work: `--headless --test-world` is this. The line in `run/src/display/headless/README.md` that said `game` would replace the demo was written before there was a reason to keep a run that needs no card.

## When the card goes

CUDA's errors from a fault are sticky, so nothing after one can draw, and only a new process has a context again. Whatever thread is talking to the card says so with `game_gpu_failed_note` and stops, and `failed` ends the run through the path it already had. What a save needs is all on the CPU's side, so the session still saves the world, and `device_memory_lost_set` makes everything that gives blocks back do so without asking a runtime that can only say the same thing again. `game_gpu_lost` is how `main` comes to exit 9.

A card can also hang and say nothing, with the gpu thread inside a call that never comes back. Main would go on showing the last picture for ever, and `game_loop_stop` would join for ever. So the gpu thread counts its rounds, idle or not, and `game_gpu_alive` runs on main each frame: `GAME_GPU_STUCK_NS` with no round is `game_gpu_stuck`. That thread is never joined. The sim is, the world is saved, the console has gone back, and then nothing that thread may be using can be given back, so `game_close` does not run and the program ends by `_exit`. If main itself was away, on another VT or stopped, what it did not see proves nothing, so a gap of a second starts the count again.

`--pretend-gpu-lost <pictures>` makes `device_memory_fault_now` run in the draw of that picture: a real fault on the real card, in this process's context only.
