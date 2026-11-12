# 008-ahmed-bohemoth-smooth-frames-when-turning — master plan

**Status:** draft for approval (2026-11-12). Plans only: on approval, `plans/README.md` applies. Phase files 00 to 07 exist beside this file (section 10), phase 00 done and the rest `not started`, and **no code is written until Ahmed says go.** Nothing is pushed.

## 0. Context

### Ahmed's words, 2026-11-12

- "To add to ./plans/008* please look at ~/Pictures/4.png. when shaking my head vigorously frame rate drops drastically we have to be smarter about what we are doing to fix this please!"

### What the picture shows

`~/Pictures/4.png` is the game at 1920 by 1080 on a snowy mountain, with F3 up. In this game z is height, so `x -80 y 134 z 418` is 418 blocks up, 0.32° N. The sun is 52.8° up. He is looking half at a near snow slope and half out over a valley with trees and far hills.

| F3 line | Value | What it is (read from the code) |
|---|---|---|
| FPS | **45.1** | New pictures main took a second, averaged over a window (`game_fps_note`, `run/src/game/index.c:1169-1177`). It counts pictures, not main's loops |
| coarse | 0.81 ms | The card's clock, last picture only (`game_frame_draw`, `run/src/game/index.c:1180-1206`) |
| primary | 3.65 ms | Primary rays, entity rays **and the far field's rays** (`frame_pass_first_kernel`, `cuda/src/frame/index.cu:5-8`) |
| shade | 6.90 ms | Water, surfaces, sun rays, AO and fresh AO, all one number (`shade_frame_entities`; `cuda/src/frame/README.md`: "A kernel here is one call") |
| post | 1.27 ms | God rays (off), post, TAA, outline |
| readback | 1.38 ms | One synchronous `cudaMemcpy` of the 8.3 MB picture (`cuda/src/frame/index.cu:153-156`): 6.0 GB/s |
| gpu total | **14.01 ms** | The sum of the above, on the card's clock, last picture only |
| sim tick | 1.01 ms | The sim thread's last tick |
| hud | 0.87 ms | Main: the HUD drawn over a copy of the picture |
| present | 0.34 ms | Main: the copy into the display's buffer |
| uploads 0, far jobs 0 | | Nothing is streaming or building: the world is settled |

His settings (`settings.yaml`): render distance 64, full detail 32, 16 workers, vsync on, render scale 100%, every effect on but god rays, far field on. His card is an RTX 2080 Super, in a laptop (below).

**F3 shows one picture's numbers, and no maximum.** There is no p99, no count of late pictures, no clock speed and no turning speed. So 14.01 ms is one picture taken while he was turning. Phase 01 adds the missing numbers.

### What the card was doing at 14:41 on 2026-11-12

These were measured with `nvidia-smi -q` and `sensors`, read-only, while Ahmed's game was running (`pgrep` showed `./build/release/bin/minecraft_c`, pid 463671). Because the game was open, **no GPU measurement was run for this plan.**

| What | Value |
|---|---|
| Graphics clock | **570 to 675 MHz**, of 2100 max |
| Memory clock | 5000 MHz, of 7001 max; performance state P3 |
| Power draw | 42 to 43 W |
| **Enforced power limit** | **50 W**, against a default and requested 200 W |
| Clock event reasons | **SW Power Cap: Active. SW Thermal Slowdown: Active.** Each counter: 449,126 s, about 5.2 days of the machine's 40 days up |
| GPU temperature | 80 to 81 °C (slowdown 97, target 87) |
| Utilisation | 90 to 100 % |
| The machine | A laptop: `alienware_wmi`, `dell_smm`, `BAT1`, `ACAD` online, battery full. `platform_profile` is `balanced` (choices: cool, quiet, balanced, balanced-performance, performance, custom) |
| Fans | Processor 2900 RPM, video 2800 RPM, both at their max |
| CPU | Package 91 °C (cores 87 to 90, "high" 82); load average 8 to 12, from agents' builds |
| Other CUDA work on the card at that moment | `run_player_movement` and a debug `minecraft_c` from another agent's worktree, 120 and 394 MiB |

**So the card was running at about 30 % of its clock**, held there by the laptop's power and heat limits. On a cool card, `bench_world`'s 1080p rows at render distance 32 take 5 to 9 ms (`bench/world/README.md`, Stage 17.3). Plan 004 phase 08 and phase 11 already found the card "slows to about a third after some 30 s of steady load", and plan 005 phase 14 left the spikes "needs `nvidia-smi` clocks beside the frame log on his desktop". This is that reading: on his machine the card is held at 50 W all the time.

**This is the largest single factor, and it is outside the code.** But it is the card he plays on. So this plan designs every fix to hold on the capped card, and says what Ahmed can try outside the code (section 2.2).

### What exists today (read from the repo, 2026-11-12)

**The threads.** `main` runs the display loop, takes pictures and draws the HUD. The `gpu` thread uploads, plans the far field and draws (`game_gpu_thread`, `run/src/game/index.c:1456-1506`). The `sim` thread ticks at 20 Hz. Pictures pass from `gpu` to `main` through a three-slot exchange (`render_exchange_*`, `run/src/render/frame/index.c:41-68`, `RENDER_EXCHANGE_SLOTS = 3`). Its publish replaces a picture main has not taken yet, so main always gets the newest.

**The rule that paces the card.** With vsync on, the gpu thread draws a picture only once main has taken the last one:

