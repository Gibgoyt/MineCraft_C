# cuda/src/frame

**Purpose:** one frame's kernels in order on one stream, with the card's clock around each, and the copy of the picture home on a second stream; up to two frames on their way at once (plan 008 phase 03).
**Tier:** a CUDA area. It holds no memory but its events and the copy stream: every block comes from its caller, and so does the host memory for the frames on their way (`frame_flight_t`) and the staging, which it pins.
**Owns:** the CUDA events in each slot of the `frame_flight_t` it is given, the copy stream, and the pins on the flight and the staging (`device_staging_t`, whose marks it owns through `device_staging_init`).
**Lifetime:** `frame_gpu_init` to `frame_gpu_deinit`; pins last from `frame_host_pin` to `frame_host_unpin`.
**Threads:** one at a time: the `gpu` thread, or the one thread of a run with no threads.
**Public functions** (`run/include/gpu_bridge/frame/index.h`, for C and CUDA):

- `frame_gpu_init`, `frame_gpu_deinit`: make and destroy the events and the copy stream, and pin and unpin the flight (`frame_flight_bytes`, page-aligned) and the staging's two areas. Deinit waits for a frame still on its way first, and is safe on a zeroed `frame_gpu_t`.
- `frame_host_pin`, `frame_host_unpin`: pin host memory for the copy home. It stays whoever's it was: `cudaHostRegister` allocates nothing, so `run/src/heap/` stays the only allocator.
- `frame_gpu_queue`: coarse pass, primary rays, shading, post and the outline, queued in the next free slot, behind every copy queued for that frame; returns at once with the slot. It then turns the staging, so what is copied from there on is the next frame's.
- `frame_gpu_home`: the copy of a queued frame's picture home, on the copy stream, once that frame's post is done.
- `frame_gpu_finish`: waits for a frame sent home, and reports each kernel's time and each pass's on the card's clock, and the marks. God rays and the temporal pass count as post, AO as shading, and the far field's rays as primary, as the rest of those same rays. A fault in this frame or the one before comes out here.
- `frame_gpu_run`: queue, home and finish of one frame, which is everything before plan 008 phase 03: `--serial-frames`, and every caller that wants its picture now.
- `frame_kernel_name`: what a kernel is called in a profile.
- `frame_marks_init`, `frame_marks_deinit`, `frame_gpu_clock_mhz`, `frame_gpu_clock_rated` (plan 008 phase 01): a `frame_marks_t` set in `frame_gpu_t.borrowed_marks` makes each frame say more of itself. Shade's five kernels each on the card's clock (`shade_ms`, which add up to the shade kernel's time: the test holds it to 0.01 ms), the fresh AO list's length, when the frame was all queued on the host's `CLOCK_MONOTONIC` (`queued_ns`), and, when `clock_due` is set, the card's clock speed. That is a one-warp kernel that spins `FRAME_CLOCK_CYCLES` of `clock64()`, timed by two events, before the frame's first mark so the passes are timed as without it: MHz = cycles / (ms x 1000). The events also hold the kernel's start, a few microseconds, so it reads about 1 % low. No NVML: the game links only the CUDA runtime. `frame_gpu_clock_rated` is CUDA's stated peak (`cudaDevAttrClockRate`). The events a frame's marks are read from are its slot's, as two frames may be on their way; the marks keep two of their own only for `frame_gpu_clock_mhz`. The fresh list's length and the clock's cycles come home as a few bytes copied into the pinned slot behind the frame's post, so reading them costs the thread no wait. Plan 009 phase 01: a bench may set `frame_marks_t.borrowed_counts` to a counts block (`traverse_counts_t` and its planes, at least `traverse_counts_bytes` of the picture, or the frame is refused); a frame queued with the marks then runs `traverse_primary_counted`, `lod_frame_counted` and shade's counted kernels, which add every ray's steps into it. Play never sets it.

**May be called by:** `render`.
**Depends on:** `gpu_bridge/*`, `cuda/src/traverse/`, `cuda/src/shade/`, `cuda/src/post/`, `cuda/src/device_memory/` (the staging), the CUDA runtime.

## A frame on its way

Each slot of `frame_flight_t` is one frame: its kernel events, shade's and the clock's, an end event, and the few words its marks copy home. In each slot:

1. `owned_events[0 .. FRAME_KERNEL_READBACK]` are recorded on the frame's stream, the legacy one, between the kernels. The one at `FRAME_KERNEL_READBACK` comes after post and the outline: the picture is whole.
2. The copy stream (`cudaStreamNonBlocking`, so the legacy stream does not wait for it, nor it for the legacy stream) waits for that event, copies the picture into pinned host memory, and records `owned_events[FRAME_KERNEL_COUNT]`. The readback's time is still from the one to the other.
3. `owned_end` is the last thing on the frame's stream: after the few words copied home.
4. `frame_gpu_finish` waits for the copy's event and for the end.

`render/frame` writes the two device pictures in turn, by slot, so frame N's copy home and frame N+1's post never touch the same picture. The copy home of N then runs while N+1's coarse and primary passes do: the 1.3 ms readback of a 1080p picture leaves the card's critical path. `frame_gpu_whole` and `frame_gpu_done` ask, without waiting, whether a frame's picture is whole and whether it is home.

**Measured: a copy to the card queued after a copy home still waiting for its picture waited for that copy home.** With N's copy home queued as soon as N-1 was published, N+1's staging copies (the entity lists, every frame) were queued behind it, and the card started N+1 only once N's whole readback was done: no gain at all. So `render/frame` queues N's copy home only after N+1's copies and kernels, and the gain is the whole readback.

## Why it is the way it is

The kernels run in one stream's order and every copy to the card runs in that same stream, between frames: that order is what lets `world_buffers` reuse a slab at once. The second stream does nothing but read a finished picture, behind an event, so it cannot break that. Uploads never go on it.

A synchronous `cudaMemcpy` from pageable memory waits for every kernel queued before it, so with two frames on their way it would bring back the wait. So the per-frame copies (sections, the column table, entity lists, the rain top, the far field's tables) go through `device_staging_t`: copied into a pinned area and queued with `cudaMemcpyAsync` on the frame's stream. There are two areas, one for each slot, and `frame_gpu_queue` turns them: an area is written again only once the stream is past its mark, which is the frame two before, finished already in the usual run. A copy too large for what is left goes waiting, in the same order.

The clock is `cudaEventRecord` between the kernels and not the host's, because a host clock around each would need a wait around each, and so would change what it measures. An event is a marker in the stream, costing the kernels nothing they would notice, so the timers are always on, and `run/src/profiler/` only reads what they say. A kernel here is one call this file makes: what a call does inside itself, as `shade_frame_entities` does with AO, is one number.

A wait comes once, at the finish. A kernel that faults after it started is first reported there, so that is the check that matters: `frame_gpu_finish` returns what `cudaEventSynchronize` says, as "frame: waiting for the passes". With one frame on its way that is at most one frame later than with `frame_gpu_run`: `render_feature_bridge`'s `a_fault_is_reported_within_two_frames` holds it.

**Plan 008 phase 05: a frame drawn smaller.** With `frame_args_t.borrowed_output_camera` set, the camera is the drawn one, at a rung under the output's size. Post then writes into the hits block (`frame_targets_t.borrowed_hits`, `traverse_hits_bytes` of the output, 16 bytes a pixel, and nothing reads it once shade is done), and after the `FRAME_KERNEL_OVERLAY` mark `post_upscale` brings that up into the frame's device picture at the output camera's size, before the overlays, which are drawn by the output camera reading the drawn depths under each pixel (`entity_rays_overlay_scaled`). So the upscale's time is in the overlay kernel's slot of `frame_timings_t` (and its `ran` bit is set), and post's own time is as at the full size, of fewer pixels. The copy home is of the output's size (`frame_slot_t.width` and `height`). `frame_gpu_queue` refuses a target smaller than the output, and a hits block too small for the drawn picture. With no output camera nothing of this runs.
