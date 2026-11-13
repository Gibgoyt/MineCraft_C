# run/src/render/frame

**Purpose:** the C side of a frame: the tables the kernels read, the free-fly camera, the finished pictures on their way from the gpu thread to the main thread, and putting one on the screen.
**Tier:** Arena: it takes from the arena its parent gives, and its device blocks come from its parent too.
**Owns:** nothing of its own but the events and copy stream inside `frame_gpu_t`, three pins on the pictures its parent holds, and, through `frame_gpu_init`, the pins on the frames on their way and the staging, also from its parent's arena.
**Lifetime:** `render_open_frame` to `render_close_frame`.
**Threads:** `gpu` draws, and `main` takes and presents. With no threads, one thread does both. The camera functions touch only what they are given.
**Public functions** (`run/include/render/frame/index.h`):

- `render_exchange_start`, `render_exchange_publish`, `render_exchange_take`, `render_exchange_waiting`: a triple buffer's numbers, from one thread to another, without either ever waiting.
- `render_camera_start`, `render_camera_fly`, `render_camera_to_traverse`: where the eye is, one step of flying, and what kernels take. `render_camera_to_traverse_drawn` (plan 008 phase 05): the same for a picture drawn smaller than it is shown, with the shown picture's field of view and aspect.
- `render_frame_scale_set`, `render_frame_drawn_size` (plan 008 phase 05): the size the next frames are drawn at, in percent of each side of the output, and what that comes to. See "Drawn smaller" below.
- `render_frame_texture_table_bytes`, `render_frame_face_table_bytes`, `render_frame_shape_table_bytes`, `render_frame_arena_bytes`: sizes for the parent.
- `render_frame_init`, `render_frame_deinit`: the tables and textures sent to the card, and the pictures pinned. The face table holds, after its faces, the blocks no ray of shade goes on through, for the far field (`lod_light_t.stops_after_faces`, `lod_stops_offset`) (see `cuda/src/lod/README.md`, "Only where shade will look").
- `render_frame_features_set`: which effects the next frames draw with. `SHADE_FEATURES_ALL` unless told otherwise, and `game` asks for `SHADE_FEATURES_PLAY`, as the ones with a history do not suit a test that wants one exact picture.
- `render_frame_time_set`, `render_day_fill`: the time of day, and what sky and light it means, by the fixed path below. For tests and benches that set a time straight on a frame.
- `render_frame_sun_set`, `render_sky_fill`: the sun given from outside as a unit vector, and what sky and light it means. The game uses this from plan 004 phase 02, handing it the sun of the world clock at the drawing camera's latitude. `render` knows no latitude: it is given three floats, stored in `render_frame_t.sun_given` with `sun_from_outside` set, which `render_frame_time_set` clears.
- `render_frame_fog_set`: how far off the fog is whole, for a render distance that has changed.
- `render_frame_under_water_set`: whether the eyes are in water, for what `post` draws: see `cuda/src/post/README.md`.
- `render_frame_draw`: a whole frame, from world_buffers as they stand and a camera, on the card and home.
- `render_frame_queue`, `render_frame_finish` (plan 008 phase 03): `render_frame_draw_weather` in two halves. The queue reads the buffers, the camera and the weather, sends the entity lists through the frame's staging, queues the kernels on the device picture of its flight slot (`borrowed_xrgb_second` for slot 1), and moves the temporal state on, as that is about the order of frames. The finish waits for the oldest frame queued, adds it to the books and publishes it. At most two are queued (`on_way`). A frame queued alone goes home into the exchange's back slot at once; with one before it on its way, that one goes home only now, behind this frame's copies and kernels (a copy to the card queued behind a copy home still waiting for its picture waits for it: measured), and this one at the first's finish, into the slot its publish gives back, so the exchange keeps its three slots. `render_frame_oldest_whole` and `render_frame_oldest_done` ask without waiting. `render_frame_draw_weather` is a queue and a finish, after finishing anything on its way.
- `render_frame_draw_weather`, `render_weather_take` (and `render_weather_take_staged`, which sends the map through the frame's staging), `render_weather_apply`, `render_weather_visibility`, `render_weather_flicker`: the same frame with the weather where the eye is (plan 004 phase 08), what the thread that draws makes of each slot the sim sends (`render_weather_slot_t`) as `render_weather_t`, and the arithmetic of what weather does to the sky, the light and the fog. See "Weather" below.
- `render_frame_take`, `render_frame_present`, `render_frame_present_full`: the newest picture, and copying it to a display buffer; the last for pixels of the target's own size. A picture is always the output's size (`width` by `height`), whatever its rung.
- `render_frame_report`, `render_frame_percentiles`: what the frames took.

**May be called by:** `render` (the Owner) and `render/entities`, `game`, `player` and `player/camera` for `render_camera_t` and the exchange, and `ui/screen_inventory` for `render_exchange_t`.
**Depends on:** `render/lod`, for what a frame draws past level 0 and its bands, `arena`, `log`, `assert`, `machine`, `texture`, `display/flip` for `display_buffer_t`, `simd/copy`, the generated block tables, `gpu_bridge/device_memory`, `gpu_bridge/world_buffers`, `gpu_bridge/traverse`, `gpu_bridge/shade`, `gpu_bridge/post`, `gpu_bridge/frame`, `render/entities`, whose scene a frame hands its kernels.

## How a picture reaches the screen

The plan's own words: copy to pinned host memory, then into the dumb buffer, then a page flip. Here that is `frame_gpu_queue` and `frame_gpu_home` into one of three pinned pictures, `frame_gpu_finish`, `render_exchange_publish`, then on the main thread `render_frame_take` and `render_frame_present`, whose copy is `asm/src/copy`'s streaming kernel, with its fence, into whatever `display_buffer_t` it is given. The HUD comes between those two in Stage 14.2.

Three pictures, so that the gpu thread never waits for the display and the display never waits for the card: if a new one is not ready for this flip, the last one shows again.

## The day

`render_day_fill` is arithmetic, once a frame, from a fraction of a day: 0 midnight, 0.25 sunrise in the east, 0.5 noon, 0.75 sunset in the west. The sun's path leans 20 degrees to the south, so that noon has shadows and a side facing south is not the same as one facing north. Colours are mixed from day, twilight and night by how high it is, which come to 1 between them and do not overlap. What lights the world is the sun, or from sunset the moon opposite it, going to nothing at the horizon as one takes over from the other. Colours are this file's and nothing else's, in linear light.

`render_day_fill` works out its fixed sun and hands it to `render_sky_fill`, which does everything else, so every picture drawn through `render_day_fill` is what it was before the split, to the bit. With a sun given from outside, the height that mixes day, twilight and night is `direction[2]`, the sine of the real elevation. The fixed path is plan 004's real sun at 0.35 rad (20.05 degrees) north on an equinox, which `tests/run/climate/sun/` checks.

`shade_scene_t.sun` and `post_settings_t` are always what the settings and the time make them, worked out again when either changes: so a kernel has few cases of its own, and with `SHADE_FEATURE_SKY` off it is sent the plain sky and each face's one brightness. This file keeps no clock: `game` does.

## The camera

`x` is east, `y` north and `z` up. A yaw of 0 looks east, and a positive yaw turns to the north. W and S go along where it looks, level, A and D across that, Space and Left Shift up and down, Left Ctrl five times as fast, and the wheel sets the speed. `game` turns keys into `render_fly_input_t`.

The position is `float64_t`, and goes to the kernels as a whole cell and a fraction, so the picture is as exact far from the origin as near it.

## From frame to frame

`post_temporal_t` holds what goes from one frame to the next: the last frame's camera with no jitter, this one's jitter, a count of frames, and which block of each two the last frame wrote. `render_frame_queue` is what keeps it, and the blocks swap as soon as a frame is queued (the next frame queued reads what this one writes, in the stream's order), and only for an effect that was on. A frame counts as drawn before for the histories once it is queued, finished or not. An effect turned on starts from nothing, as what its blocks hold is from before it was turned off. See `cuda/src/post/README.md` and `cuda/src/shade/README.md`.

## Drawn smaller (plan 008 phase 05)

`render_frame_t.width` and `height` are the output's: what goes home, what main shows, and what every per-pixel block was sized for once. `scale_percent` is the next frame's rung, `POST_SCALE_WHOLE` from `render_frame_init`; the game sets it before each draw (`run/src/game/pace/` picks it: the render scale at the top, and while turning or late a rung under it, down to the floor setting). `render_frame_queue` then:

1. works out the drawn size with `post_scaled_side`, the static render scale's old arithmetic, and the drawn camera with `render_camera_to_traverse_drawn`: the same view, fewer pixels. Jitter stays a fraction of a drawn pixel, so TAA accumulates as at full size.
2. makes the output's camera too, with no jitter, and hands it to the kernels as `frame_args_t.borrowed_output_camera` when the sizes differ. Post then writes the drawn picture into the hits block (free once shade is done), `post_upscale` brings it up bilinear into the frame's device picture, and the outline and the placing preview are drawn over that by the output's camera, so they stay one output pixel wide (their rectangles, worked out in the drawn camera's pixels, are scaled up and widened by one).
3. keeps the drawn camera, without jitter, as `temporal.previous`: the next frame reads TAA's and AO's history at that size (see `run/include/gpu_bridge/post/index.h`, "pictures drawn smaller than they are shown"). A change of rung is a reprojection like any other.

At `POST_SCALE_WHOLE` none of it runs: `borrowed_output_camera` is nullptr and a picture is what it was, to the byte. The static render scale is the ladder's top rung and goes the same way; the CPU's nearest-pixel bring-up (`game_view_bring_up`) is gone. Plan 009 phase 07 (temporal upscaling) is to draw at these same rungs and accumulate at the output's size in place of step 2's bilinear.

## Weather

Plan 004 phase 08. The sim sends a `render_weather_slot_t` a tick through a triple buffer it owns (`game/weather`): the rain rate, snow share, wind, cloud, fog and dust at the eye, a strike's bolt and flash, and the rain top map round the eye with a revision. **`render_weather_take`**, on the thread that draws, eases the numbers toward each new slot by `1 - exp(-dt / 5 s)` over the ticks between, so weather never pops (at once for the first slot and a forced one), keeps the bolt's age and the fall's clock (`time_s`, the slot's tick wrapped at an hour, plus up to 0.05 s with threads, so drops keep falling between ticks; with none, one slot is one picture), and sends the 8 KiB map to its device block only when the revision moves.