```c
const bool draw_on = atomic_load_explicit(&game->uncapped, ...) != 0u && ...paused == 0u;   // index.c:1464
if (!draw_on && render_exchange_waiting(&render->frame.pictures)) {
    time_sleep_ns(GAME_GPU_IDLE_NS);   // 100 µs, index.c:62
    continue;
}
```

**How main is paced.** In a window there is no vblank to wait on, so main keeps to `MC_DISPLAY_REFRESH_HZ` by the clock (`run/src/display/index.c:421-427`). That is **120** on this machine (`config/machine.yaml:21`). Each loop:

1. `display_x11_wait_ready` waits until the X server has finished the last `ShmPutImage` (`run/src/display/x11/index.c:459-469`, called at `run/src/display/index.c:412`).
2. Main draws: `game_loop_frame` takes the newest picture, composes the HUD and copies it out (`run/src/game/index.c:1552-1588`).
3. Main puts the frame, then sleeps to the next 8.33 ms tick.

**A picture's path on the gpu thread** (`run/src/game/index.c:1474-1505`):

1. `render_upload_frame` (`run/src/render/upload/index.c:186-197`).
2. `render_lod_frame(lod, camera->position)` (`run/src/render/lod/index.c:795-821`).
3. `game_fill_step`, `game_day_apply`.
4. `game_frame_draw`, then `render_frame_draw_weather` (`run/src/render/frame/index.c:650-764`), then `frame_gpu_run` (`cuda/src/frame/index.cu:165-205`). That queues every kernel on one stream, copies the picture home synchronously, and waits with `cudaDeviceSynchronize` (`:182`).

Every host-to-card copy is a synchronous `cudaMemcpy` (`cuda/src/device_memory/index.cu:289`, `cuda/src/world_buffers/index.cu:103,371,402,413`).

**The kernels of a picture,** in stream order (`cuda/src/frame/index.cu:69-163`):

1. Coarse pass: one safe distance per 8×8 tile, from scratch every frame (`cuda/src/traverse/index.cu:146-216`).
2. Primary rays (`:219-239`).
3. Entity rays.
4. The far field: `lod_past_kernel` and `lod_frame_kernel` over every pixel (`cuda/src/lod/index.cu:509,517`), tracing only where shade will look past full detail.
5. Shade: water, surfaces, the sun, AO's history pass, then fresh AO (`cuda/src/shade/index.cu:1125-1188`).
6. Post and TAA (`cuda/src/post/index.cu`).
7. The outline.
8. The readback.

**Temporal reuse that exists:**

- **TAA** (`cuda/src/post/README.md`, "The temporal pass"). Halton jitter; history reprojected along each pixel's unjittered ray at its `t`; clamped to 1.25 standard deviations of the 3×3 neighbours; 10 % of the new frame mixed in. A still camera reads its own history pixel. A camera that moved at all, rotation included, reads 16 texels by Catmull-Rom (`post_history_at`, `cuda/src/post/index.cu:89-120`, `POST_TAA_STILL 0.002`). History is indexed by this frame's `camera.width` (`:96`, `:113`).
- **AO** (`cuda/src/shade/README.md`, "AO"):
  - Each pixel sends one ray on one frame in four (`SHADE_AO_TURNS = 4`).
  - It keeps a running mean of up to 32 samples, found again in the last frame by `post_reproject` and `shade_ao_same_surface` (`cuda/src/shade/index.cu:202-271`).
  - A pixel whose surface was not on the last picture, and has no neighbour on it either, goes on the fresh list. It gets `SHADE_AO_FRESH_RAYS = 4` rays at once (`run/include/gpu_bridge/shade/index.h:92`; `:250-256`).
- **Nothing else.** The coarse pass, primary rays, the far field, sun rays and shading are traced from scratch every frame. No cache anywhere is keyed by view direction.

**Residency does not follow the view.** Upload follows the world map's view by position. The far field follows the camera's position only (`render_lod_frame(..., camera->position)`). `grep -rni frustum run/src/render cuda/src` finds nothing. Turning never asks for a section or a far column, which agrees with F3's `uploads 0  far jobs 0`.

**Measuring tools that exist:**

- **F3** (`ui_hud_debug_t`, `run/include/ui/hud/index.h:42-51`).
- **The watch** (`run/src/game/watch/`, plan 005 phase 7). It logs a line for a frame over 33 ms, naming its slowest part with "the rest of main's frame" as main's own wait, plus a minute summary.
- **`--profile`** (`run/src/profiler/`). Lanes for `main`, `sim`, `gpu`, and the card's kernels with their own clock. It runs in the profile, debug, sanitize and tsan presets only; the release build refuses it.
- **`bench_world`** (`bench/world/`). Standard worlds and paths:
  - `orbit` turns 360° over 240 settled frames, which is **1.5° a frame**. Live, it turns in 10 s, which is **36° a second**.
  - It has live rows on the game's own threads, uncapped, with main at 120 Hz.
  - It records `worst_gap_ms`, per-pass means and p99, and a picture hash for settled rows.

### Where this plan sits

Plan 008 is renderer and frame-pacing work. It touches no terrain, climate or biome code, so it is independent of plans 006 and 007.

There is one overlap. **Plan 006 phase 01 (being built now in another worktree) edits `run/src/game/index.c` and the F3 lines** (`run/src/game/corner/`). This plan's phase 01 also adds F3 lines and gpu-thread timing in `run/src/game/index.c`. So 008's first code commit waits for 006 phase 01 to land on `main`, or rebases onto it. Plans 006 and 007's files are not edited by this plan.

---

