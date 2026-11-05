# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 7: Lag after being away

**Status:** not started   (updated 2026-11-05)

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

- [ ] Add to the F3 overlay: mobs in the world, the sim tick in ms, sections waiting to upload, and drops on the ground
- [ ] When a frame takes over 33 ms, or a tick over 50 ms, log one line naming the slowest stage. At most once every few seconds, so the log stays readable
- [ ] Reproduce headless: a world left for a full in-game day and night with no input, then measure frames and ticks. Compare with hostile mobs off, and record which change fixes it

**Stage 2: fix what it shows.**

- [ ] **Pause when the window loses focus**, as Minecraft does, with a setting to turn it off. The game opens the pause screen, and clicking back in resumes
- [ ] Fix whatever stage 1 found, if pausing alone doesn't explain it
- [ ] Add a test that a long time without focus leaves the world as it was, with mobs and time unchanged

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(run_ui_hud|run_game_.*|run_mob_spawn|run_display_x11|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: play, switch to another window for 10 minutes or more, come back, and check it's still smooth. F3 shows the new numbers if not.

## Commits

Appended as they land:

## Issues found

**Open for Ahmed: pause when the window loses focus?** Default yes, as in Minecraft, with a setting to turn it off. It also means mobs and night don't creep up on you while you're in another window.