**`render_frame_draw_weather`** draws a frame from copies of the frame's sky, light and fog with **`render_weather_apply`** over them, so the next frame starts from the clear day again and nothing of the weather stays in `render_frame_t`:

- The sky mixes toward its own brightness, `0.3 R + 0.59 G + 0.11 B` times (0.92, 0.95, 1.0), by 0.8 of the cloud. The sun's glow goes by all of it, its and the moon's discs by 0.9, direct light by 0.75 (shadows fade under overcast), ambient by 0.3.
- A flash adds 3 f to the sky and 1.5 f to the ambient light, with `f = 1 / (1 + (d / 500)^2)` and the bolt's flicker.
- Visibility `max(32, F (1 - 0.85 max(fog, dust)) / (1 + rain / 10))` against the setting's fog end F: when it is nearer, the fog ends there and starts at 0.3 of it, so weather fog is thick close up. 124 blocks in a 30 mm an hour storm at render distance 32, 117 in a 0.9 fog.
- The fog's colour goes toward grey (0.55, 0.57, 0.60) by the larger of cloud and fog, times the daylight, or toward dust (0.62, 0.47, 0.30) by the dust.
- The kernels get a `post_weather_t`: the eased density, the wind, the clock, the drops' colour (the ambient light), the bolt from the eye's cell. See `cuda/src/post/README.md`.

With `SHADE_FEATURE_WEATHER` off (`--no-weather`), or no slot with weather, the frame is exactly `render_frame_draw`'s. With everything at 0, `render_weather_apply` leaves the sky and light to the bit, which `render_feature_weather` checks. `render_t` holds the map's device block in what was its padding, as `app_t` is at main's frame limit.

The snow layer (`config/blocks/snow_layer`) is `thin`, and `frame_shape_of` gives it `TRAVERSE_SHAPE_LAYER`, an eighth of a block on the floor, as it gives a lily pad `TRAVERSE_SHAPE_FLAT`.

