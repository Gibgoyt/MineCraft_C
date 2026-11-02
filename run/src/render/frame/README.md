# run/src/render/frame

**Purpose:** the C side of a frame: the tables the kernels read, the free-fly camera, the finished pictures on their way from the gpu thread to the main thread, and putting one on the screen.
**Tier:** Arena: it takes from the arena its parent gives, and its device blocks come from its parent too.
**Owns:** nothing of its own but the clock events inside `frame_gpu_t`, and three pins on memory its parent holds.
**Lifetime:** `render_open_frame` to `render_close_frame`.
**Threads:** `gpu` draws, and `main` takes and presents. With no threads, one thread does both. The camera functions touch only what they are given.
**Public functions** (`run/include/render/frame/index.h`):

- `render_exchange_start`, `render_exchange_publish`, `render_exchange_take`, `render_exchange_waiting`: a triple buffer's numbers, from one thread to another, without either ever waiting.
- `render_camera_start`, `render_camera_fly`, `render_camera_to_traverse`: where the eye is, one step of flying, and what kernels take.
- `render_frame_texture_table_bytes`, `render_frame_face_table_bytes`, `render_frame_shape_table_bytes`, `render_frame_arena_bytes`: sizes for the parent.
- `render_frame_init`, `render_frame_deinit`: the tables and textures sent to the card, and the pictures pinned.
- `render_frame_features_set`: which effects the next frames draw with. All of them, unless told otherwise.
- `render_frame_draw`: a whole frame, from world_buffers as they stand and a camera, on the card and home.
- `render_frame_take`, `render_frame_present`: the newest picture, and copying it to a display buffer.
- `render_frame_report`, `render_frame_percentiles`: what the frames took.

**May be called by:** `render` (the Owner), `game`.
**Depends on:** `arena`, `log`, `assert`, `machine`, `texture`, `display/flip` for `display_buffer_t`, `simd/copy`, the generated block tables, `gpu_bridge/device_memory`, `gpu_bridge/world_buffers`, `gpu_bridge/traverse`, `gpu_bridge/shade`, `gpu_bridge/post`, `gpu_bridge/frame`.

## How a picture reaches the screen

The plan's own words: copy to pinned host memory, then into the dumb buffer, then a page flip. Here that is `frame_gpu_run` into one of three pinned pictures, `render_exchange_publish`, then on the main thread `render_frame_take` and `render_frame_present`, whose copy is `asm/src/copy`'s streaming kernel, with its fence, into whatever `display_buffer_t` it is given. The HUD comes between those two in Stage 14.2.

Three pictures, so that the gpu thread never waits for the display and the display never waits for the card: if a new one is not ready for this flip, the last one shows again.

## The camera

`x` is east, `y` north and `z` up. A yaw of 0 looks east, and a positive yaw turns to the north. W and S go along where it looks, level, A and D across that, Space and Left Shift up and down, Left Ctrl five times as fast, and the wheel sets the speed. `game` turns keys into `render_fly_input_t`.

The position is `float64_t`, and goes to the kernels as a whole cell and a fraction, so the picture is as exact far from the origin as near it.
