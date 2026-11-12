# 008-ahmed-bohemoth-smooth-frames-when-turning — phase 2: Pacing and presenting without waiting

**Status:** in progress: the rule, main's next take, the gpu thread on the rule and two X segments built and tested; measuring next   (updated 2026-11-12)

Read first: `plans/008-ahmed-bohemoth-smooth-frames-when-turning-phase-00-vision-and-plan-files.md`, then the master `plans/008-ahmed-bohemoth-smooth-frames-when-turning.md`: section 1 (C2 and C6), 3.2 and 4. Then phase 01's results table, which says how large C2 and C6 really are on Ahmed's card.

## Goal

A picture that is late no longer waits a whole slot, and the card no longer idles waiting for main to take the last picture. When this phase is done:

- **The gpu thread decides when to start each picture** by a tested rule in a new module, `run/src/game/pace/`. That rule replaces the "only when main has taken it" test at `run/src/game/index.c:1464-1468`. It starts at once after a late picture. Otherwise it starts just in time for main's next take, from its own predicted picture time $\hat{T}$.
- **Main publishes when it will next take a picture** (`next_take_ns`), every loop.
- **In a window, main never blocks on the X server.** Two MIT-SHM segments are used in turn. If both are still with the server, main skips that put, counts it, and goes on taking pictures.
- **Picture intervals follow the card's own time,** not 8.33 ms steps. On phase 01's `spot/shake-720-live-capped` row, the interval p50 is within 1 ms of $P + G$'s p50, and the card's idle time waiting for main is under 0.5 ms a picture (master section 4).
- **A still camera's pictures, and its card power, are as before.**

## Where this fits

**The vision.** Master 2.1: most of "drops drastically" is quantisation. A 15 to 17 ms picture becomes a 25 ms interval because it waits for main's next 8.33 ms tick after missing one. And the card sits idle in that wait. This phase is the cheapest fix and the largest felt one, and it changes no picture.

**What reads it:**

| Reader | What |
|---|---|
| Phase 03 | One picture in flight: this phase's start rule decides when the next is queued |
| Phase 05 | The controller uses $\hat{T}$ and the late flag kept here |
| Phase 07 | Intervals before and after |
| Ahmed | F3's "pictures/s" and "late" lines while shaking |

**What this phase must NOT build:**

- No change to any kernel, stream or picture. Phase 03 overlaps the card's work; phases 04 and 05 make pictures cheaper.
- No change to the KMS flip path's own buffering. It is already double or triple buffered (`run/src/display/flip/`). Only the gpu thread's start rule applies there too.
- No drawing past one picture in flight. Uncapped (vsync off) keeps today's back-to-back drawing.

## Ahmed's words

- "when shaking my head vigorously frame rate drops drastically we have to be smarter about what we are doing to fix this please!"

---

## Background, explained

### What happens today, worked through

Main ticks at $t = 0, 8.33, 16.67, 25, \ldots$ ms. Say a picture costs $P + G = 15$ ms.

| Time | Main | The gpu thread | The card |
|---|---|---|---|
| 0 | Takes picture A | Wakes within 0.1 ms, starts B | Busy with B from 0.1 |
| 8.33 | Nothing new | Drawing B | Busy |
| 15.1 | | B ready; waits for main | **Idle** |
| 16.67 | Takes B | Starts C | Busy from 16.8 |

Every picture costs 16.67, and the card idles 1.6 ms of each slot. Now say $P + G = 17$ ms:

| Time | Main | The gpu thread | The card |
|---|---|---|---|
| 0 | Takes A | Starts B | Busy |
| 16.67 | Nothing new | Drawing B | Busy |
| 17.1 | | B ready; waits | **Idle** |
| 25.0 | Takes B | Starts C | Busy from 25.1 |

Every picture costs 25, and the card idles 7.9 ms of each. That is 40 a second from a card that could give 58.

### What the new rule does with the same numbers

