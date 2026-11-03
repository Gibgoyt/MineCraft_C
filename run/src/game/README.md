# run/src/game

**Purpose:** connects the sim, gpu and main threads: streams the world around the eye, uploads it, draws it and shows it. Later stages add their tick and frame calls here.
**Tier:** Owner.
**Owns:** `owned_region`, with `arena_world`, `arena_tick` and `arena_frame` over it. The first holds the column jobs and their scratch, reset at `game_close`. The other two are the Tick and Frame arenas of `plans/CODING_STANDARD.md` section 4, reset at those boundaries, and nothing takes from them until Stages 11 to 15.
**Lifetime:** `game_open` to `game_close`: World. `game_t` itself is Process, under `app_t`.
**Threads:** `main` calls everything but what runs on the two threads it starts, `sim` and `gpu`, or does all of it itself with no threads.
**Public functions** (`run/include/game/index.h`):

- `game_init`, `game_deinit`: remember what it runs on, taking nothing; stop everything. Deinit is safe on a zeroed `game_t`.
- `game_open`, `game_close`: a world, its blocks on the card, and its pictures, with the effects `game_config_t.features_off` leaves on.
- `game_day_set`: the time of day, from where the clock goes on. Mid-morning unless told otherwise.
- `game_sim_step`, `game_fly`, `game_settle`: a step of streaming, a step of flying, and running the sim and upload here until the card holds all the eye's place wants.
- `game_headless_scene`: what `display_headless_run` drives. One thread, no clock, and one script gives one set of pictures.
- `game_loop_start`, `game_loop_frame`, `game_loop_stop`: the real threads, and a frame on the main thread.
- `game_loop_source_begin`, `game_loop_source`, `game_loop_discard`: what `display_frames_run` calls: the threads start on its first frame, then real keys and a real mouse to `game_loop_frame`, and Esc to stop.
- `game_fly_from_script`, `game_fly_from_input`: either kind of input to a step of flying.
- `game_player_set`, `game_walk_send`, `game_hands_send`: walk through the world with this player and not fly; what the mouse and keys want of it; what its two buttons are doing.
- `game_entity_set`: what the player breaks comes out as items in the world, which it can pick up.
- `game_item_set`: what it picks up goes into an inventory, and the hotbar slot in hand is what it places.
- `game_hotbar_send`: the number keys and the wheel.
- `game_ui_set`, `game_screen_input`: the game screens come up, on this ui; a poll's input for them.
- `game_input_from_script`: a script's tick as a poll of real input.
- `game_report`: what the frames took, and the books of what was dropped.

**May be called by:** `run/src/main/`.
**Depends on:** `ui`, `player`, `entity`, `item`, `render`, `world`, `terrain`, `display`, `input`, `texture`, `job`, `thread`, `time`, `heap`, `arena`, `log`, `assert`.

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

