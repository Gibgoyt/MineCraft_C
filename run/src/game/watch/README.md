# run/src/game/watch

**Purpose:** plan 005 phase 7. Ahmed left the game in another window for a long time, came back to a very laggy game, and "there is nothing that even logs that". This is what says why: the F3 overlay's numbers for what a world left alone piles up, a line in the log when a frame or a tick is slow naming its slowest part, and a line a minute of how the ticks and frames went.
**Tier:** Arena by depth (2). It allocates nothing.
**Owns:** nothing. `game_watch_t` is three lines of the game's World arena beside the column jobs (`game_watch_of`, in `game/index.c`), as `game_t` is at its size and `app_t` at main's frame limit.
**Lifetime:** World. `game_watch_start` empties it as a world opens.
**Threads:** one line each. The sim's line is written by `game_watch_tick_note`, the gpu thread's by `game_watch_gpu_note` (from `game_watch_gpu_look`), and main's by `game_watch_frame_note` and `game_watch_frame_say` (from `game_watch_frame_look`). What another thread reads is atomic and relaxed: only the overlay and the log depend on it. The sim takes the minute's frame numbers from main's line by exchange; a frame that lands either side of that goes in the next minute.
**Public functions** (`run/include/game/watch/index.h`):

- `game_watch_start`: nothing seen yet.
- `game_watch_tick_note`: the sim's, after each tick that ran, with what each part took (`game_watch_tick_stage_t`: weather, hands, mobs, player, world, publish) and the mobs and drops in the world.
- `game_watch_gpu_note`, `game_watch_gpu_look`: the gpu thread's, after each upload: sections of the newest snapshot not yet dealt with, and far-field columns on workers.
- `game_watch_frame_note`, `game_watch_frame_say`, `game_watch_frame_look`: main's, after each frame of play.
- `game_watch_debug_read`: mobs, drops, uploads and far jobs for the overlay (`game_hud_debug_read`).
- `game_watch_tick_stage_name`: a part's name.
- `game_watch_kernels_note`: what the card's clock said a frame's kernels took, onto the profiler's card lane. It was in `game/index.c` until that file passed 1500 lines.

**May be called by:** `run/src/game/` and its parts.
**Depends on:** `game`, `ui/hud` (`ui_hud_debug_t`, `ui_hud_stage_name`), `render/frame`, `profiler`, `time`, `log`.

## What it says

**A slow tick** is one over 50 ms, the whole of its slot at 20 Hz. **A slow frame** is one over 33 ms, under 30 a second. Each says one line, at most once in 5 s, with how many more there were since the last line:

    watch: tick 31200 took 63.1 ms, and the slowest part was mobs at 55.0 ms (weather 0.1, hands 0.2, mobs 55.0, player 1.0, world 4.0, publish 0.3); 70 mobs, 12 drops; 3 more slow ticks since the last line
    watch: a frame took 48.2 ms, and the slowest part was shade at 31.0 ms (coarse 0.4, primary 2.3, shade 31.0, post 0.8, readback 0.4, gpu 40.1 in all, sim tick 2.0, hud 0.6, present 1.0, the rest of main's frame 5.3); 70 mobs, 12 drops, 0 sections waiting to upload, 16 far jobs; 4 more slow frames since the last line

A frame with threads is main's time from one frame to the next, which a pause and another VT start again. The card's passes are those of the gpu thread's last picture, drawn beside main and not inside its frame, so a slow pass there says the card is behind, not that main waited for it; "the rest of main's frame" is main's own time past the HUD and the copy out, which is the display's wait, input and the session. With no threads, as under `--headless`, a frame is the draw alone, and the tick before it is timed as a tick.

**Every minute of ticks** (1200 at 20 Hz) the sim says how they went, with the frames main drew meanwhile:

    watch: ticks 1 to 1200: a tick took 1.20 ms on average and 9.04 ms at most (mobs); 1180 frames took 8.10 ms on average and 40.20 ms at most; 70 mobs, 12 drops, 0 sections waiting to upload, 0 far jobs

A paused world has no ticks, so it says nothing until play goes on.
