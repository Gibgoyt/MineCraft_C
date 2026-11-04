# cuda/src/post

**Purpose:** from shade's pixels to what goes on the screen: the sky where nothing was hit, with the sun and moon, distance fog, god rays, the temporal pass, and the tone map to XRGB8888.
**Tier:** a CUDA area. It holds no memory. Device blocks come from its caller, and are checked against what is asked of them.
**Owns:** nothing.
**Lifetime:** each call starts a kernel and returns.
**Threads:** the `gpu` thread, or the one thread of a run with no threads.
**Public functions** (`run/include/gpu_bridge/post/index.h`, for C and CUDA):

- `post_frame`: the kernel, with nothing from any other frame.
- `post_frame_temporal`: the same with god rays on top, and with the temporal pass after it, as asked.
- `post_xrgb_bytes`, `post_history_bytes`, `post_shafts_bytes`, `post_shafts_across`: what its targets need.
- `post_sky`, `post_sky_discs`, `post_fog`, `post_fog_way`, `post_fog_along`, `post_tone_map`, `post_xrgb`, `post_jitter`, `post_camera_jitter`, `post_reproject`, `post_taa_squeeze`, `post_taa_unsqueeze`, `post_catmull_rom`: its arithmetic, inline on host and device, so that a test on the host runs the same lines.

**May be called by:** `render`, `cuda/src/frame/`.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/status`, `gpu_bridge/traverse`, `gpu_bridge/shade` for the form of its input, and `cuda/include/traverse/` for a pixel's direction.

## The tone map

A straight line up to 0.8, and a smooth roll towards 1 past it, then the sRGB curve. Up to 0.8 in linear light a texel under full brightness comes out as the byte it went in as, give or take rounding, which a test checks: nothing here may wash out the pack's colours. The roll is for when stored light and block light add up past 1, under a torch at noon. There is no dither, because one script must give one picture, byte for byte.

## The sky

`post_settings_t` holds this moment's colours, from `render_day_fill`: a straight mix from the horizon to the zenith by how far up a ray looks, with the sun's glow added around where it is, which is most of a red evening. A ray that hit nothing also gets the sun's disc or the moon's, opposite it, by `post_sky_discs`. No stars.

## The fog

From nothing at 70 % of `fog_end` to all sky at `fog_end`, which is a chunk short of what is loaded, so that the edge of the loaded world melts into the sky. Its colour is `post_sky` in that pixel's direction: the same glow, so fog takes colour from the sun, and no disc, as a sun does not show through a hill. With `SHADE_FEATURE_FOG` off, `render_frame_t` sets a range only `TRAVERSE_COARSE_SKY` is past, so that the kernel has no case for it.

**That edge is out to the side, so it is the side that counts.** `post_fog_along` takes what a ray goes up or down only as far as `fog_start`, by `post_fog_way`, so from `z = 1250` the ground under the eye stays in sight at any render distance, clear below and melting into the sky by `sqrt(fog_end^2 - fog_start^2)` out, about 71 % of `fog_end`. A ray going up or down by less than `fog_start`, which is all of them at ordinary heights, gets exactly `post_fog`. `shade_fog_behind` and what `render_entities` leaves out go by the same way. A reflection's way, which a mirror bent, counts as if it had gone on along the eye's ray.

## The temporal pass

`SHADE_FEATURE_TAA`. **Each frame looks at a different point inside each pixel,** the eight first points of Halton's sequence in 2 and 3, by `post_camera_jitter` moving the camera's `forward`, so that the coarse pass, the rays and shading all look off centre with no case of their own. This pass then adds each new frame to what it has:

1. `post_kernel` works out what is seen, as it does anyway, and leaves it in `pixels` squeezed by `post_taa_squeeze`, by the largest of its channels. A sun's disc of 20 next to a sky of 0.7 would otherwise stay aliased however long the history, as a tenth of 20 is more than all of the sky.
2. `post_taa_kernel` asks where that pixel's surface was on the last picture: `post_reproject`, along the pixel's ray **with no jitter**, at `t`. A camera that did not move gets its own pixel back, exact, and nothing is blurred however long it stands there. One that moved gets Catmull-Rom over 16 of the last pixels, as anything softer loses a little sharpness on every frame. The sky is at no distance, so it moves only with the look. A place off the last picture or behind that eye has no history.
3. **The history is held to what this frame says is possible:** within 1.25 standard deviations of the mean of the 3 by 3 pixels around this one, per channel. That is what stops ghosts: when something moves or a block goes, what it left is out of that range, and goes at once. It also takes away some of the sharpness a still picture could have, so it is the one number here to change if something shimmers or ghosts.
4. A tenth of this frame is mixed in. So about 20 frames make up what is seen, which is a third of a second at 60 frames a second.

The result goes to the next history and, unsqueezed, through the tone map. Mixing takes place before the tone map, so it is exact for a flat colour, to within 2 of 255 by test. Nothing about this pass is random, so one script gives one picture. With the setting off no history is read or written and each frame has no jitter, and with it on again the history starts from nothing.

There is no sharpen after it. Blocks at a low angle are softer than one frame's, which is the point: that is the shimmer `cuda/src/shade/README.md` said was left for here.

## God rays

`SHADE_FEATURE_GOD_RAYS`, off unless asked for: `--god-rays`. `shade_shafts_frame` in `cuda/src/shade/` asks along each ray how much sun the air there gets, at half the picture's size each way, as a sun ray needs the textures and the loop that are there. This kernel adds what it found, under `post_temporal_t.shaft_colour`, on top of the fog: it only ever adds light. **Each pixel reads the four texels around it, by nearness, of those at about its own distance,** so that the sky beside a near pillar does not light the pillar's edge: a texel's distance is in `post_shaft_t.t`.
