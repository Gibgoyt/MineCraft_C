# cuda/src/shade

**Purpose:** one colour per pixel of a frame's hits: the texture of the face that was hit, at a mip chosen by distance, under that face's brightness, the light stored in its voxel and what one ray towards the sun saw, in linear light. A ray that lands on a texel of alpha 0, on a part-covered texel, or on water goes on to what is behind it. Water reflects one ray and takes colour with depth.
**Tier:** a CUDA area. It holds no memory. Device blocks come from its caller, and are checked against what is asked of them.
**Owns:** nothing.
**Lifetime:** each call starts a kernel and returns.
**Threads:** the `gpu` thread, or the one thread of a run with no threads.
**Public functions** (`run/include/gpu_bridge/shade/index.h`, for C and CUDA):

- `shade_frame`: the kernel.
- `shade_pixels_bytes`: what its target needs.
- `shade_face_brightness`, `shade_face_towards`, `shade_sky_level`, `shade_block_level`, `shade_sun_towards`, `shade_surface_light`, `shade_sun_bias`, `shade_face_uv`, `shade_texel_of`, `shade_mip_level`, `shade_level_dimension`, `shade_srgb_to_linear`, `shade_pixel_tangent`, `shade_water_fresnel`, `shade_water_tint`, `shade_fog_behind`, `shade_own_kind_behind`: its arithmetic, inline on host and device, so that a test on the host runs the same lines.

**May be called by:** `render`, `cuda/src/frame/`.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/post` for the sky and fog it draws behind glass and in water, `gpu_bridge/status`, `gpu_bridge/traverse`, and `cuda/include/traverse/` for a pixel's direction and the ray loop.

## What is here, and what is not

Point sampling, on purpose: blocks are meant to look like their pixels. The mip is by distance alone, as the plan says, so a surface seen at a low angle stays sharper than it strictly should, and shimmers a little. Stage 09.4's temporal pass is what deals with that.

## The light on a surface

`shade_surface_light`, with `shade_sun_t` as `render_day_fill` worked it out for this moment:

    face brightness x (ambient x sky level + block colour x block level) + direct x how square it is to the sun x what the ray saw

Each kind of face keeps its one brightness: a top is 1, a bottom a half, the sides facing y 0.8 and those facing x 0.6. A level goes down by 0.8 a step from 15, which is exactly 1. At noon a top in the sun comes to about 1, so a pack's colours show as they were drawn.

**The stored light is the voxel's own, the one the ray stopped in**, its byte from the hit, sky in the high half. Air is not on the card, by `run/src/render/encode/`'s format. That settles the decision Stage 08.2 issue 3 left for here, in favour of keeping the format: **Stage 11.1 must leave in each voxel with a block the light its faces should show, the brightest of its neighbours in the air, before the section goes out.** A flood fill that leaves a solid voxel at 0 gives dark faces, with this code doing what it says. One voxel has one byte for all six of its faces, so a wall one block thick shows the brighter side's stored light on its dark side too, though the sun ray still knows better. `run/src/world/light/` does this: see "What a block holds" in its README. What the choice costs and what would be done otherwise is in this stage's plan file.

## The sun ray

One per surface that faces the light and has any sky light stored, from a hair off that surface in the voxel's own coordinates, so it is as exact far from the eye as near. It is `traverse_trace` again, with `resume` set so that it takes only what it enters, and a surface cannot meet itself. The column heights send it to the top of the world at once, once it is above everything. What it meets goes by that block's texture at the mip the surface is seen at: a hole lets it on, a part-covered texel takes its share and lets it on, an opaque texel ends it, and water goes by how much of it there is: see Water. At `SHADE_SUN_LAYERS` surfaces it calls the sun hidden, which is what a thick wood looks like anyway. At night the same ray goes to the moon. Faces away from the light pay nothing.

Shadows have hard edges, one ray's worth, and it does not bounce. There are no soft edges until a later stage's temporal pass has something to blend.

A texel of alpha 0 is a hole. The kernel sends the ray on from there with `traverse_trace`, through the world it is given, and looks at the next surface, up to `SHADE_ALPHA_LAYERS` in all, and that one it draws: on a line of leaves that many holes long, it stops on the last, with that texture's mean colour, which is what far leaves look like anyway. Only the pixels that land on a hole pay for this, and they pay per hole. Holes behind a hole are in their own place: a leaf's far side is not drawn, since a ray takes only what it enters. The blob promises a cutout texture is 0 or 255 at every mip, so no threshold is needed, and a far tree stays about as see-through as a near one.

`shade_scene_t.features` is the setting. With `SHADE_FEATURE_TRANSPARENCY` off, a ray stops at water and at a part-covered texel on the texture's mean colour, as it did before there was anything behind them, and `SHADE_FEATURE_WATER_REFLECTION` and `_WATER_TINT` then do nothing. With `SHADE_FEATURE_ALPHA` off a hole shows the texture's mean colour, as it did before there were any. `SHADE_FEATURE_SHAPES` is carried here for `render_frame_draw`, which gives no shape table to a frame with it off. With `SHADE_FEATURE_SHADOW` off no ray goes out and the sun reaches everything that faces it. With `SHADE_FEATURE_LIGHT` off every voxel reads as open sky with no block light. `SHADE_FEATURE_SKY` is seen only by `render_frame_t`: off, it sends a `shade_sun_t` of ambient 1 and nothing else, which leaves each face its one brightness, to the bit, as before there was a sun. The cost of each is in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-1-non-cube-shapes.md` and `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-2-sky-shadows-light.md`.