## 1. Why fast turning drops the frame rate

Two different things happen when Ahmed shakes his head:

1. **The card's time for a picture goes up.** Turning brings work: fresh AO for every newly seen pixel, a heavier TAA read, and views of the horizon that cost more than views of a slope.
2. **The pacing rule turns a little extra time into a lot of lost frames.** A picture that misses its slot waits for the next one.

On a card held to 50 W both bite much harder.

Each cause below is ranked by how much of the drop it can explain. Each is marked **proven** (from the code, with arithmetic) or **suspected**, with the experiment that settles it in phase 01.

### C1. The card runs at a third of its clock (measured; not turning-specific)

- **Evidence:** the `nvidia-smi` table above. Enforced power limit 50 W of 200, SW power cap and SW thermal slowdown both active for 5.2 days of uptime, 570 to 675 MHz of 2100.
- **Effect:** every pass costs about three times what the same picture costs on a cool card. A 14 ms picture on this card is a 5 ms picture at full clock. Everything below would leave a lot of room at 5 ms; at 14 ms it leaves almost none.
- **Prove in phase 01:**
  - The shake bench records the card's clock beside every row, from a `clock64` calibration kernel and from an `nvidia-smi` sidecar.
  - If Ahmed agrees (Q5), the same row is measured again after he sets `platform_profile` to `performance`. The card ms should scale with the clock ratio.

### C2. The slot rule: the card idles waiting for main, and a late picture waits a whole slot (proven)

The gpu thread starts a picture only after main has taken the last (`index.c:1464-1468`). Main takes at most once per 8.33 ms, paced by the clock at 120 Hz. So a picture that is ready at $P + G$ after the take is shown at the next 8.33 ms tick:

$$\text{interval} = 8.33 \cdot \left\lceil \frac{P + G}{8.33} \right\rceil \text{ ms}$$

where $G$ is the card's time and $P$ is the gpu thread's own work before and after it: upload, far-field planning, fill, entities, weather and the wait's wake-up.

| $P + G$ | Shown every | Pictures a second |
|---|---|---|
| ≤ 8.33 ms | 8.33 ms | 120 |
| 8.34 to 16.67 ms | 16.67 ms | 60 |
| 16.68 to 25.0 ms | 25.0 ms | 40 |
| 25.1 to 33.3 ms | 33.3 ms | 30 |

**The screenshot fits this exactly.** $G = 14.01$, so $P + G$ sits just under the 16.67 line. A mean interval of $1000 / 45.1 = 22.2$ ms is one picture in three at 16.67 and two in three at 25.0: $0.34 \cdot 16.67 + 0.66 \cdot 25.0 = 22.2$. So while turning, two pictures in three cost more than about 16 ms of $P + G$, and each lost 8.33 ms.

**And the card idles in the gap.** From finishing a picture to main's next take, it does nothing, up to 8.33 ms a picture, about 4 ms on average. With vsync off (`uncapped`), `draw_on` is true and the gpu thread draws back to back. That is the proof that this rule, and not the card, sets the pace with vsync on.

- **Prove in phase 01:** the same row live-capped and live-uncapped. Uncapped intervals follow $P + G$; capped ones fall in 8.33 ms steps. The gpu thread's time "waiting for main to take" is logged per picture.

### C3. Fresh AO for every pixel turning brings into view (proven to scale with turn speed; size suspected)

A pixel whose surface was not on the last picture gets 4 AO rays at once. Steady state is 1 ray on a quarter of the pixels (`SHADE_AO_TURNS = 4`). Turning brings a strip into view at the edge of every picture.

At FOV 70° vertical and 16:9, half the width is $\tan^{-1}(0.7002 \cdot 16/9) = 51.2°$, so the picture is 102.5° wide. Turning by $\Delta$, the new strip is the share of width between $\tan(51.2° - \Delta)$ and the edge, over the whole width of 2.49 in tangent units:

| Turn speed | Picture every | $\Delta$ | Strip | Fresh rays | All AO rays against steady |
|---|---|---|---|---|---|
| 720°/s | 16.7 ms | 12° | 17.2 % (357 k pixels) | 1.43 M | **3.8 ×** |
| 720°/s | 25.0 ms | 18° | 23.7 % (491 k) | 1.96 M | **4.8 ×** |
| 720°/s | 33.3 ms | 24° | 29.3 % (607 k) | 2.43 M | **5.7 ×** |
| `orbit` row | (settled) | 1.5° | about 2 % | 0.17 M | 1.3 × |

Steady AO is 2.07 M pixels over 4, which is 0.52 M rays. "All AO rays against steady" counts the steady rays plus the fresh ones.

**It feeds itself:** a slower picture turns further, so the next strip is wider. Plan 001 stage 17.3 measured fresh rays only on the slow `orbit` (1.5° a frame), where they cost about 0.1 ms. At 720°/s there are 8 to 14 times as many.

**How much card time that is, is not known.** Shade's kernels are one number today.

- **Prove in phase 01:**
  - Clock events around each of shade's kernels.
  - The fresh list's length read back with each picture.
  - The settled shake bench at 0, 90, 360, 720 and 1440°/s, with AO on and off.

### C4. TAA reads 16 history texels instead of one whenever the camera moved (proven; small)

A still camera reads its own history pixel. Any turn, however small, makes every pixel read 16 by Catmull-Rom (`cuda/src/post/index.cu:89-120`). Post is 1.27 ms in all, so this is likely 0.2 to 0.5 ms on the capped card.

