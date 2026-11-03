# run/src/game/session

**Purpose:** the game from the title screen to Quit Game: runs the menus, opens the world they choose, pauses it and closes it.
**Tier:** Arena by depth, and it allocates nothing. Whatever it opens is owned by `app_t`'s `game`, `ui` and `terrain`.
**Owns:** nothing.
**Lifetime:** One run of the program, as a local of `main`.
**Threads:** `main`. `game_loop_*` starts the others.
**Public functions** (`run/include/game/session/index.h`):

- `game_session_init`, `game_session_deinit`: remember what it runs on, and go to the main screen; forget it.
- `game_session_settings_set`: its Settings buttons work, from these settings, and each world opens with what they say.
- `game_session_source`, `game_session_discard`: what `display_frames_run` calls.
- `game_session_run`: all of it through KMS.
- `game_session_headless_scene`, `game_session_end`: all of it for `display_headless_run`, and closing what is left.

**May be called by:** `run/src/main/`.
**Depends on:** `game`, `ui`, `ui/menu` and `ui/screen_settings`, `settings`, `terrain`, `input`, `display`, `simd/copy`, `log`, `assert`.

## How it goes

| State | Each frame | Goes to |
|---|---|---|
| Menu | Menus on a blank frame | Playing, on Play; done, on Quit Game |
| Playing | One real frame of `game` | Paused, on Esc |
| Paused | The pause screen over the last picture | Playing, on Resume or Esc; Menu, on Quit to Title |

**It all runs on one taking of the console:** one `display_frames_run`, one guardian. Giving the console back between a menu and a world is not an option: `display_flip_restore` and `display/vt_guard`'s restore steps each run once for a run of the program, on purpose, and each taking holds the emergency notice up for three seconds with keys going to the shell in between. The first try at this stage did that, and `the_whole_round_...` found it leaving the console in graphics mode. See the stage file's issue 1.

**So a world opens and closes inside a frame**, the only frames that take long or log, against what `display_frame_source_t` asks of a source:

- First the loading screen, in both buffers, so that it is on the screen whichever is showing. It stays there until the world's first picture lands in that buffer.
- Then that frame: `terrain_deinit` and `terrain_init` with the chosen seed, as `run/include/terrain/index.h` says another seed takes, and `game_open`, which starts the player, inventory and drops anew. The guardian's watchdog ends the program if it hears nothing for 8 seconds, and an all-ocean seed can take longer than that to find dry land, up to 16641 columns, so `game_open_alive_set` keeps it told once for each ring of that search. The log says how long it took.
- A log line on a console in graphics mode does no harm: it is not drawn, and it is there when the console comes back. Nothing else logs but a change of settings going through.
- `game_loop_threads_ensure` starts the threads on the next frame, after the guardian's fork as under `--test-world`, and what is pinned for this world is pinned after that fork too.

**A pause stops the world's time and nothing else.** `game_pause_set` stops the sim's ticks and the day. Columns go on streaming, and pictures already being drawn finish. The gpu thread waits when no one takes its pictures, so a pause costs almost nothing on the card. On pausing, everything held is let go, so nothing is still walking or mining in the one tick before the first frame after Resume says otherwise. The same Esc that pauses cannot resume: `escape_used`.

**Under `--headless`** the script is the input, as what `input_poll` would have said about each tick, and it all happens on one thread: a world opens between two ticks, with no loading screen since nothing can see one, and a pause simply skips `game`'s tick and draw. Nothing reads a clock, but the log's line of how long a world took. After Quit Game the rest of the script does nothing and the frame stays as it was, since a scene can only end a run by failing.

## Settings

The Settings button, on the main screen or a pause, brings up `run/src/ui/screen_settings/` in the same frames, over whichever screen brought it up. The screen changes `settings_t.values`, and this hands them on:

- A change waits `GAME_SESSION_APPLY_FRAMES` frames with no other change and no hand on a slider, then becomes a `game_config_t` and goes to `game_reconfigure`, in the frame's end, between frames. That is what makes render distance live: the same world goes on, with its player, its edits within reach and its time.
- Behind this screen, in a pause, each frame takes the newest picture with `game_view_refresh`, so a person sees what a change has done and the gpu thread keeps uploading. With no threads that is a whole settle and a draw, once for each change. Behind any other screen a pause stays as it was: one picture, costing nothing.
- Done or Esc applies what is still waiting and keeps them with `settings_save`. If that fails it stays up and says why, and a second Done leaves anyway: the values are still in force for this run, and no one should be held on a screen by a full disk.
- A world that opens later opens from the values, with a table sized for its full detail.

`tests/run/ui/menu/` runs the whole round on fake kernels with a real guardian forked. Only Ahmed can run it on a real console.