Only frame 0 of an animated texture is drawn. A block with no texture, or a material past the table, draws magenta, which nothing else in the game is, so that the bug shows.

## See-through: near to far

A pixel is added up from the near surface to the far: `sum` is what has been seen, and `through` how much of whatever is further on still shows, for each channel. Its `t` is the first surface it drew, which is what fog and later passes go by. Where it goes on from:

- **A hole** adds nothing, as before.
- **A part-covered texel**, which is ice here, adds its colour by its alpha, under its own light and sun ray, and lets the rest through. With `SHADE_FEATURE_TRANSPARENCY` off, or `SHADE_ALPHA_LAYERS` in, it is the end: the old stop on the texture's mean colour.
- **The far side of what a ray just left** is no surface: the same block, and next to it across the face the ray came in by (`shade_own_kind_behind`). So a wall of glass, whose texture is a frame of opaque texels around a hole, has no frames inside it, and a stack of ice has no faces inside it. Only glass and part-covered blocks count: leaves still show the leaves behind them, which is most of what a tree looks like.
- **What is behind it all**, if the ray meets nothing, is the sky with the sun's disc in it, drawn here: post draws the sky only for a pixel with nothing on it.
- **Fog.** Post fogs a whole pixel by its first surface. Each layer behind that gets only the rest of its own, `shade_fog_behind`, so the two together come to what that layer's distance should have.

The sun ray goes on the same way, so a shadow agrees with what is seen: it passes the far side of what it left, takes each part-covered texel's share, and takes the water its own way, below.

## Water

The block `shade_scene_t.water_material` names, which only `render_frame_t` needs to know. A sea is a great many voxels of one block with nothing to see between them, so a ray does not start a trace for each. **`shade_water_through` steps along the ray cell by cell, reading the tree's three levels at each, to where the water stops**, and the trace goes on from there, from the last voxel's own entry, so that it takes what is past that voxel and nothing before. That is one memory walk for a whole body of water however deep, and `SHADE_WATER_SEEN_BLOCKS` is the most it walks with tint on.

A pixel of water is, near to far:

    reflection x F + (1 - F) x (surface x alpha + (1 - alpha) x (given by depth + what got past x behind))

- **The surface** is the texture at its alpha, lit as any surface with its own sun ray. The pack's water is coloured, so no biome tints it, and its alpha of about 0.7 is what makes shallow water look like water and not a pane.
- **The reflection, `SHADE_FEATURE_WATER_REFLECTION`,** is one ray off the face the eye's ray came in by, weighted by Schlick's Fresnel with 0.02 straight on. It stops on the first thing it meets that is not a hole, with the light stored in its voxel and all the sun that faces it, and no ray of its own to ask: no shadow in a reflection, on purpose, and no second reflection. Water it meets shows its mean colour. If it meets nothing it has the sky with the sun's disc, so a low sun glitters on the sea. Its fog is by the whole way from the eye. Only the first face of a body of water reflects, so a ray from inside water sees no mirror: **there is no total internal reflection** and from below the surface is a window. That is the simple one the plan asks for.
- **The depth tint, `SHADE_FEATURE_WATER_TINT`,** is `shade_water_tint`: what passes falls by an exponential with depth, per channel with red first, and what it took comes back as a deep colour under the light on that water's top. That light has all the sun and no ray of its own: a shadow does not lie on what is in the water, only on its surface and bed. Past `SHADE_WATER_SEEN_BLOCKS` of it the ray stops, and what it no longer shows is under a twenty-fifth of itself in the channel that lasts longest, before what the surface's alpha takes.
- **A sun ray in water** takes `SHADE_WATER_ABSORB_SUN` per block, one number and not three, as it carries no colour, and nothing for the surface's alpha. So a bed in the shallows has most of the sun on it, and a deep one has only its stored sky light. With tint off the water is clear to the sun as well. A ray with tint on that goes past `SHADE_WATER_SEEN_BLOCKS` calls the sun hidden.

From inside a body of water (a hit that says `TRAVERSE_FACE_INSIDE`) there is no surface: only the tint, out to where the water ends, and what is behind. Going out of the water into air draws nothing at that surface. A level of water below the voxel's top is not here: all water is a whole cube until Stage 11.2 says otherwise. The cost of each setting is in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-3-water-glass.md`.