- **Prove in phase 01:** the settled shake bench with TAA on and off, by speed.

### C5. What a frame costs depends on where it looks, and turning sweeps through the dear directions (suspected; likely large at render distance 64)

At 418 blocks up, looking out over the valley sends primary rays across up to 512 blocks of full detail before they leave it. Each crossing is a coarse tile march of 16-block stretches, then `traverse_trace`. The far field then traces those rays on (counted inside "primary"). Looking at the slope 3 blocks away stops every ray at once. The screenshot is half of each.

So a shake visits pictures over the slot even if turning added no work at all. This is why the drop is "when shaking" and not at one fixed view: the worst direction decides.

- **Prove in phase 01:** the **cost rose**: settled, at his spot, 72 yaws × 3 pitches, card ms per pass and per kernel (far field included), at render distance 32 and 64.

### C6. Main waits for the X server, and the X server waits for the card (suspected)

`display_x11_wait_ready` blocks main until the X server says the last `ShmPutImage` is done. The X server's copy of an 8.3 MB picture into the window is its own work on the same card. At 90 to 100 % CUDA utilisation it waits for a turn.

While main waits it takes no picture. A picture ready on time can still miss its slot. The watch's "the rest of main's frame" (`run/src/game/watch/index.c:118-122`) is this wait plus input. Earlier logs put it near 33 ms.

- **Prove in phase 01:** every put's time from send to `ShmCompletion`, in the in-game frame log, beside the card's busy share.

### C7. Card time spent not tracing, and CPU work the card waits for (proven; about 1.4 ms plus $P$)

- **The readback** is a synchronous `cudaMemcpy` at the end of the stream: 1.38 ms a picture with no kernel running.
- **`cudaDeviceSynchronize`** then leaves the card idle while the gpu thread uploads, plans the far field, fills, and sends entities and weather for the next picture. All of those use synchronous copies.

Both are serial, every picture, whether turning or not. They count against $P + G$ in C2.

- **Prove in phase 01:** the profiler's `upload`, `lod_frame` and `frame_draw` lanes give $P$ per picture; the card lane gives the readback.

### C8. Other programs' CUDA work on the same card (seen; not turning-specific)

At 14:41 another agent's tests were on the card while Ahmed played. Contexts on one card take turns. The build mutex (`.claude/rules/test-or-build-mutex.md`) keeps agents from running at the same time as each other, not from running at the same time as the game. This gives random spikes while agents work.

- **Prove in phase 01:** one shake row with a debug test started on the card halfway through.

### C9. Heat feeds back (suspected)

The chassis fans are at their maximum and the CPU package is at 91 °C. More card work raises heat and lowers the cap. A hot CPU slows main and the gpu thread's $P$. Plan 001 stage 17.3 measured CPU work at 2 to 3 times its cool cost when hot.

- **Prove in phase 01:** CPU and card temperatures in the sidecar, over a 60 s shake.

### Ruled out, with the evidence

| Idea | Why not |
|---|---|
| Turning makes sections upload or page in | Residency follows position only (above); F3 shows `uploads 0  far jobs 0` while it drops |
| Frustum culling or streaming by view | None exists (`grep` finds no frustum in `run/src/render` or `cuda/src`) |
| A cache keyed by view direction that turning invalidates | None exists; the coarse pass is rebuilt every frame |
| Per-frame allocations | `arena_frame` is reset per frame; `bench_world` counts 0 heap calls while timed |
| Mouse input cost | One `recv` a frame, 32 bytes an event (`x11_read`, `run/src/display/x11/index.c:108-144`), and one warp a poll |
| Lost ray coherence or L2 reuse while sweeping | A frame is traced from scratch in the same tile order whatever the speed. One frame's own traffic is far larger than the card's 4 MiB L2, so nothing useful carries over to lose. **Suspected, not proven:** phase 01's settled sweep at 0 and 1440°/s with AO and TAA off must cost the same within run noise, 3 % |

---

## 2. Where I disagree, or read Ahmed's words a particular way

### 2.1 "Drops drastically" is mostly quantisation

The card's time per picture rises by a few milliseconds while turning (C3, C4, C5). The slot rule (C2) turns that into 60 → 40 → 30 pictures a second.

The cheapest and largest fix is to stop the card from idling and stop a late picture from waiting a whole slot (phase 02). Done alone, a 17 ms picture shows at about 59 a second instead of 40. The other phases then bring the card's time back under the slot.

### 2.2 The biggest factor is the laptop, and code cannot lift a power cap

The plan says plainly what Ahmed can try, outside the code, and asks once (Q5):

- the `performance` platform profile (`powerprofilesctl set performance`, or `/sys/firmware/acpi/platform_profile`);
- the Alienware thermal mode in its own tool;
- clear vents;
- not letting agents build while he plays: their CPU heat shares the chassis with the card.

The fixes are designed and judged on the card **as it is**: capped, at about 600 MHz. Anything that holds there holds better uncapped.

### 2.3 Turning may cost less, but the still picture must not change

Lowering the resolution, skipping work and reusing old results while moving is what every engine does. Motion hides it. But a still camera must give exactly today's picture, byte for byte (golden pictures and settled hashes of still rows).

So every adaptive measure here acts only while the camera turns or a picture is late. It returns to full detail within a quarter of a second of stopping (section 4).

### 2.4 720°/s is a target, not the limit

A vigorous shake is often 1000 to 2000°/s at the peaks. Targets are set at 720°/s, a fast deliberate turn, and measured at 1440°/s too.

### 2.5 Turning vsync off is not the fix

