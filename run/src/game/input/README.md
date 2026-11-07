# run/src/game/input

**Purpose:** what a person's hands come to for `game`: a poll of real input or a script's tick turned into a step of flying, into what the body wants, and into what the hotbar and the debug overlay's key are doing. Not in the plan's list: `game/index.c` passed the standard's 1500 lines for splitting once the mobs and settings met, and all of this goes through `game`'s public calls.
**Tier:** Arena by depth, and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Nothing is kept here. `game_t.look_rest` and `game_t.hud_debug_key` are `game`'s.
**Threads:** the one that owns input, which is `main`, or the one thread of a headless run.
**Public functions** (declared with the rest of `game`, in `run/include/game/index.h`):

- `game_input_from_script`: a script's tick as a poll of real input.
- `game_fly_from_script`, `game_fly_from_input`: either kind of input to a step of flying.
- `game_fly`, `game_walk_view`: the eye moved by a step of flying, or put where the ticks have walked it, and the world's middle with it. Moved here from `game/index.c` when plan 004 phase 09 took that file past 1500 lines.
- `game_walk_send`, `game_hands_send`, `game_hotbar_send`: what the mouse and keys want of the body, what its two buttons are doing, and the number keys and wheel.
- `game_debug_send`: each press of the overlay's key turns it on or off.
- `game_loop_input_frame`: one poll of input, with its picture, on the real threads. `game_loop_input_clear`: nothing held, as far as the sim's next tick knows.
- `game_screen_sim`, `game_has_screens`, `game_screen_input`, `game_screen_up`: what the game screens are told of the sim, whether a game has them, and a poll's input for them. Moved here from `game/index.c` when plan 004 phase 08 took that file past 1500 lines again.

**May be called by:** `run/src/game/` and its callers.
**Depends on:** `game`'s header, `game/view`, `game/atlas` (the Atlas has a poll's input before the game screens, and is one of them for Esc), `input`, `display/headless`, `player`, `item`, `ui` (the screens), `time`.