At 17 ms, B is late: it was aimed at 16.67 and finished at 17.1. So C starts at 17.1, at once, not at 25. C is ready at 34.1, shown at 41.67. D starts at 34.1, ready at 51.1, shown at 58.33. Each picture costs 17 ms of card time. Main shows a new one on most ticks and repeats one about one tick in seven. About 58.8 new pictures a second, against 40.

At 15 ms, nothing is late, so the rule starts just in time. It aims each picture to be ready 1 ms before main's next take. So B starts at $16.67 - 15 - 1 = 0.67$ ms after A's take, and is ready at 15.67. The card still idles about 1.7 ms a slot, as today: drawing a second picture into that slot would only heat the card. But B's camera was read 0.67 ms later than today's, so what main shows is that much fresher.

### $\hat{T}$, the predicted picture time

$\hat{T}$ is the 90th percentile of the last 16 pictures' $P + G$: from the gpu thread's start to the picture being ready, as phase 01's stamps measure it. Why the 90th and not the mean: one picture in ten may then come in over the prediction and be late, and the next starts at once. The mean would make half of them late.

### Two SHM segments

An `ShmPutImage` asks the X server to copy from shared memory into the window. The server answers with an `ShmCompletion` event, which names the segment (`shmseg`). Until then, the client must not write into that memory.

Today there is one segment, so main waits for each completion before drawing into it again (`display_x11_wait_ready`, `run/src/display/x11/index.c:459-469`). With two, main draws into whichever is free:

- if both are with the server, main skips this frame's put and keeps the picture it took for the next;
- the screen shows the last frame one tick longer, which it would have anyway;
- main's loop keeps its rhythm.

---

## Design

### `run/src/game/pace/` (new)

    typedef struct {
        int64_t now_ns;
        int64_t next_take_ns;       /* main's next tick, from game->pace_next_take_ns */
        int64_t tick_ns;            /* 1e9 / MC_DISPLAY_REFRESH_HZ */
        int64_t predicted_ns;       /* T-hat */
        int64_t last_ready_ns;      /* when the last picture was ready */
        int64_t last_aimed_ns;      /* the take it was aimed at */
        uint8_t picture_waiting;    /* render_exchange_waiting(&frame.pictures) */
        uint8_t uncapped;
        uint8_t just_in_time;       /* Q3: 1 for A, 0 for B */
    } game_pace_in_t;

    typedef struct {
        int64_t start_at_ns;        /* sleep until this, then draw */
        int64_t aimed_ns;           /* the take this picture is for */
    } game_pace_out_t;

    void game_pace_decide(const game_pace_in_t *in, game_pace_out_t *out);
    void game_pace_note(game_pace_t *pace, int64_t started_ns, int64_t ready_ns);   /* keeps the 16 samples, T-hat */

**The rule:**

- **Uncapped:** start now, as today.
- **The last picture was late** (`last_ready_ns > last_aimed_ns`): start now, aimed at the first take at least $\hat{T}$ away.
- **A picture is waiting** that main has not taken and it is not late: wait for the take, as today. But **under Q3 A**, the start is not the take itself. It is $\max(\text{take}, \text{next\_take} - \hat{T} - 1\text{ ms})$ for the following take.
- **Nothing waiting** (main took it): start at $\max(\text{now}, \text{next\_take} - \hat{T} - 1\text{ ms})$ under A, or now under B.

Pure, no clock inside, no state but what is passed. `game_pace_t` (16 samples and $\hat{T}$) lives in the World arena beside `game_watch_t`, not in `game_t`.

**Main** stores `next_take_ns` (an atomic `int64_t`, relaxed) before it sleeps:

- in a window, the `next_ns` of `display_window_run` (`run/src/display/index.c:423-426`), through the frame source's context;
- on KMS, the next flip's expected time, from the flip's own interval stats.

**The gpu thread** (`game_gpu_thread`, `run/src/game/index.c:1456`):

