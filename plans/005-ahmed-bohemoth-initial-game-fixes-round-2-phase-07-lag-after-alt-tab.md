# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 7: Lag after being away

**Status:** in progress: pause on focus loss built and tested   (updated 2026-11-10)

## Goal

Coming back to the game after a long time in another window, it plays as smoothly as before. If it ever slows down, the game says why.

**Ahmed's words:** "When switching tabs in xfce for a long period of time and then switching back to the game it becomes very very laggy. But there is nothing that even logs that", and "going back to title screen and returning it is no longer as laggy".

## What his log shows

From one session of 2115 s, in `../prompt.md`:

| Number | Value | What it suggests |
|---|---|---|
| Mobs | "2309 mobs came, 2239 went, and 70 are in the world" | **70 is exactly `MOB_SPAWN_CAP`** (`run/include/mob/spawn/index.h:24`). The world was full of mobs |
| Frames | mean 8.06 ms, **p50 5.3 ms, p99 49.9 ms**, max 144 ms | Mostly smooth, with heavy spikes |
| `shade` | mean 4.25 ms, **max 126 ms** | The GPU had some very slow frames |
| `readback` | mean 1.92 ms, **max 79 ms** | Copying frames back sometimes waited a long time |
| FPS | 69.6 over the session | Below what the frame times allow (plan 003, row B) |

**What's known from the code:**

- **The game doesn't pause when you leave the window.** `run/src/display/x11/index.c` only lets go of the mouse and keys on focus loss. Time, spawning and everything else carry on.
- **Leaving to the title screen and reopening resets the world's live state:** mobs, drops and the upload queue. That fits a pile-up of something.

**The likely cause, not proven:** while Ahmed was away, night came and hostile mobs filled the world to its cap of 70. Each runs AI and pathfinding every tick, and the renderer draws them. Phase 3 turns those mobs off, which would hide this without proving it. Other possibilities still open: something else piling up, such as the upload queue, light updates or leaf decay, or other programs using the GPU while he was away (the `readback` spikes).

## Todo

**Stage 1: make it say why.** This comes first, so the cause is measured, not guessed.

- [x] Add to the F3 overlay: mobs in the world, the sim tick in ms, sections waiting to upload, and drops on the ground
- [x] When a frame takes over 33 ms, or a tick over 50 ms, log one line naming the slowest stage. At most once every few seconds, so the log stays readable
- [ ] Reproduce headless: a world left for a full in-game day and night with no input, then measure frames and ticks. Compare with hostile mobs off, and record which change fixes it

**Stage 2: fix what it shows.**

- [x] **Pause when the window loses focus**, as Minecraft does, with a setting to turn it off. The game opens the pause screen, and clicking back in resumes
- [ ] Fix whatever stage 1 found, if pausing alone doesn't explain it
- [x] Add a test that a long time without focus leaves the world as it was, with mobs and time unchanged

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(run_ui_hud|run_game_.*|run_mob_spawn|run_display_x11|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: play, switch to another window for 10 minutes or more, come back, and check it's still smooth. F3 shows the new numbers if not.

## Commits

Appended as they land:

- `96906f1d` Pause the game when its window loses the focus, with pause_on_focus_loss in settings.yaml, resume when the focus comes back, read the X focus mode from byte 8, and test that a long time away leaves mobs and time unchanged.

## Issues found

**Pause on focus loss, as built (2026-11-10).** `display_x11_t.focus_losses` counts each time the window manager takes the focus; the session reads it every frame (`game_session_window_look`) and pauses a world being played, with the pause screen up, unless `pause_on_focus_loss` (settings.yaml, default `true`; Settings, HUD page, "Pause when away") is off. When the focus comes back (a click on the window, or Alt+Tab to it) that pause ends by itself; the click never reaches the game. A pause made with Esc, or one with the Settings screen up over it, stays. The setting's box went on the HUD page because it was the page with room. Test: ctest `run_game_focus`, real threads, a world at midnight with a mob: 5 s away (3581 frames, 100 ticks' worth) left the tick count, clock, mob count and positions and the body exactly as they were.

**The overlay and the slow lines, as built (2026-11-10).** New `run/src/game/watch/`. F3 has two more lines: `mobs N  drops N` (entity store and drop stacks, from the sim after each tick) and `uploads N  far jobs N` (sections of the newest snapshot not yet on the card, and far-field columns on workers, from the gpu thread). The sim tick in ms was already a line (`sim tick`). Each part of a sim tick is now timed (weather, hands, mobs, player, world, publish). A tick over 50 ms or a frame over 33 ms logs one `watch:` line naming its slowest part, at most once in 5 s, with how many more were not said. Every 1200 ticks the sim logs a `watch: ticks A to B:` line with the mean and slowest tick (and its part), the frames' mean and slowest, and mobs, drops, uploads and far jobs, so a log from a long session shows when it went slow. The watch lives in the World arena beside the column jobs, as `game_t` and `app_t` have no room. `game_kernels_note` moved into `game/watch` to keep `game/index.c` under 1500 lines. Goldens regenerated and looked at: `debug_640x480.png` and `corner_debug_640x480.png` (two more overlay lines), `settings_hud.png` (the new box).

**A bug found on the way:** `x11_take_focus` read the focus event's mode from byte 1, which is the detail; the mode is byte 8. So a grab's focus event (XFCE's keyboard grab while Alt+Tab is held) could count as leaving, and a real focus change with detail Virtual or Inferior would not. Now `wire_focus_is_real` reads byte 8, tested in `run_display_x11`. Not checked against a real xfwm4: Ahmed's check below covers it.


**Open for Ahmed: pause when the window loses focus?** Default yes, as in Minecraft, with a setting to turn it off. It also means mobs and night don't creep up on you while you're in another window.

**A second suspect, from phase 14 (2026-11-09).** Far-field builds can take every worker for minutes at render distance 64, and they queue up again as the player moves. That backlog could also explain "laggy" and "going back to the title screen and returning is no longer as laggy", since reopening a world starts the far field over. Stage 1 here should record the far-field line from the end-of-session report alongside the mob count, and compare render distance 32 with 64.

