# run/src/game/watch

**Purpose:** plan 005 phase 7. Ahmed left the game in another window for a long time, came back to a very laggy game, and "there is nothing that even logs that". This is what says why: the F3 overlay's numbers for what a world left alone piles up, a line in the log when a frame or a tick is slow naming its slowest part, and a line a minute of how the ticks and frames went.
**Tier:** Arena by depth (2). It allocates nothing.
**Owns:** nothing on the heap. Plan 008 phase 01: the frame log's file descriptor, and the marks' CUDA events (`frame_marks_init` in `game_watch_open`, given back in `game_watch_close`). `game_watch_t` is lines of the game's World arena beside the column jobs (`game_watch_of`, in `game/index.c`), as `game_t` is at its size and `app_t` at main's frame limit.
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
- Plan 008 phase 01, picture by picture: `game_watch_open`, `game_watch_close` (the rings, the marks and the frame log, as a world opens and closes); `game_watch_marks_arm` (whoever draws, before each picture: the marks, and the card's clock once a second); `game_watch_picture_begin`, `game_watch_stamp`, `game_watch_slot_note`, `game_watch_picture_end` (the gpu thread's); `game_watch_main_frame`, `game_watch_take_note`, `game_watch_display_note` (main's); `game_watch_rings_read` (the sim's, once a tick, for the minute line and the frame log); `game_watch_recent_read` (main's, for F3's last second).

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

## Picture by picture (plan 008 phase 01)

Ahmed, 2026-11-12: "only when shaking my head does frame rate tank; when walking around without my mouse/view frame rate sits nicely; i think F3 frame rate tab is also innacurate as fuck something laggy is happening and we need to figure out where". So every picture says where its time went.

**Two rings, one writer each,** in the World arena, `GAME_WATCH_RING` (4096) records each, 34 s at 120 a second:

- **Pictures** (`game_watch_picture_t`), the gpu thread's, one for every picture it draws, taken or not. Stamps on `time_now_ns`'s clock: the last picture done (from there it waits for main to take it), this one begun, after the upload, after the far field's plan, after fill and the day's light, all queued (`frame_marks_t.queued_ns`: from there it waits for the card), and ready. Then the card's passes, the far field's rays, shade's five kernels, the fresh AO count, the card's MHz at its last reading, and the turn speed: the angle between this picture's forward and the last one's, over the time between their starts. Counted in `pictures_drawn` with a release once whole.
- **Takes** (`game_watch_take_t`), main's, one for every picture main takes: which picture, main's frame counts when it began and at the take, when it was taken, and what the window last said (`game_watch_display_note`): the last put's send to completion, main's block waiting for it, and from that send to main's next wait, its sleep to the slot. Counted in `takes_made` with a release.

What main needs of a picture at the take, its number and when it began, goes through the exchange slot it was drawn into (`slots`), written before the draw, so the publish hands it over with the picture. A picture main took was **late** if it was taken past the slot after the one it began in: main's frame count at the take is more than one past its count at the start.

**The sim reads the takes** once a tick (`game_watch_rings_read`, from `game_watch_tick_note`), joining each to its picture, into the minute's numbers and the frame log's lines. A take whose picture the gpu thread has not counted yet waits for the next tick. One read so late that main is writing where it would read next skips what is gone and counts it (`takes_lost`): 34 s behind, which the sim never is.

**F3 reads the last second** of both rings on main (`game_watch_recent_read`), only what is under their counts and never as far back as where the next is written. See `run/src/ui/hud/README.md` for the four lines.

**The minute line** gains the pictures a second, the 99th percentile of the time between them (to the whole ms), and the late ones:

    watch: ticks 1 to 1200: a tick took 1.20 ms on average and 9.04 ms at most (mobs); 1180 frames took 8.10 ms on average and 40.20 ms at most; 59.8 pictures a second, interval p99 26 ms, late 412; 70 mobs, ...

### `--frame-log <path>`

`minecraft_c --frame-log ~/shake.csv` (any build, release included: off, it costs nothing past a null pointer as each world opens) adds a line to `<path>` for every picture main takes. A new file gets the header first. The lines wait in a 1 MiB buffer in the World arena and are written by the sim at least every 10 s or when it is half full, and the rest as the world closes, so play never waits on the disk. The columns, all times in ms but `taken_s`:

| Column | What |
|---|---|
| `seq` | the picture's number since the world opened (pictures drawn and never taken leave gaps, as with vsync off) |
| `taken_s` | when main took it, seconds since the world opened |
| `interval_ms` | **from the last picture main took to this one: what was on the screen** |
| `ticks_waited`, `late` | main's frames from its start to its take, and 1 if that is more than 1 |
| `idle_ms` | the gpu thread waiting for main to take the last picture before starting this one: the card idles meanwhile |
| `upload_ms`, `lod_plan_ms`, `fill_ms` | the gpu thread's parts before the draw |
| `draw_cpu_ms` | from there to all queued: entities, weather, the kernels' launches |
| `card_wait_ms` | from all queued to ready: the thread waiting for the card, and the copy home |
| `p_ms` | from begun to ready, less the card's time: the gpu thread's own work, P in plan 008 |
| `ready_to_take_ms` | from ready to main's take: the picture waiting for main's slot |
| `card_ms`, `coarse_ms`, `primary_ms`, `lod_ms`, `shade_ms`, `post_ms`, `readback_ms` | the card's clock: the whole, its passes, and the far field's rays (inside primary) |
| `water_ms`, `surfaces_ms`, `sun_ms`, `ao_ms`, `ao_fresh_ms` | shade's own kernels |
| `fresh` | pixels on the fresh AO list: surfaces the last picture did not have, each given 4 AO rays |
| `card_mhz`, `clock_read` | the card's clock speed at its last reading, and 1 on the picture that read it (once a second) |
| `turn_deg_s` | the camera's turn speed |
| `x_put_ms`, `x_wait_ms`, `x_idle_ms` | the X server's last put, send to completion; main's block waiting for it this frame; main from that send to its next wait (its sleep to the slot, mostly). 0 under KMS |

**How to read it.** Sort by `interval_ms`: every line well over 8.33 ms (16.7, 25.0) is a picture the screen held for more than one slot. On that line, `card_ms` against `p_ms` and `idle_ms` says whether the card, the gpu thread or the wait for main took the time; `fresh` and `turn_deg_s` say whether turning brought the work; `card_mhz` says how fast the card was going; `x_put_ms` and `x_wait_ms` say whether the X server held main.