Uncapped, the gpu thread draws back to back. That heats a card already at its cap and draws pictures nobody sees. Phase 02 keeps the cap's spirit, at most about one picture a slot, while ending the idle wait.

---

## 3. Design

### 3.1 Measure first (phase 01)

Nothing is fixed until the causes have numbers.

- **A head-shake path in `bench_world`.** Yaw sweeps back and forth as a triangle wave, ±60° about a heading, at constant speeds of 0, 90, 360, 720 and 1440°/s. A pitch variant sweeps ±20°. It runs at a given place (`--at X Y Z`, `--seed S`), so Ahmed's mountain can be a row, and at render distance 32 and 64. It runs three ways:
  - **Settled:** frame $k$ at time $k / 60$ s. Pictures are reproducible, and per-kernel costs can be compared across speeds over the same directions.
  - **Live-capped:** the game's threads, main at 120 Hz with vsync on. This shows the slot rule.
  - **Live-uncapped:** the same with vsync off.
- **Clock events inside shade:** water, surfaces, sun, AO, fresh AO, as kernels of their own on the card lane. **The fresh list's length** read back with the picture.
- **The gpu thread's loop split per picture:**
  - waiting for main to take;
  - upload;
  - far-field planning;
  - fill;
  - draw's CPU part;
  - waiting for the card.

  Plus each picture's **slot accounting**: when it was ready, when it was shown, how many 8.33 ms ticks it waited, and whether it was late.
