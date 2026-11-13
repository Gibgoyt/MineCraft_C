# cuda/src/post

**Purpose:** from shade's pixels to what goes on the screen: the sky where nothing was hit, with the sun and moon, distance fog, god rays, what eyes under water see, the temporal pass, the weather's rain, snow and lightning (plan 004 phase 08), and the tone map to XRGB8888.
**Tier:** a CUDA area. It holds no memory. Device blocks come from its caller, and are checked against what is asked of them.
**Owns:** nothing.
**Lifetime:** each call starts a kernel and returns.
**Threads:** the `gpu` thread, or the one thread of a run with no threads.
**Public functions** (`run/include/gpu_bridge/post/index.h`, for C and CUDA):

- `post_frame`: the kernel, with nothing from any other frame.
- `post_frame_temporal`: the same with god rays on top, and with the temporal pass after it, as asked; and with a `post_weather_t` and its rain top map, or nullptr for none.
- `post_frame_upscaled` (plan 009 phase 07): the same with TAA on for a picture drawn smaller, its temporal pass at the output's size. `post_jitter_points`, `post_temporal_jitter_points`, `post_upscale_place`, `post_upscale_weight`, `post_upscale_cap`, `post_upscale_mix`: its arithmetic.
- `post_xrgb_bytes`, `post_history_bytes`, `post_shafts_bytes`, `post_shafts_across`, `post_weather_map_bytes`: what its targets need.
- `post_upscale` (plan 008 phase 05): a tone-mapped picture drawn smaller brought up to the output, bilinear in its bytes. `post_scaled_side`, `post_scaled_under`: a rung's side, and the drawn pixel under an output pixel. See "Drawn smaller" below.
- `post_sky`, `post_sky_discs`, `post_fog`, `post_fog_way`, `post_fog_along`, `post_water_fog`, `post_under_water`, `post_tone_map`, `post_xrgb`, `post_jitter`, `post_camera_jitter`, `post_reproject`, `post_taa_squeeze`, `post_taa_unsqueeze`, `post_catmull_rom`, and for the weather `post_weather_hash`, `post_weather_unit`, `post_weather_cells_round`, `post_weather_cell`, `post_weather_tilt`, `post_weather_open_sky`, `post_weather_layer`, `post_weather_drops`, `post_weather_bolt`, `post_weather_fog_colour`, `post_weather_apply`: its arithmetic, inline on host and device, so that a test on the host runs the same lines.

