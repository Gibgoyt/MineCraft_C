# run/src/player

**Purpose:** the player: its body, getting what it wants to the sim thread, and where it is back to main. The Owner of `movement`, `camera`, `interaction`, `survival`, `combat` and `felling`.
**Tier:** Owner.
**Owns:** no region or arena. Its part keeps all it needs in `player_movement_t`, each move uses `physics`'s region, and a tier may allocate less than it is allowed: `plans/CODING_STANDARD.md` section 4.
**Lifetime:** Process for `player_t`. `player_spawn` starts a body, for a world.
**Threads:** See below.
**Public functions** (`run/include/player/index.h`):

- `player_init`, `player_deinit`: a pair. Deinit is safe on a zeroed `player_t`.
- `player_spawn`: a body at rest at a place, before the threads start.
- `player_send`: main says what the player wants. `player_sneak_sent`: whether the keys it sent last say to sneak, for the sim.
- `player_look_send`: main says where the player looks, for what it aims at.
- `player_hands_send`: main says what the two mouse buttons are doing.
- `player_interact_tick`: one tick of the sim's for the hands, on what the last tick aimed at, before `player_tick`.
- `player_interact_fell_tick`: `player_interact_tick`, with somewhere for a log the hands cut to set the rest of its tree going, and for what can be heard: `run/src/game/` keeps both, runs `player_felling_tick`, and takes the sounds to `run/src/audio/`.
- `player_place_preview`: what the next tick's right button would put down and where, as `player_interaction_place_plan` says with the very input `player_interact_tick` builds, for `run/src/game/` to send to the renderer each tick as the preview of placing (plan 005 phase 10). The input says whether the look is level or below, for placing ahead (plan 005 phase 13). Nothing while a screen holds the buttons.
- `player_body_aimed`: the body the eye looks at, as a swing finds it, for the right button on an animal.
- `player_attack_tick`, `player_attack_landed`: one tick of the sim's for the hand, right before `player_interact_tick`: what a press on a body in reach does, for the caller to apply; and that it landed.
- `player_pickup_reach`: the box an item has to overlap to be picked up, with the body where it now is.
- `player_tick`, `player_tick_wind`: one tick of the sim's: the body moves, in a wind from the climate for the second (plan 004 phase 08; the first is it with none), then what it aims at is worked out from where it now stands.
- `player_survive_tick`: one tick of the sim's, right after `player_tick`, for what keeps the body going.
- `player_vitals_read`, `player_armor_points_read`: main asks how the body is, and how many armour points it wears (0 to 20, whole: `run/src/game/` tells survival each tick from what is worn), for a screen.
- `player_vitals_publish`: puts both where main reads them, from survival as it stands: after each tick, and by `run/src/save/state/` once it has read a body in.
- `player_eyes_under_water_read`: the thread that draws asks whether the eyes were under water as of the last tick.
- `player_eye_at`: main asks where to draw the eye right now.
- `player_target_view`: main asks what that tick aimed at, to draw its outline.
- `player_command_send`, `player_command_take`: plan 006 phase 01's mailbox: main sends one command (a teleport, fly mode on or
  off, a speed factor) and the sim takes it first in its tick. A teleport is each axis a place or a step from the feet, which the
  sim adds to its own position, so `/tp ~ ~ ~300` keeps x and y to the bit; held to `PLAYER_COMMAND_PLACE_MOST` across and the
  gameplay heights; done with `player_movement_place`, so nothing is drawn smeared between; then the body arrives
  (`player_survival_arrive`, from `player_tick`). Refused by a body that is dead or asleep.
- `player_fly_wheel_send`, `player_flight_read`: main's wheel while flying, and flying, what it flies at and the factor, back.

**May be called by:** `run/src/main/` for the pair, `run/src/game/` for the rest, and `run/src/save/state/` to read and write it.
**Depends on:** `entity/store` for what its part aims at, `physics`, `world/map`, `render/frame` for the exchange, `machine`, and its part below.

**Parts:** `movement`: walk, sprint, sneak, jump, swim, climb and fall. `camera`: mouse look, what is aimed at, and its outline. `interaction`: mining, drops and placing. `survival`: health, hunger, food, damage, death, respawn and the bed. `combat`: the attack: what the left button aims at among the world's bodies, and how strong a swing is. `felling`: a log cut bringing down the rest of its tree.

## The fixed tick and the threads

A body moves only in `player_tick`, at `game.tick_hz` from `config/machine.yaml`, on the sim thread, however fast the pictures are. So its path depends on nothing but what it was told at which tick.

| From, to | What | How |
|---|---|---|
| `main` to `sim` | The newest intent, in one word, with when it was sent | Relaxed atomics, since neither points at anything |
| `main` to `sim` | A jump pressed since the last tick, so that a quick tap is not lost between two | The same |
| `main` to `sim` | The newest yaw and pitch, in one word, so that they stay a pair | The same |
| `main` to `sim` | The two buttons held now, and those pressed since the last tick, so that a quick click is not lost between two | The same |
| `sim` to `main` | The body's health, in one word | The same |
| `main` to `sim` | Plan 006 phase 01: one command at a time, a teleport's target and its word | A mailbox of one: main writes the target only while the word is 0 (acquire), then the word (release); the sim reads the target after the word (acquire), then puts 0 back (release). A second command while the first waits is refused, and the box says to try again |
| `main` to `sim` | Wheel steps while flying | A relaxed add; the sim takes them with an exchange |
| `sim` to `main` | Flying, what it flies at and the speed factor, in one word, for the HUD | Relaxed, as the health |
| `sim` to `main` | Each tick's eye, then and now, when it was due, and what it aims at | A triple buffer: `render_exchange_t` |

Neither thread ever waits for the other.

**Render interpolation.** `player_eye_at` goes from a tick's start to its end across the tick after that one was due, so the picture is always one tick behind and moves evenly. Past its end it holds there until the next tick comes, so a late tick makes it wait and never go back. The camera's look is main's, and follows the mouse every frame. Only where the eye is waits for a tick.

**Survival before the rest.** The buttons reach `survival` before the hands, in `player_interact_tick`, and a dead, sleeping or waiting body's keys count for nothing in `player_tick`. A body that nothing has called `player_survive_tick` on never comes to harm, which is what tests of how it moves want.

**Nobody at the controls.** An intent over 250 ms old counts as all keys and buttons up. Main stops sending when the console goes to another VT and the sim goes on: without this, a held W would walk the player away while nobody is looking.