- The body starts on the nearest dry land to the world's middle, found by looking at single columns in widening squares, and waits there for its column. Flying still starts over the middle, in or out of water.
- The mouse turns the camera on `main` at once. The keys go to `sim`, and move the body on its next tick. `main` draws from where the last two ticks say, between them.
- Under `--headless` it is the same code on one thread, with time counted in ticks. Each tick settles the world first, so that the body does not depend on how fast a worker was, and is drawn at the tick's end.
- What it aims at is worked out on each tick, with the look `main` last sent. After each tick the sim publishes it, with how far along mining is and where each item lies, to `run/src/render/entities/`, and whichever frame comes next draws them: the outline and the crack are in the picture by the time `main` sees it. The `gpu` thread still leaves each picture's camera beside it in `picture_cameras`, and `main` reads it as `shown_camera`.
- Each tick the hands act first, on what the last tick aimed at, then the body moves: the left button mines and the right places, all on `sim`, through `world/edit`, so that light, the renderer and `world/tick` see them as any other edit. See `run/src/player/interaction/README.md`.
- With `game_entity_set`, as `--walk` also does, what the hands break, and what `world_tick_broken_next` says went by itself, goes through the player's ring into `run/src/entity/` on that same tick, before the body moves. Then everything there moves, and the body picks up what it now reaches. `game` joins the two, so that `entity` knows nothing of a player. See `run/src/entity/item_drop/README.md`.
- With `game_item_set`, as `--walk` also does, what the body picks up goes into `run/src/item/`, and `game` joins that to the hands too: before each tick the hand is set to the hotbar slot chosen, and what the tick placed comes off that slot. So neither knows of the other. The inventory opens and closes with the world, and nothing is saved. Each tick after the hands, `item_world_tick` gives chests and furnaces their slots, burns furnaces, and throws out what a broken one held, through `game_spill`. The same join carries tools and armor: before the hands' tick the right button goes to `equipment_use_tick`, which puts armor on and tills with a hoe, and the slot in hand becomes what the hands mine with, a change of tool starting the block again. After it, what they broke wears that slot, and shears bring the block out as well. See `run/src/item/equipment/README.md`.
- With a player, `game` is also what joins `run/src/player/survival/` to everything else. Before each tick it tells it the time of day and what food is in hand, and what it ate comes off that slot, a stew leaving its bowl. After each tick, one that has died has all it carried and what is in its crafting grids thrown out through `game_spill`, and picks nothing up until it is back. A night slept through sets `morning_latch`, which main turns into `game_day_set` in `game_day_advance`.
- With `game_ui_set`, as `--walk` also does, `game` joins `run/src/ui/`'s game screens to all that. On `main`, each poll goes to `game_screen_input` first: E opens the inventory, E or Esc closes what is up, and while one is up the mouse is its pointer, the body gets no keys and no buttons but is told someone is at the controls, and Esc does not end the run. Each frame's picture goes through `ui_screen_over`, which leaves it alone with nothing up. On the `sim`, each step does what the screen's ring holds, so that a click shows in milliseconds; each tick, before the hands act, the right button as it goes down on a crafting table, furnace or chest brings that one up, unless the player sneaks, and `player_t.hands_held_off` keeps the hands from acting on it or under a screen. Each tick after the hands, a screen whose block has gone closes, and one that is up is told how things stand. A body that dies closes it first, so that what it had out comes out with the rest. A world with no `game_ui_set` runs as it did, and a picture with no screen up is the same bytes.
- A world whose render distance is past `game_config_t.full_detail` draws the rest as coarse grids, in `run/src/render/lod/`. The map and the sim never hear of it: the map loads only what is drawn block by block, and the gpu thread, or `game_settle` with no threads, keeps the grids up with the eye.
- Without a player everything flies as before, so the golden pictures of `tests/run/render/frame/` stay as they were.

## The time of day

One day is 1200 seconds, of whatever `game_loop_frame` or a script's ticks say has passed, so a script's pictures depend on its ticks and on nothing else. `main` keeps it, as a real frame already knows how long it took, and the gpu thread reads it before each frame. A frame out of date by one is the sun a few millionths of a day behind, so a relaxed atomic is plenty. `SHADE_FEATURE_DAY_CYCLE` off stops the clock where it is. When the sim has a world time of its own to keep (Stage 16.1 saves one), it should move there.

## The guardian's fork

Under KMS, `display/vt_guard` forks its guardian as the console is taken, with CUDA live and the pictures already pinned. So:

- The sim and gpu threads start on the first frame, after that fork, so nothing of theirs is mid-write as it happens. The job workers are already running then, with nothing to do.
- The guardian touches nothing of CUDA's and leaves by `_exit`, so no exit handler of the runtime's runs in it.
- A pinned page must stay the parent's across a fork, or the card would go on writing to a page the program no longer sees. The kernel keeps it so for pins taken with `FOLL_PIN`. Whether this driver's are was measured, not assumed: `a_world_runs_through_the_display_loop_with_a_real_guardian_forked` draws through pictures pinned before the fork, with the guardian alive, and compares with a picture made before it.

## Why `--headless` alone still shows the demo

Plain `--headless` stays what `tests/run/display/headless/` tests, with no world and no card work: `--headless --test-world` is this. The line in `run/src/display/headless/README.md` that said `game` would replace the demo was written before there was a reason to keep a run that needs no card.
