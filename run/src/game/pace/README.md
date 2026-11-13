# run/src/game/pace

**Purpose:** plan 008 phase 02, when the gpu thread starts each picture; and plan 008 phase 05, the size it draws it at.
When it starts: Before, it started one only once main had taken the last: a 17 ms picture then waited for main's next 8.33 ms tick after missing one, became a 25 ms interval, and the card sat idle in the wait (phase 01 measured 5.7 ms a picture idle, 87 % late, with the card at 9.8 ms). Now a picture that missed its take does not make the next one wait, and a picture that fits starts just in time, so what main shows was read from a fresher camera.
**Tier:** Arena by depth (2). It allocates nothing.
**Owns:** nothing. `game_pace_t` is lines of the game's World arena beside `game_watch_t` (`game_pace_of`, in `game/index.c`), as `game_t` is at its size.
**Lifetime:** World. `game_pace_start` empties it as a world opens.
**Threads:** `game_pace_decide` and `game_pace_take_at` are pure. `game_pace_note` and the first line of `game_pace_t` are the gpu thread's. Main writes `next_take_ns` every frame of play, relaxed; a stale value is an earlier take on the same beat, which `game_pace_take_at` carries forward.
**Public functions** (`run/include/game/pace/index.h`):

- `game_pace_start`: nothing measured yet; Q3 A or B.
- `game_pace_decide`: when to start the next picture, from `game_pace_in_t`: now, main's next take, the tick, T-hat, when the last picture was ready and the take it was aimed at, whether main has taken it, uncapped, and Q3.
- `game_pace_note`: after each picture, its P + G into the ring of 16, T-hat as their 90th percentile, and its ready time and aim for the next ask.
- `game_pace_take_at`: the first of main's takes at or after a time, on its beat.
- `game_pace_ladder`, `game_pace_ladder_set` (phase 05): the rungs a world may draw at, from the render scale down to the floor setting, into the pace with nothing counted.
- `game_pace_note_drawn`: after `game_pace_note`, that picture's card time and the rung it was drawn at.
- `game_pace_predict_ns`: the cost model: what a picture at a rung is predicted to take.
- `game_pace_rung`: the controller, from the pace and a `game_pace_rung_in_t` (the tick, the turn at the centre in pixels and degrees a second, the late flag and the late picture's rung). No clock inside.
- `game_pace_rung_next`: the gpu thread's, before each picture: `game_pace_rung` with the turn worked out from the frame's last camera to this one, and the late flag from the pace. The caller hands the result to `render_frame_scale_set`.

**May be called by:** `run/src/game/` and tests.
**Depends on:** `types`, `machine`, `time`, the generated `tuning/render/resolution` header, and `render/frame` for the camera, the frame's last camera and `post_turn_pixels` (phase 05's `game_pace_rung_next` alone; the rest is plain arithmetic).

## The rule

T-hat is the 90th percentile of the last 16 pictures' P + G, from the gpu thread's start to the picture being ready: one picture in ten may then come in over it and be late, and the next starts at once. The lead is T-hat plus a 1 ms margin (Q3), and a picture's aim is the earliest take it can make started now: the first take at least the lead away.

1. **Uncapped, or no take known** (main published none, or the game is paused): today's rule. Start now; capped, hold while a picture waits.
2. **The last picture was late** (ready after the take it was aimed at): start now.
3. **The lead is over two ticks** (16.67 ms at 120 Hz): start now, back to back. Paced, such a picture would get three ticks, 40 a second; back to back a 17 ms picture gives 58.8, shown 16.67 or 25 ms apart. Over two ticks, more pictures are worth more than an even beat.
4. **A picture waits for main and is on time:** hold until main takes it. Starting now would show the next no sooner than starting just in time after the take, and only heat the card.
5. **Nothing waits:** under Q3 A, start just in time: at the aim less the lead, or now if that is past. Under B, now.

At 15 ms that is a picture each two ticks, ready 1 ms before its take, 0.67 ms fresher than before; at 14 ms the same; at 7 ms one each tick. Nothing here ever starts before now, so never before the last picture was ready: never two in flight.

The gpu thread asks, sleeps toward `start_at_ns` in slices of at most `GAME_PACE_SLICE_NS` (0.5 ms), and asks again each time; with `hold` it only sleeps a slice, or 100 us under today's rule. Main's next take comes from whoever runs its frames: the window's clock (`display_window_run`'s next tick), the next flip under KMS (`display_flip_next_ns`), or a bench's own clock, through `game_loop_next_take`.

The rule takes main's beat on trust: takes at `next_take_ns + k tick`. A main whose takes come faster than the tick it says (a test that sleeps 1 ms a frame and says "next in 1 ms") would move the beat ahead with every frame, and a just-in-time start with it, for ever. So the gpu thread never lets a start the rule has said move later until that picture is drawn; with a true beat it never needs to.

## The resolution ladder (plan 008 phase 05)

Some directions cost more than the slot on a capped card (phase 01's cost rose), and turning sweeps through them. So while turning, or after a late picture, a picture is drawn smaller for a moment and brought up on the card; a still camera is always drawn whole. The tuning is `compile/config/render/resolution/index.yaml`.

**The ladder** (`game_pace_ladder_t`): the render scale at the top (`percent[0]`), then each of the fixed rungs 85, 70 and 50 % of each side (72, 49 and 25 % of the pixels) that is under the top and not under the floor setting (`dynamic_resolution_floor_percent`, 50 by default). With `dynamic_resolution` off, the top alone. The world's ladder is set as it opens and whenever its view changes (`game_scale_apply` in `game/index.c`).

**The cost model.** Each picture's card time G is scaled to the whole picture by the share of pixels it was drawn with: G-hat at 100 % is the 90th percentile of G / share over the last 16 pictures, and P-hat the 90th percentile of the rest, (P + G) - G. A rung is predicted at P-hat + G-hat x share. The card's time is mostly per pixel (the coarse pass is per tile, and so per pixel too); P does not scale.

**The rules,** for each picture, in order (`game_pace_rung`):

1. **Still** (under 2 pixels of turn at the centre since the last picture, phase 04's line) **for 15 pictures in a row:** straight to the top, whatever it costs (250 ms at 60). Phase 02's rule then paces the whole picture.
2. **Turning, and the last picture was late** (drawn at this rung: a picture on its way when the rung stepped down was drawn above it, and does not count twice), **or turning faster than 180 degrees a second with this rung predicted over 80 % of the slot** (two ticks; 13.3 ms at 120 Hz): one rung down. A still camera never steps down.
3. **Otherwise, the rung above predicted under 80 % of the slot for 8 pictures in a row:** one rung up. The hysteresis keeps it from hunting.
4. Never past the ladder's ends.

80 % leaves room for the 90th percentile's misses and phase 02's 1 ms margin (the plan's 85 % gave a p99 of 17.4 ms on phase 01's shake replayed: the cost changes with direction faster than the percentile follows; 80 % gives 16.0); 180 degrees a second is slow enough to see detail on, where a late picture alone decides. The worked example: P = 1 ms and G-hat = 16 ms at 100 % predict 17 ms, over the budget; at 85 % 12.6, under 13.3, so a 720 degree a second turn steps there.

The turn is `post_turn_pixels` between the frame's last queued camera (without jitter) and this one at the output's size, so it is in the pixels a person sees; degrees a second divide its angle by the time since the last ask. Each picture's rung is in the watch's picture ring (`percent`), the frame log's `rung` column and F3's "res 70% dyn".