1. Ask `game_pace_decide`.
2. Sleep until `start_at_ns`, in slices of at most 0.5 ms, rechecking `stop` and `paused`.
3. Draw.
4. Call `game_pace_note`.

The `GAME_GPU_IDLE_NS` poll stays only for a pause.

### Two segments (`run/src/display/x11/`)

- `display_x11_t` holds `segment[2]`, `owned_shm[2]`, `put_pending[2]` and `put_sent_ns[2]`, and `buffer` points at the free one.
- `x11_packet_take` clears `put_pending[i]` for the `shmseg` the completion names.
- `display_x11_wait_ready` becomes `display_x11_free_buffer`: it returns the free segment, or none if both are pending. It never blocks while one is free, and still fails after 5 s with none, as today.
- `display_window_run` (`run/src/display/index.c:405-428`), when none is free: skips `display_draw_frame`'s put but still calls the frame source, so main takes the picture and composes the HUD into its own buffer. It counts `puts_skipped`.
- The second segment doubles the shared memory to 16.6 MB.

### What F3 and the frame log say

F3's "pictures" line gains `skipped N` (X puts skipped) when not 0. The frame log's record gains `aimed_ns`, `start_at_ns` and `late` from this rule, so phase 07 can see it worked.

---

## Owned paths

This phase may edit these, plus this file:

