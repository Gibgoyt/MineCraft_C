# run/src/game/pace

**Purpose:** plan 008 phase 02. When the gpu thread starts each picture. Before, it started one only once main had taken the last: a 17 ms picture then waited for main's next 8.33 ms tick after missing one, became a 25 ms interval, and the card sat idle in the wait (phase 01 measured 5.7 ms a picture idle, 87 % late, with the card at 9.8 ms). Now a picture that missed its take does not make the next one wait, and a picture that fits starts just in time, so what main shows was read from a fresher camera.
**Tier:** Arena by depth (2). It allocates nothing.
**Owns:** nothing. `game_pace_t` is lines of the game's World arena beside `game_watch_t` (`game_pace_of`, in `game/index.c`), as `game_t` is at its size.
**Lifetime:** World. `game_pace_start` empties it as a world opens.
**Threads:** `game_pace_decide` and `game_pace_take_at` are pure. `game_pace_note` and the first line of `game_pace_t` are the gpu thread's. Main writes `next_take_ns` every frame of play, relaxed; a stale value is an earlier take on the same beat, which `game_pace_take_at` carries forward.
**Public functions** (`run/include/game/pace/index.h`):

- `game_pace_start`: nothing measured yet; Q3 A or B.
- `game_pace_decide`: when to start the next picture, from `game_pace_in_t`: now, main's next take, the tick, T-hat, when the last picture was ready and the take it was aimed at, whether main has taken it, uncapped, and Q3.
- `game_pace_note`: after each picture, its P + G into the ring of 16, T-hat as their 90th percentile, and its ready time and aim for the next ask.
- `game_pace_take_at`: the first of main's takes at or after a time, on its beat.

**May be called by:** `run/src/game/` and tests.
**Depends on:** nothing but `types` and `machine`.

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