- **The card's clock in MHz,** from a one-warp kernel that reads `clock64()` at its start and end, timed by events, once a second. This costs microseconds and needs no NVML. Beside it, an `nvidia-smi` sidecar script in the bench directory logs clocks, power limit, temperatures and throttle reasons once a second.
- **F3 gains three lines** (the HUD's debug block):
  - the card ms of the last second: mean, p99 and max;
  - pictures a second, late pictures, and the card's idle ms waiting for main;
  - turn speed in degrees a second, and the card's MHz.
- **`--frame-log <path>`, in release builds too.** One CSV line per picture: times, ticks waited, card ms per kernel, fresh count, turn speed, X put-to-completion latency and MHz. Ahmed can then shake his head in his real game and hand the file over. The watch's minute line gains the interval p99 and the late count.
- **The cost rose** (C5) at his spot.
- **One experiment per cause** (C1 to C9, and the ruled-out line). The table is in the phase 01 file.

**Running it:** only with Ahmed's game closed. The phase asks him first, and never touches his process.

### 3.2 Pacing: start a picture when it is needed, and never make a late one wait a slot (phase 02)

**Main publishes when it will next take a picture:** an atomic `next_take_ns`, the tick it is sleeping to.

**The gpu thread keeps a prediction of its own picture time** $\hat{T}$: the 90th percentile of $P + G$ over its last 16 pictures. It decides when to start:

1. **If its last picture was late** (it finished after the tick it was aimed at), start the next **at once**. The exchange's publish already replaces an untaken picture, so main shows the newest. A 17 ms picture then gives about 59 new pictures a second instead of 40, and a 20 ms one 50 instead of 40.
2. **Otherwise, start just in time:** at $\max(\text{now}, \text{next\_take} - \hat{T} - \text{margin})$, with a margin of 1 ms (Q3). The card still starts at most one picture a slot. Heat stays as today's, and the camera is read later, so what is shown is fresher.
3. **Never more than one picture in flight**, and never two untaken pictures drawn back to back unless the last was late.

The rule is a pure function of (now, `next_take`, $\hat{T}$, last finish, last aimed tick), tested alone with a fake clock. It lives in a new module, `run/src/game/pace/`: `run/src/game/index.c` is already 1638 lines, past the standard's split at 1500.

**Present without blocking main.**

- **X11:** two MIT-SHM segments, used in turn. `ShmCompletion` carries the segment, so each is tracked. If both are still with the X server, main skips that frame's put and counts it, instead of waiting (C6). A skipped put shows the last frame for one more tick, which is what would have happened anyway, and main keeps taking pictures.
- **KMS** already flips between buffers and is unchanged.

**What this does not do:** make any picture cheaper. Phase 02 alone should take Ahmed's case from 45 to about 55 to 60 pictures a second, with no change to any picture.

### 3.3 Overlap the card's work (phase 03)

- **Readback on a second stream.**
  - The post kernel writes one of two device pictures, in turn. An event at the end of the frame lets a copy stream take that picture home while the next frame's coarse and primary passes run.
  - Uploads stay on the frame's stream, between frames, as `cuda/src/frame/README.md` requires for slab reuse. The copy stream only reads a finished picture.
  - This takes the readback, 1.38 ms a picture, off the card's critical path.
- **One frame in flight, not a full wait.**
  - The gpu thread queues frame N+1's kernels, then waits on frame N's end event, not `cudaDeviceSynchronize`.
  - The CPU part of $P$ for N+1 (planning, filling, the camera) overlaps N's kernels.
  - Synchronous copies on the legacy stream would still wait for N's kernels. So the per-frame copies (upload sections, the column table, entities, the weather map) move to `cudaMemcpyAsync` from pinned staging. Staging is not reused until its frame's event has passed.
- **A fault is still reported.** The end event's status is checked every frame, so a fault surfaces at most one frame later, with the same words. A `--serial-frames` debug switch keeps today's path for a test to compare against.

### 3.4 Turning without fresh work (phase 04)

**AO's first guess, with no rays.** A fresh pixel gets a corner estimate from the voxels around its face (the ambient-occlusion trick Minecraft uses for its blocks):

- for each of the face's four corners, the three cells beside it in the face's plane, one step out, read from the tree;
- the four corner values blended by where on the face the pixel is;
- weighted as one sample.

That is about 12 cell reads, against 4 traced rays of up to 12 steps each. The normal one-in-four rays then refine it over the next frames, as today.

**A world-space AO cache, which head-shaking is made for.** A shake goes back and forth, so the strip that enters was usually on screen 100 to 300 ms earlier. Screen-space history has forgotten it, because it went off the edge. World space has not.

- **The table:** a fixed hash table on the card, keyed by (voxel cell, face, 4×4 place on the face), holding the running mean, the sample count and the frame it was written. Every AO ray writes its result there. A fresh pixel looks there before anything else.
- **Size:** 2 M entries of 8 bytes, 16 MiB (section 6).
- **Collisions:** a 16-bit tag in the entry; a mismatch is a miss.
- **Age:** entries older than 4 s are ignored.
- **Edits:** the key must change when a section is edited, so that its old entries never match again. The column table's `generation` (`run/include/gpu_bridge/world_buffers/index.h:100`) changes per load, not per edit. So phase 04 either:
  - keys by the section's pool offset (`world_buffers_column_t.sections[z]`), if phase 04 confirms a re-sent section always gets a new slab; or
  - adds a small per-section edit counter beside it in the table.

**Defaults** (Q2): the corner guess and the cache together. One fresh ray instead of four is the fallback switch. Plan 001 stage 17.3 found two rays instead of four "moved the picture by up to 137 in 255" on a moving camera, so that path is judged by eye at the gate.

**TAA under fast motion.** When the camera turned more than 2 pixels at the picture's centre since the last frame, the history read becomes the 5-tap bicubic approximation, five bilinear reads in place of 16 point reads (Jimenez, "Filmic SMAA", SIGGRAPH 2016). Under 2 pixels it stays Catmull-Rom 16, and a still camera stays the one exact read. So still pictures do not move.

### 3.5 Adaptive resolution while turning or late (phase 05)

- **A ladder of internal sizes,** per axis: 100, 85, 70, 50 % (pixels 100, 72, 49, 25 %). The kernels already draw `camera.width` × `camera.height` into blocks sized for the full picture, so a smaller camera needs no new blocks.
- **The controller:** a pure function of the last 16 pictures' $P + G$, the turn speed at the picture's centre, and the late flag, with hysteresis.
  - **Step down** one rung when a picture is late, or when $\hat{T}$ is over 85 % of the slot while turning faster than 180°/s.
  - **Step up** one rung when the predicted cost at the next rung up is under 85 % of the slot for 8 pictures.
  - **Go straight to 100 %** after 15 pictures with the camera still (Q1).
  - **Never below the floor**, 50 % by default.
- **History across size changes.** AO's and TAA's history are indexed by the **previous** camera's width (`post_history_at`, `shade_ao_at` and their callers). `post_reproject` already maps through both cameras. A change of size is then a reprojection like any other.
- **Upscaling on the card.** A last kernel takes the tone-mapped picture from the internal size to the output size, bilinear, into the full-size picture that is read back. The CPU's nearest-pixel `game_view_bring_up` (`run/src/game/view/index.c:71-90`) goes. The static render-scale setting (`render_scale_percent`) becomes the ladder's top rung.
- **Temporal upsampling** (TAA accumulating at output size from jittered smaller frames) is better and harder. It is Q9, after this works.
- **Settings:** `dynamic_resolution` (on by default) and `dynamic_resolution_floor_percent` (50), on the Settings screen beside render scale. F3 shows the rung.

### 3.6 Far-field reuse while turning in place (phase 06, only if phase 01 says so)

If the cost rose shows the far field deciding the worst directions at render distance 64, phase 06 adds a **turn cache for the far field**. The bar is the far field costing at least 1.5 ms of the worst direction's picture on the capped card.

- **What it is:** a cube map round the eye, 6 × 1024², holding what each direction's far ray found: `t`, colour and face.
- **Filling:** every far ray traced writes its texel.
- **Reading:** a pixel whose direction's texel is valid reads it instead of tracing.
- **When valid:** filled from within 0.25 blocks of the current eye, and with the same far-field revision. At 512 blocks or more, 0.25 blocks of eye movement is under 0.03°, half a pixel at 1080p.
- **Cost:** 6 × 1024² × 16 bytes is 96 MiB.

Turning back and forth while standing then traces each far direction once. Walking invalidates it, and costs what it costs today.

Phase 06 is **conditional** (Q7). If phase 01 shows the far field is not what makes the worst directions dear, phase 06 records that and closes with no code.

### 3.7 Other CUDA work while Ahmed plays

There are three choices (Q4):

- **A (default):** the build wrapper prints a warning when `build/release/bin/minecraft_c` is running and the command will use the card.
- **B:** the wrapper waits until the game exits.
- **C:** nothing.

The default is a warning and a line in phase 01's numbers, because waiting would stall every agent for as long as he plays.

---

## 4. Targets

"His spot" is `x -80 y 134 z 418` in his world. "Capped" is the card as measured above, about 600 MHz under a 50 W cap, recorded beside every number. Phase 01 fills the "Today" column.

| Measure | How | Today | Target |
|---|---|---|---|
| New pictures a second while shaking at 720°/s, his spot, render distance 64, capped | Live-capped shake row and `--frame-log` in his game | 45.1 (screenshot) | **≥ 59**: picture interval p99 **≤ 16.7 ms** |
| The same at render distance 32 | Same | Phase 01 | **p99 ≤ 16.7 ms** |
| Worst picture interval in a 10 s shake at 720°/s | Same | Phase 01 | **≤ 25 ms** |
| At 1440°/s, render distance 64 | Same | Phase 01 | p99 ≤ 25 ms |
| A still camera's picture | Golden pictures, settled hashes of still rows | Today's | **Byte for byte the same** |
| A still camera's card time | Settled still rows | Today's | Within 2 % |
| Card idle while late, waiting for main | Slot accounting | Up to 8.33 ms a picture | Under 0.5 ms a picture |
| Main blocked on the X server | Frame log | Phase 01 | p99 under 1 ms |
| From stopping a turn to full resolution | Frame log | (no adaptive resolution) | ≤ 250 ms |
| Camera-to-screen latency, p50 | Frame log: camera read to picture shown | Phase 01 | No worse than today |
| Card power while still | Sidecar | Phase 01 | Within 5 % of today: pacing must not draw extra pictures |
| Existing `bench_world` rows | Full run | History | No row slower by more than 3 % |
| Stretch, if the card is ever uncapped: shaking at 720°/s, render distance 32 | Live-capped | Phase 01 | p99 ≤ 8.33 ms (120 a second). Recorded, not required |

These are first targets. Ahmed changes any of them at the phase 01 gate (Q8).

## 5. What does not change

- The world, terrain, residency, upload budget, far-field building and the job system.
- What a still picture looks like, and every effect's meaning and setting.
- Uploads stay on the frame's stream between frames (the slab rule in `cuda/src/frame/README.md`).
- The display protocols: MIT-SHM in a window, KMS flips on a console.
- The settings file: new keys get defaults, and old files read as before.
- `bench_world`'s existing rows and its history format. New rows and columns are added at the end.

## 6. Costs and limits

| What | Budget | How it's held |
|---|---|---|
| Frame VRAM | The frames budget is 256 MiB; frames take 204 to 220 MiB today (`bench/world/README.md`) | The second device picture (8.3 MiB) and the AO cache (16 MiB) come to about 245 MiB. The far-field turn cache (96 MiB) does not fit and gets a budget line of its own if phase 06 runs. Measured in each phase |
| New kernels' card time | ≤ 0.2 ms a picture in all (clock kernel, upscale, cache lookups) | Measured on the card lane |
| `app_t` and main's 16384-byte frame limit | No growth | New state goes in the World or Frame arenas, as `game_watch_t` does |
| `run/src/game/index.c` | Already 1638 lines | Pacing goes in `run/src/game/pace/`; the frame log goes in `run/src/game/watch/` |
| Release build | Frame log and F3 lines cost nothing when off | A null check per picture |

## 7. Tests the plan relies on

- **The pacing rule:** a pure function with a fake clock.
  - At $\hat{T} = 14$ ms it starts one picture a slot, just in time.
  - At 17 ms it starts back to back and gives intervals of 17 ms, not 25.
  - It never has two pictures in flight.
- **Double-buffered SHM:** under `Xvfb`, as `tests/run/display/x11/` runs. Two puts outstanding never block main; a third skips and counts.
- **Overlap:** `run_render_frame` gives the same pictures byte for byte with `--serial-frames` and without. A kernel fault is reported within 2 frames.
- **Corner AO:** worked on paper for a face in an open field (1.0), in a two-wall corner, and in a three-wall corner.
- **The AO cache:** a hit after the strip left and came back; a miss after an edit; a miss after 4 s; a tag collision is a miss.
- **The resolution controller:** pure, fed the frame logs recorded in phase 01. It steps down within 2 pictures of a late one, holds 100 % with a still camera, and is back at 100 % within 15 still pictures.
- **History across sizes:** a still scene drawn at 50 % then 100 % has no ghost: no pixel differs from a fresh 100 % frame by more than TAA's own clamp allows.
- **Still pictures:** `run_render_frame`'s golden pictures, and every settled still row's hash, unchanged in every phase.
- **The shake bench's own check:** at speed 0 its settled pictures hash the same as a still row at the same place.

## 8. Risks

| Risk | What's done about it |
|---|---|
| Phase 01 finds the card's cap explains nearly everything | Then phases 02 and 05 matter most (pacing and resolution follow whatever the card can do), and phases 04 and 06 shrink or close. Recorded at the gate |
| Just-in-time starts mispredict and miss | The 90th percentile with a 1 ms margin; a late picture starts the next at once; the margin is tuned from frame logs |
| Starting at once when late draws more pictures and more heat | At most one picture in flight; when not late, one a slot as today. Card power while still is a target |
| Two streams break the slab rule | Uploads stay on the frame stream; the copy stream only reads finished pictures behind an event; `--serial-frames` compares pictures byte for byte |
| Async copies reuse staging too early | Staging is per frame in flight, released by that frame's event; a test writes a pattern and checks it after 3 frames |
| The corner AO guess looks different from ray AO on the strip | It is weighted as one sample and refined within about 0.5 s; judged by Ahmed at the gate. Fallback: one ray |
| The AO cache's 16 MiB pushes frames over 256 MiB | Measured; fallback 1 M entries (8 MiB) |
| Lower resolution while turning is visible | Floor and on/off are settings (Q1); still pictures are untouched; Ahmed judges at the gate |
| Moving-camera pictures change (orbit rows' hashes move with phases 04 and 05) | Expected. Each new hash is recorded after looking at the pictures; still rows must not move |
| The X server still lags under full card load even with two segments | Main no longer blocks, so pictures keep being taken. Lowering the card's load (phases 04 and 05) is the rest |
| Plan 006 phase 01 edits `run/src/game/index.c` and F3 at the same time | 008's first code waits for it on `main`, or rebases (phase 00) |

## 9. Out of scope

- The laptop's power and thermal settings. They are Ahmed's; section 2.2 lists what to try.
- Real vblank in a window (X's Present extension), Vulkan or GL presentation.
- Hardware ray tracing (the card's RT cores), mesh rasterisation, or replacing the voxel tracer.
- Changing what any effect looks like at rest.
- Streaming or residency by view direction. It is not a cause (section 1).

## 10. Phases

Each phase ends somewhere worth stopping, and has its own file with plan 004's rich structure:

- Goal, read first, where it fits, what it must NOT build, Ahmed's words;
- background explained, design, owned paths, steps with a *Why*;
- todo, tests with numbers, verification, gate, risks;
- Commits, and Issues found.

Every heavy command goes through `tools/test-or-build.sh`, one command per call, with `timeout` inside.

### Phase 00: The vision and the plan files — done

- [x] This master and phase files 01 to 07, every box of 01 to 07 unticked
- [x] Phase 00 says where plan 008 sits, the order with plan 006 phase 01, and the rules every phase follows

### Phase 01: Measure the head-shake cost

- [ ] The `shake` path in `bench_world` (settled, live-capped, live-uncapped; `--at`, `--seed`; speeds 0 to 1440°/s; pitch variant)
- [ ] Clock events inside shade; the fresh AO count; the gpu thread's loop split; slot accounting
- [ ] The `clock64` MHz kernel; the `nvidia-smi` sidecar; F3's three new lines; `--frame-log` in release
- [ ] The cost rose at his spot, render distance 32 and 64
- [ ] One experiment per cause (C1 to C9 and the ruled-out line), each answered in the phase file
- [ ] Ahmed's own shake recorded with `--frame-log` in his game

**Gate:** Ahmed reads the table of causes with numbers, his own frame log, and the cost rose, and answers Q5 and Q8.

### Phase 02: Pacing and presenting without waiting

- [ ] `run/src/game/pace/`: the start rule, `next_take_ns`, $\hat{T}$
- [ ] Two SHM segments in `run/src/display/x11/`, skip instead of block
- [ ] Shake rows live-capped: interval follows $P + G$

**Gate:** Ahmed shakes his head at render distance 64 and reads F3: pictures a second near the card's own rate, not 40 or 45.

### Phase 03: Overlapping the card's work

- [ ] Readback on a copy stream from two device pictures
- [ ] One frame in flight, end events, async copies from pinned staging
- [ ] `--serial-frames`; pictures byte for byte; faults within 2 frames

**Gate:** card time a picture down by the readback, and the same pictures.

### Phase 04: Turning without fresh work

- [ ] Corner AO first guess
- [ ] The world-space AO cache
- [ ] TAA's 5-tap read under fast motion

**Gate:** Ahmed shakes and says whether the edges look right; AO's card time at 720°/s ≤ 1.3 × still.

### Phase 05: Adaptive resolution while turning or late

- [ ] The ladder, the controller, history by the previous width, upscale on the card
- [ ] Settings and F3

**Gate:** Ahmed shakes at render distance 64 and says whether he sees the drop in sharpness; still pictures exact.

### Phase 06: The far field's turn cache (conditional)

- [ ] Only if phase 01's cost rose shows the far field at ≥ 1.5 ms of the worst direction on the capped card; else recorded and closed

**Gate:** Ahmed turns in place at render distance 64 and sees the same far hills.

### Phase 07: The final check

- [ ] All shake rows before and after, at his spot and the standard worlds; `history.tsv`; his frame log again; full suites; release build

**Gate:** Ahmed's shake test in his own world, and his verdict.

## 11. Still open for Ahmed

Each has a default, and none blocks approval.

| # | Question | Default if not answered |
|---|---|---|
| Q1 | Dynamic resolution while turning or late: on by default? Floor? | On, floor 50 % per axis, back to 100 % after 15 still pictures |
| Q2 | AO for newly seen pixels: **A** corner guess plus the world-space cache; **B** one ray instead of four; **C** keep four rays | **A** |
| Q3 | Pacing when on time: **A** start just in time (fresher camera); **B** start as soon as main takes (today's, plus starting at once when late) | **A**, margin 1 ms |
| Q4 | Agents' card work while you play: **A** warn; **B** wait for the game to exit; **C** nothing | **A** |
| Q5 | Will you try the `performance` platform profile (and Alienware's thermal mode) once, so phase 01 can measure the capped and the less capped card? | Measure as it is only, and print the commands |
| Q6 | Your panel's real refresh rate. `config/machine.yaml` says 120; in a window main keeps to it by the clock, and a different panel rate judders | 120 until you say |
| Q7 | Phase 06 only if phase 01 shows the far field decides the worst directions | Yes, conditional |
| Q8 | Targets in section 4 (720°/s, render distance 32 and 64, his spot, the capped card) | As written, until the phase 01 gate |
| Q9 | Temporal upsampling instead of a bilinear upscale in phase 05 | Bilinear first; temporal upsampling as a later plan |

## Verification of this plan

    ls plans/008-*
    grep -L "## Issues found" plans/008-*phase-*.md   # prints nothing
    git status --short                               # nothing uncommitted