**May be called by:** `render`, `cuda/src/frame/`.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/status`, `gpu_bridge/traverse`, `gpu_bridge/shade` for the form of its input, and `cuda/include/traverse/` for a pixel's direction.

## The tone map

A straight line up to 0.8, and a smooth roll towards 1 past it, then the sRGB curve. Up to 0.8 in linear light a texel under full brightness comes out as the byte it went in as, give or take rounding, which a test checks: nothing here may wash out the pack's colours. The roll is for when stored light and block light add up past 1, under a torch at noon. There is no dither, because one script must give one picture, byte for byte.

## The sky

`post_settings_t` holds this moment's colours, from `render_day_fill`: a straight mix from the horizon to the zenith by how far up a ray looks, with the sun's glow added around where it is, which is most of a red evening. A ray that hit nothing also gets the sun's disc or the moon's, opposite it, by `post_sky_discs`. No stars.

## The fog

From nothing at 70 % of `fog_end` to all sky at `fog_end`, which is a chunk short of what is loaded, so that the edge of the loaded world melts into the sky. Its colour is `post_sky` in that pixel's direction: the same glow, so fog takes colour from the sun, and no disc, as a sun does not show through a hill. With `SHADE_FEATURE_FOG` off, `render_frame_t` sets a range only `TRAVERSE_COARSE_SKY` is past, so that the kernel has no case for it.

**That edge is out to the side, so it is the side that counts.** `post_fog_along` takes what a ray goes up or down only as far as `fog_start`, by `post_fog_way`, so from `z = 1250` the ground under the eye stays in sight at any render distance, clear below and melting into the sky by `sqrt(fog_end^2 - fog_start^2)` out, about 71 % of `fog_end`. A ray going up or down by less than `fog_start`, which is all of them at ordinary heights, gets exactly `post_fog`. `shade_fog_behind` and what `render_entities` leaves out go by the same way. A reflection's way, which a mirror bent, counts as if it had gone on along the eye's ray.

## Under water

`SHADE_FEATURE_UNDER_WATER`, and `post_settings_t.under_water` for a frame whose eyes are in water: `render_frame_under_water_set` says so, from `movement.eyes_under_water` by way of `game`. `post_under_water` multiplies the picture by a blue tint, then takes it to `water_colour` with distance, after the sky's fog and any god rays and before the temporal pass.

**The fog is what keeps it from being a void.** Stored light runs out in the sea, so a bed 13 blocks down is nearly black at noon, and 23 blocks down all of it, however near. A tint alone would multiply black. So the fog's colour comes from no light in the world, only from the sky's, and 12 % of it is there at the eye. It goes to all water by `POST_WATER_FOG_END`, 28 blocks, quickly at first. `render_frame_t` takes the colour down to 22 % at midnight: dark, but still water. It does not know how deep the eyes are, or what is over them, so a pool in a cave is as blue as a sea at noon.

**The distance is where the water stops.** An eye in the water starts inside a surface at no distance. `shade_kernel` left that as the pixel's `t`, as it wants for its own fog, and with `under_water` it gives the distance the water goes on for, or to the box in it. The temporal pass reads the same `t`, so under water it now reprojects a real place, and only there.

A body's flag is as of the last tick, and the eye is drawn between ticks, so coming up the picture can be blue for up to a tick with the eye just out. A flying eye has no body and no flag, so it never sees this.

Cost at 1920 by 1080, under a sea with every pixel water: post 0.251 ms against 0.246 ms with the setting off, frame 15.17 against 15.18, which is less than the two differ by from run to run.

## The temporal pass

`SHADE_FEATURE_TAA`. **Each frame looks at a different point inside each pixel,** the eight first points of Halton's sequence in 2 and 3, by `post_camera_jitter` moving the camera's `forward`, so that the coarse pass, the rays and shading all look off centre with no case of their own. This pass then adds each new frame to what it has:

1. `post_kernel` works out what is seen, as it does anyway, and leaves it in `pixels` squeezed by `post_taa_squeeze`, by the largest of its channels. A sun's disc of 20 next to a sky of 0.7 would otherwise stay aliased however long the history, as a tenth of 20 is more than all of the sky.
2. `post_taa_kernel` asks where that pixel's surface was on the last picture: `post_reproject`, along the pixel's ray **with no jitter**, at `t`. A camera that did not move gets its own pixel back, exact, and nothing is blurred however long it stands there. One that moved gets Catmull-Rom over 16 of the last pixels, as anything softer loses a little sharpness on every frame. **On a fast turn** (plan 008 phase 04), more than `POST_TAA_FAST_PIXELS` (2) pixels at the picture's centre since the last frame (`post_turn_pixels` on the host, `POST_TEMPORAL_FAST`), it reads bilinear's 4 (`POST_TAA_FAST_READ`): motion hides the softness. Measured at 148 MHz, post at 720 degrees a second was 7.02 ms by Catmull-Rom, 4.40 by bilinear and 7.76 by the 5-tap bicubic (five bilinear reads, Jimenez 2016: 20 loads here, as the history is a plain array and not a texture), against 4.20 still. The jitter is a bit of `post_temporal_t.flags`, and a kernel reads its pair from `POST_JITTER_TABLE`, held to `post_jitter` to the bit. The sky is at no distance, so it moves only with the look. A place off the last picture or behind that eye has no history.
3. **The history is held to what this frame says is possible:** within 1.25 standard deviations of the mean of the 3 by 3 pixels around this one, per channel. That is what stops ghosts: when something moves or a block goes, what it left is out of that range, and goes at once. It also takes away some of the sharpness a still picture could have, so it is the one number here to change if something shimmers or ghosts.
4. A tenth of this frame is mixed in. So about 20 frames make up what is seen, which is a third of a second at 60 frames a second.

The result goes to the next history and, unsqueezed, through the tone map. Mixing takes place before the tone map, so it is exact for a flat colour, to within 2 of 255 by test. Nothing about this pass is random, so one script gives one picture. With the setting off no history is read or written and each frame has no jitter, and with it on again the history starts from nothing.

There is no sharpen after it. Blocks at a low angle are softer than one frame's, which is the point: that is the shimmer `cuda/src/shade/README.md` said was left for here.

### At the output's size, for a picture drawn smaller (plan 009 phase 07)

**The history is always at the output's size.** A frame drawn at a rung under 100 % with TAA on goes through `post_frame_upscaled`: `post_kernel` as above at the drawn size, leaving the squeezed colours in `pixels`, then `post_taa_upscale_kernel`, one thread per *output* pixel, which writes the next history and the target at the output's size. So the card traces `camera.width × camera.height` rays (56 % of them at 75 %) and the picture is still the display's. The bilinear `post_upscale` stays only for TAA off.

1. **The jitter is longer.** A drawn pixel at 75 % is 1.33 output pixels wide, and the jitter has to visit every output pixel under it, so drawn smaller it goes through the first `POST_UPSCALE_JITTER_FRAMES` (16) Halton points, `POST_TEMPORAL_UPSCALE` saying so. The first eight are the eight, and at 100 % nothing changes. `POST_JITTER_TABLE` holds all sixteen.
2. **Each output pixel gathers.** Its middle is placed on the drawn picture (`post_upscale_place`), and the 3 × 3 drawn samples around the nearest jittered one are read. A sample counts 1 if its jittered middle is inside the output pixel, which is what the pixel at 100 % averages, and besides that `POST_UPSCALE_SOFT` (1 / 1024) of a tent `POST_UPSCALE_RADIUS` (1) drawn pixel wide (`post_upscale_weight`). So an output pixel with no sample inside it this frame still gets a value, and the next sample inside it outweighs that at once. The same nine samples give the mean ± 1.25 σ that the history is clamped to.
3. **The history counts its samples.** `post_taa_pixel_t.weight` says how many samples a pixel's history holds, and `post_upscale_mix` keeps a running mean of them: this frame's share is what it adds over all of them, up to a cap (`post_upscale_cap`). Moving, the cap is the drawn pixels per output pixel times 1 / `POST_TAA_BLEND`, which is ten frames' worth as at 100 %. Still, it is `POST_UPSCALE_STILL_FRAMES` (32) frames' worth, two turns of the jitter, so a still picture comes to the mean over each output pixel's own area: the picture at 100 %. With few samples near it this frame the history counts for more, and with nothing held the new is all of it.
4. **Reprojection** is along the output pixel's own ray, which has no jitter, at the nearest sample's `t`. It reads the output-size history with the last camera at the output's size, which differs from it only in where it was and where it looked (the drawn and output cameras share `forward`, `right` and `up`). The read is Catmull-Rom, or the fast read on a turn, as above, with the weight read the same way. The pass at 100 % reads the same history the same way and writes `POST_TAA_WEIGHT_WHOLE` (10) as its weight, so going between rungs is a reprojection like any other in either direction.
5. **After the history**, the weather goes on along the output pixel's ray, then the tone map. The outline and the preview are drawn after that, at the output's size, as before.

Measured in `render_feature_post` and recorded in `plans/009-ahmed-bohemoth-game-engine-optimizations-phase-07-temporal-upscaling.md`: how near 32 still frames at 75 % and 50 % come to 100 %, and whether a block that goes away leaves more ghost than at 100 %.

## Drawn smaller (plan 008 phase 05)

A frame may be drawn at a rung under the output's size (`run/src/render/frame/README.md`, "Drawn smaller"). Every kernel up to the temporal pass takes the drawn camera. With TAA on, since plan 009 phase 07, the temporal pass gathers at the output's size and its history is always the output's size (see "At the output's size" above). AO's history is still at the size of the frame that wrote it, read by `temporal.previous.width` and `height`. A still room that drops to 50 % for a frame is back within 2 in 255 of never having dropped after 30 frames (`render_feature_resolution`).

With TAA off, `post_upscale_kernel` takes the tone-mapped picture, rows of the drawn width, to the output: each output pixel's middle, placed on the drawn picture, between its four pixels by integer weights in 1/256ths, rounded, so a flat colour stays itself to the bit and one script gives one picture. It is in sRGB bytes, after the tone map: cheap, and the history stays in linear light at the drawn size. It runs only below 100 % with TAA off, under the frame's `FRAME_KERNEL_OVERLAY` mark, before the overlays. With TAA on, temporal upscaling has replaced it.

## God rays

`SHADE_FEATURE_GOD_RAYS`, off unless asked for: `--god-rays`. `shade_shafts_frame` in `cuda/src/shade/` asks along each ray how much sun the air there gets, at half the picture's size each way, as a sun ray needs the textures and the loop that are there. This kernel adds what it found, under `post_temporal_t.shaft_colour`, on top of the fog: it only ever adds light. **Each pixel reads the four texels around it, by nearness, of those at about its own distance,** so that the sky beside a near pillar does not light the pillar's edge: a texel's distance is in `post_shaft_t.t`.

## Weather

Plan 004 phase 08. `render_frame_draw_weather` passes a `post_weather_t`, plain numbers by value, and the rain top map, a 64 by 64 block of `int16_t` the sim keeps round the eye; with none, nothing below runs and the picture is exactly as before.

**Fog toward the weather's colour, before the history.** Where fog is, its colour is `post_weather_fog_colour`: the sky's, mixed toward grey (under cloud and in fog) or brown (in dust) by `fog_mix`. It is steady from frame to frame, so it goes in where fog always did. The sky's own grey, the sun's dimming, the flash and how near the fog comes are `render/frame`'s, in `post_settings_t` and `shade_sun_t`.

**Rain, snow and the bolt after the history, before the tone map.** A drop moves several pixels a frame: mixed into the temporal pass's history it would smear into haze and leave ghosts. So `post_weather_apply` goes over the picture in `post_taa_kernel` after `history_next` is written, along the pixel's ray with no jitter, and in `post_kernel`'s last branch when the temporal pass is off.

**The drops are procedural: nothing is stored per drop.** Round the eye stand `POST_WEATHER_LAYERS` (4) upright cylinders of radius 2, 4, 8 and 16 blocks, cut into cells about 0.25 blocks wide and 1.5 tall that slide down at the fall speed (9 blocks a second for rain, 1.5 for snow) as `time_s` goes on. A hash of the layer, the cell round and the cell up says whether a drop is in it, with chance `density` (the rain rate over 40 mm an hour, from 0.03 to 0.85), and where. A pixel's ray meets each cylinder once going out; it is covered if its place there is within the drop: a streak 0.03 wide and `max(0.6, fall / 60)` long for rain, at `POST_RAIN_ALPHA`, a round flake 0.08 across that wobbles 0.3 either way over 3 seconds for snow, at `POST_SNOW_ALPHA`. A sleet day has both, by `snow_share`. The drop's colour is the sky's ambient light times 0.6 for rain and 1 for snow, so rain is faint and grey at night. **In wind the cylinders lean:** a place `p` from the eye is at `p + p.z * wind / fall` across, so drops fall along the slope a drop in that wind would, at most 2.5 sideways for each block down. Heights are the world's, so a still eye sees the same drops whichever way it turns.

**A drop shows only before what the ray hit and under open sky:** its distance along the ray under the pixel's `t`, and its height at or above the top of the highest block of its column in the rain top map, plus one. A column not loaded, or off the map, is a roof. So from inside a house the rain shows through the window and never over the eye.

**The bolt** is 12 segments, given by the sim from the cloud base to the ground. A pixel is covered by `bolt_alpha` (its flicker: full, half, full over 0.2 s) times one less its ray's nearest distance to them over `bolt_width` (0.4 blocks, and 0.002 more for each block away, so a far bolt does not vanish), before what the ray hit, and goes toward a white with a little blue, bright enough to stay white through the tone map.

**Cost** at render distance 32 and 1080p: see `bench/world/README.md` and plan 004 phase 08's Issues found.

