# tests/run/game/focus

**Purpose:** ctest `run_game_focus`. Holds plan 005 phase 7's pause on losing the window's focus to what Ahmed asked: a long time in another window leaves the world as it was, with its mobs and its time unchanged, and coming back goes on.
**Tier:** test.
**Owns:** the test arena, a directory of its own under `/tmp`, removed at its end, and blocks on the card.
**Lifetime:** One run.
**Threads:** main; the real `sim` and `gpu`, and real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/input/fixtures/`, `run/src/game/session/` and everything under `run/src/game/`, `run/src/settings/`, `run/src/ui/`, `run/src/display/` for the types, and a card.

Compute only. No real display, console or input device is opened: pictures go to plain memory, input comes from the fake evdev, and the window is a zeroed `display_t` with `window` set and a zeroed `display_x11_t`, whose `focused` and `focus_losses` the test moves as `display_x11_poll` would on the window manager's focus events. That the poll counts those events, and not a grab's, is `tests/run/display/x11/`.

What it holds to, on one session through the real menus with the real threads:

- Leaving the window on the menus does nothing.
- In a world at midnight, with four zombies put round the body by hand in a pause (plan 005 phase 3 set hostile mobs' spawn weight to 0, so none come by themselves) and a second of play for them to move, leaving the window brings up the pause screen. For 5 s, which is 100 ticks of play, nothing moves: the tick count, the clock, the mobs' number and every one's place, how many have come, and the body.
- Coming back ends that pause by itself, and the ticks and the clock go on.
- A pause made with Esc stays when the focus comes back, and Esc ends it.
- With `pause_on_focus_loss` off, leaving the window leaves the world going.
- Quit to Title works from a pause that leaving began.
