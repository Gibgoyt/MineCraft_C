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
- `render_frame_features_set`: which effects the next frames draw with. `SHADE_FEATURES_ALL` unless told otherwise, and `game` asks for `SHADE_FEATURES_PLAY`, as the ones with a history do not suit a test that wants one exact picture.
- `render_frame_time_set`, `render_day_fill`: the time of day, and what sky and light it means.
- `render_frame_draw`: a whole frame, from world_buffers as they stand and a camera, on the card and home.
- `render_frame_take`, `render_frame_present`: the newest picture, and copying it to a display buffer.
- `render_frame_report`, `render_frame_percentiles`: what the frames took.

**May be called by:** `render` (the Owner), `game`.
**Depends on:** `arena`, `log`, `assert`, `machine`, `texture`, `display/flip` for `display_buffer_t`, `simd/copy`, the generated block tables, `gpu_bridge/device_memory`, `gpu_bridge/world_buffers`, `gpu_bridge/traverse`, `gpu_bridge/shade`, `gpu_bridge/post`, `gpu_bridge/frame`, `render/entities`, whose scene a frame hands its kernels.

## How a picture reaches the screen

The plan's own words: copy to pinned host memory, then into the dumb buffer, then a page flip. Here that is `frame_gpu_run` into one of three pinned pictures, `render_exchange_publish`, then on the main thread `render_frame_take` and `render_frame_present`, whose copy is `asm/src/copy`'s streaming kernel, with its fence, into whatever `display_buffer_t` it is given. The HUD comes between those two in Stage 14.2.

Three pictures, so that the gpu thread never waits for the display and the display never waits for the card: if a new one is not ready for this flip, the last one shows again.

## The day

`render_day_fill` is arithmetic, once a frame, from a fraction of a day: 0 midnight, 0.25 sunrise in the east, 0.5 noon, 0.75 sunset in the west. The sun's path leans 20 degrees to the south, so that noon has shadows and a side facing south is not the same as one facing north. Colours are mixed from day, twilight and night by how high it is, which come to 1 between them and do not overlap. What lights the world is the sun, or from sunset the moon opposite it, going to nothing at the horizon as one takes over from the other. Colours are this file's and nothing else's, in linear light.

`shade_scene_t.sun` and `post_settings_t` are always what the settings and the time make them, worked out again when either changes: so a kernel has few cases of its own, and with `SHADE_FEATURE_SKY` off it is sent the plain sky and each face's one brightness. This file keeps no clock: `game` does.

## The camera

`x` is east, `y` north and `z` up. A yaw of 0 looks east, and a positive yaw turns to the north. W and S go along where it looks, level, A and D across that, Space and Left Shift up and down, Left Ctrl five times as fast, and the wheel sets the speed. `game` turns keys into `render_fly_input_t`.

The position is `float64_t`, and goes to the kernels as a whole cell and a fraction, so the picture is as exact far from the origin as near it.

## From frame to frame

`post_temporal_t` holds what goes from one frame to the next: the last frame's camera with no jitter, this one's jitter, a count of frames, and which block of each two the last frame wrote. `render_frame_draw` is what keeps it, and the blocks swap only after a frame that succeeded and only for an effect that was on. An effect turned on starts from nothing, as what its blocks hold is from before it was turned off. See `cuda/src/post/README.md` and `cuda/src/shade/README.md`.