- `run/src/game/pace/`, `run/include/game/pace/` (new)
- `run/src/game/index.c` (`game_gpu_thread`'s start; `game_loop_frame` storing `next_take_ns`)
- `run/src/display/index.c`, `run/src/display/x11/`, `run/include/display/x11/`
- `run/src/display/flip/` (only to expose the next flip's expected time)
- `run/src/game/watch/` (the record's new fields), the F3 composer (`skipped`)
- `tests/run/game/pace/` (new), `tests/run/display/x11/`, `tests/run/render/frame/`
- `bench/world/` (only to report the new record fields)

## Steps

1. **`game_pace_decide` and `game_pace_note` with their tests first,** on a fake clock. *Why: the rule is the whole phase; it should be right before it runs.*
2. **Main's `next_take_ns`,** window and KMS. *Why: the rule's input.*
3. **The gpu thread on the rule.** *Why: the change itself.*
4. **Two SHM segments; skip instead of block.** *Why: C6.*
5. **Run phase 01's shake rows and Ahmed's frame-log procedure again;** fill in the results below. *Why: the targets.*
6. **README files** (`run/src/game/pace/README.md`, `run/src/display/x11/README.md`, `run/src/game/README.md`'s thread section); tick todos as they land.

## Todo

- [x] `run/src/game/pace/`: `game_pace_decide`, `game_pace_note`, $\hat{T}$ as the 90th percentile of 16, with README
- [x] `run_game_pace` tests (below) pass
- [x] Main stores `next_take_ns` each loop, in a window and on KMS
- [x] `game_gpu_thread` starts pictures by the rule; the 100 µs poll only while paused
- [x] Q3 settled (A by default) and recorded here
- [x] Two MIT-SHM segments; completion by `shmseg`; skip and count when both are pending; still fails after 5 s with none
- [x] F3 and the frame log show `skipped`, `aimed`, `late`
- [ ] Phase 01's shake rows run again (live-capped, live, settled 0 and 720) and the results below filled in
- [ ] Ahmed's frame log again, and its summary below
- [ ] Golden pictures and every settled hash unchanged
- [ ] READMEs updated

## Results (filled in by this phase)

| Row or log | Interval p50 / p99 / worst, before | After | Card idle waiting, before / after | Late share | Card power still, before / after |
|---|---|---|---|---|---|
| `spot/shake-720-live-capped` | | | | | |
| `spot/shake-720-live-capped-r64` | | | | | |
| `jagged_peaks/shake-720-live-capped` | | | | | |
| Ahmed's frame log | | | | | |

## Tests

### `run_game_pace` (new, `tests/run/game/pace/`)

Pure, no card, no threads. Tick 8.33 ms, margin 1 ms.

- **`a_late_picture_starts_the_next_at_once`.** Last ready at 17.1, aimed at 16.67. Start = now.
- **`fifteen_ms_pictures_start_just_in_time`.** $\hat{T} = 15$, next take at 16.67, now 0.1. Start at 0.67 under A; 0.1 under B.
- **`seventeen_ms_pictures_give_seventeen_ms_intervals`.** A simulation of 600 pictures with $P + G = 17$: mean interval between readies 17.0 ± 0.1 ms; shown intervals are 16.67 or 25 with a mean of 17.0 ± 0.3. Today's rule in the same simulation gives 25.0.
- **`fourteen_ms_pictures_are_one_a_slot`.** $P + G = 14$: exactly one picture started per tick over 600 ticks, never two (heat).
- **`never_two_in_flight`.** Over random $P + G$ between 5 and 30 ms, no start comes before the last ready.
- **`the_ninetieth_percentile_of_sixteen`.** `game_pace_note` with known samples gives the known $\hat{T}$.
- **`uncapped_starts_now`.**

### `run_display_x11` (extended, under `Xvfb` as it runs now)

- **`two_puts_outstanding_do_not_block`.** With completions held back by not reading the socket, two puts go, a third call finds no free buffer and returns at once, `puts_skipped` is 1.
- **`a_completion_frees_its_own_segment`.** Completions in the other order free the right one.
- **`no_completion_for_five_seconds_still_fails`.**

### `run_render_frame` (existing)

- **The real-threads test** (`sim`, `gpu` and a `main` loop on a plain buffer) draws the same picture as before, and stops clean.
- **Golden pictures** unchanged.

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                         # must print nothing before the bench runs
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_game_pace$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_display_x11|run_display_headless|run_game)$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^run_render_frame$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh sh -c 'bench/world/gpu_watch.sh /tmp/mc_c_shake/gpu_p02.csv & w=$!; timeout 900 build/profile/bin/bench_world --only shake --record bench/world/history.tsv --label "008 phase 02: pacing"; s=$?; kill $w; wait $w; exit $s'
    tools/test-or-build.sh cmake --build --preset release
    pgrep -af "$PWD/build"; pgrep -af gpu_watch                       # both must print nothing

## Gate

Ahmed plays at render distance 64 as usual and shakes his head hard at his spot with F3 up. He reads the "pictures" and "late" lines. They should sit near the card's own rate: about $1000 / \text{card ms}$, e.g. about 58 at 17 ms, not 40 or 45. He records a frame log (phase 01's procedure).

He says whether it feels smoother, and whether anything stutters that did not before. The start rule's margin and Q3 are his to change here.

## Risks and open questions

| Risk or question | Default |
|---|---|
| $\hat{T}$ mispredicts after a sudden heavy view, so a picture misses its just-in-time aim | The next starts at once (the late branch). The cost is one 8.33 ms repeat, as today's normal case |
| Drawing late pictures back to back raises card heat on a capped card | Only after a late one; never two in flight; card power while still is a target. Phase 01's sidecar measures it |
| Main's clock and the X server's real present drift, so "just in time" is in time for main and not the screen | Main's tick is what takes pictures, so that is what the rule aims at. Real vblank in a window is out of scope (master 9) |
| A sleep overshoots by more than the margin on a hot CPU | Slices of 0.5 ms; the margin is tuned from frame logs; phase 01's C9 numbers say how bad sleeps are |
| Skipping a put hides a stuck X server | Still fails after 5 s with no free segment; `puts_skipped` in F3 and the log |
| KMS's next flip time is less exact than the window's clock | It is read from the flip's measured interval; the late branch covers a miss |

## Commits

Appended as they land:

## Issues found

None.
