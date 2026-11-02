# run/src/player

**Purpose:** the player: its body, getting what it wants to the sim thread, and where it is back to main. The Owner of `movement`, `camera` and `interaction`.
**Tier:** Owner.
**Owns:** no region or arena. Its part keeps all it needs in `player_movement_t`, each move uses `physics`'s region, and a tier may allocate less than it is allowed: `plans/CODING_STANDARD.md` section 4.
**Lifetime:** Process for `player_t`. `player_spawn` starts a body, for a world.
**Threads:** See below.
**Public functions** (`run/include/player/index.h`):

- `player_init`, `player_deinit`: a pair. Deinit is safe on a zeroed `player_t`.
- `player_spawn`: a body at rest at a place, before the threads start.
- `player_send`: main says what the player wants.
- `player_look_send`: main says where the player looks, for what it aims at.
- `player_hands_send`: main says what the two mouse buttons are doing.
- `player_interact_tick`: one tick of the sim's for the hands, on what the last tick aimed at, before `player_tick`.
- `player_pickup_reach`: the box an item has to overlap to be picked up, with the body where it now is.
- `player_tick`: one tick of the sim's: the body moves, then what it aims at is worked out from where it now stands.
- `player_eye_at`: main asks where to draw the eye right now.
- `player_target_view`: main asks what that tick aimed at, to draw its outline.

**May be called by:** `run/src/main/` for the pair, and `run/src/game/` for the rest.
**Depends on:** `physics`, `world/map`, `render/frame` for the exchange, `machine`, and its part below.

**Parts:** `movement`: walk, sprint, sneak, jump, swim, climb and fall. `camera`: mouse look, what is aimed at, and its outline. `interaction`: mining, drops and placing.

## The fixed tick and the threads

A body moves only in `player_tick`, at `game.tick_hz` from `config/machine.yaml`, on the sim thread, however fast the pictures are. So its path depends on nothing but what it was told at which tick.

| From, to | What | How |
|---|---|---|
| `main` to `sim` | The newest intent, in one word, with when it was sent | Relaxed atomics, since neither points at anything |
| `main` to `sim` | A jump pressed since the last tick, so that a quick tap is not lost between two | The same |
| `main` to `sim` | The newest yaw and pitch, in one word, so that they stay a pair | The same |
| `main` to `sim` | The two buttons held now, and those pressed since the last tick, so that a quick click is not lost between two | The same |
| `sim` to `main` | Each tick's eye, then and now, when it was due, and what it aims at | A triple buffer: `render_exchange_t` |

Neither thread ever waits for the other.

**Render interpolation.** `player_eye_at` goes from a tick's start to its end across the tick after that one was due, so the picture is always one tick behind and moves evenly. Past its end it holds there until the next tick comes, so a late tick makes it wait and never go back. The camera's look is main's, and follows the mouse every frame. Only where the eye is waits for a tick.

**Nobody at the controls.** An intent over 250 ms old counts as all keys and buttons up. Main stops sending when the console goes to another VT and the sim goes on: without this, a held W would walk the player away while nobody is looking.
