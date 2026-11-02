# cuda/src/shade

**Purpose:** one colour per pixel of a frame's hits: the texture of the face that was hit, at a mip chosen by distance, under that face's brightness, the light stored in its voxel and what one ray towards the sun saw, in linear light. A ray that lands on a texel of alpha 0 goes on to what is behind it.
**Tier:** a CUDA area. It holds no memory. Device blocks come from its caller, and are checked against what is asked of them.
**Owns:** nothing.
**Lifetime:** each call starts a kernel and returns.
**Threads:** the `gpu` thread, or the one thread of a run with no threads.
**Public functions** (`run/include/gpu_bridge/shade/index.h`, for C and CUDA):

- `shade_frame`: the kernel.
- `shade_pixels_bytes`: what its target needs.
- `shade_face_brightness`, `shade_face_towards`, `shade_sky_level`, `shade_block_level`, `shade_sun_towards`, `shade_surface_light`, `shade_sun_bias`, `shade_face_uv`, `shade_texel_of`, `shade_mip_level`, `shade_level_dimension`, `shade_srgb_to_linear`, `shade_pixel_tangent`: its arithmetic, inline on host and device, so that a test on the host runs the same lines.

**May be called by:** `render`, `cuda/src/frame/`.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/status`, `gpu_bridge/traverse`, and `cuda/include/traverse/` for a pixel's direction and the ray loop.

## What is here, and what is not

Point sampling, on purpose: blocks are meant to look like their pixels. The mip is by distance alone, as the plan says, so a surface seen at a low angle stays sharper than it strictly should, and shimmers a little. Stage 09.4's temporal pass is what deals with that.

## The light on a surface

`shade_surface_light`, with `shade_sun_t` as `render_day_fill` worked it out for this moment:

    face brightness x (ambient x sky level + block colour x block level) + direct x how square it is to the sun x what the ray saw

Each kind of face keeps its one brightness: a top is 1, a bottom a half, the sides facing y 0.8 and those facing x 0.6. A level goes down by 0.8 a step from 15, which is exactly 1. At noon a top in the sun comes to about 1, so a pack's colours show as they were drawn.

**The stored light is the voxel's own, the one the ray stopped in**, its byte from the hit, sky in the high half. Air is not on the card, by `run/src/render/encode/`'s format. That settles the decision Stage 08.2 issue 3 left for here, in favour of keeping the format: **Stage 11.1 must leave in each voxel with a block the light its faces should show, the brightest of its neighbours in the air, before the section goes out.** A flood fill that leaves a solid voxel at 0 gives dark faces, with this code doing what it says. One voxel has one byte for all six of its faces, so a wall one block thick shows the brighter side's stored light on its dark side too, though the sun ray still knows better. `run/src/world/light/` does this: see "What a block holds" in its README. What the choice costs and what would be done otherwise is in this stage's plan file.

## The sun ray

One per surface that faces the light and has any sky light stored, from a hair off that surface in the voxel's own coordinates, so it is as exact far from the eye as near. It is `traverse_trace` again, with `resume` set so that it takes only what it enters, and a surface cannot meet itself. The column heights send it to the top of the world at once, once it is above everything. What it meets goes by that block's texture at the mip the surface is seen at: a hole lets it on, a part-covered texel takes its share and lets it on, an opaque texel ends it. At `SHADE_SUN_LAYERS` surfaces it calls the sun hidden, which is what a thick wood looks like anyway. At night the same ray goes to the moon. Faces away from the light pay nothing.

Shadows have hard edges, one ray's worth, and it does not bounce. There are no soft edges until a later stage's temporal pass has something to blend.

A texel of alpha 0 is a hole. The kernel sends the ray on from there with `traverse_trace`, through the world it is given, and looks at the next surface, up to `SHADE_ALPHA_LAYERS` in all, and that one it draws: on a line of leaves that many holes long, it stops on the last, with that texture's mean colour, which is what far leaves look like anyway. Only the pixels that land on a hole pay for this, and they pay per hole. Holes behind a hole are in their own place: a leaf's far side is not drawn, since a ray takes only what it enters. The blob promises a cutout texture is 0 or 255 at every mip, so no threshold is needed, and a far tree stays about as see-through as a near one.

`shade_scene_t.features` is the setting, with `SHADE_FEATURE_ALPHA` off a hole shows the texture's mean colour, as it did before there were any. `SHADE_FEATURE_SHAPES` is carried here for `render_frame_draw`, which gives no shape table to a frame with it off. With `SHADE_FEATURE_SHADOW` off no ray goes out and the sun reaches everything that faces it. With `SHADE_FEATURE_LIGHT` off every voxel reads as open sky with no block light. `SHADE_FEATURE_SKY` is seen only by `render_frame_t`: off, it sends a `shade_sun_t` of ambient 1 and nothing else, which leaves each face its one brightness, to the bit, as before there was a sun. The cost of each is in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-1-non-cube-shapes.md` and `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-2-sky-shadows-light.md`.

A part-covered texel still stops a ray, and shows that texture's mean colour through it, until Stage 09.3 lets rays go on for glass and water. Water is an opaque surface for now. Only frame 0 of an animated texture is drawn. A block with no texture, or a material past the table, draws magenta, which nothing else in the game is, so that the bug shows.
