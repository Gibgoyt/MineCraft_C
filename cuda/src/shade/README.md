# cuda/src/shade

**Purpose:** one colour per pixel of a frame's hits: the texture of the face that was hit, at a mip chosen by distance, under that face's brightness, the light stored in its voxel and what one ray towards the sun saw, in linear light. A ray that lands on a texel of alpha 0, on a part-covered texel, or on water goes on to what is behind it. Water reflects one ray and takes colour with depth.
**Tier:** a CUDA area. It holds no memory. Device blocks come from its caller, and are checked against what is asked of them.
**Owns:** nothing.
**Lifetime:** each call starts a kernel and returns.
**Threads:** the `gpu` thread, or the one thread of a run with no threads.
**Public functions** (`run/include/gpu_bridge/shade/index.h`, for C and CUDA):

- `shade_frame`, `shade_frame_entities`: the kernels of a picture, in the order under "Why it is several kernels", with AO when asked.
- `shade_shafts_frame`: god rays, from what `shade_frame` just wrote.
- `shade_pixels_bytes`, `shade_ao_bytes`: what its targets need.
- `shade_face_brightness`, `shade_face_towards`, `shade_sky_level`, `shade_block_level`, `shade_sun_towards`, `shade_surface_light`, `shade_sun_bias`, `shade_face_uv`, `shade_texel_of`, `shade_mip_level`, `shade_level_dimension`, `shade_srgb_to_linear`, `shade_srgb_byte`, `shade_pixel_tangent`, `shade_water_fresnel`, `shade_water_tint`, `shade_fog_behind`, `shade_own_kind_behind`, `shade_surface_light_near`, `shade_surface_light_stored`, `shade_ao_turn`, `shade_ao_hash`, `shade_ao_sample`, `shade_ao_direction`, `shade_ao_near`, `shade_ao_same_surface`, `shade_shaft_phase`, `shade_shaft_part`, `shade_shaft_noise`: its arithmetic, inline on host and device, so that a test on the host runs the same lines.

**The far field:** where a ray of its own leaves the world, with `shade_scene_t.borrowed_lod_hits` there, it stops on what `cuda/src/lod/` found on that same ray: `shade_far_colour`, the face's mean colour, or what near water comes to when deep, under the same light. So fog, water and glass in front of it work as they do on anything. See that README.

**May be called by:** `render`, `cuda/src/frame/`.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/post` for the sky and fog it draws behind glass and in water, `gpu_bridge/status`, `gpu_bridge/traverse`, and `cuda/include/traverse/` for a pixel's direction and the ray loop.

## Why it is several kernels

A warp is 32 threads running one instruction stream, and threads that come to the same code at different times each run it while the rest wait. So where the rays of one pixel's sun or AO sat inside the loop over a pixel's surfaces, the pixels of a warp that got to them at different layers took turns, each paying for a whole trace, and a warp with one pixel of AO to do took as long as if all 32 had it. Taking those rays out to kernels of their own is what took `jungle/orbit` from 37 to 124 pictures a second with the same picture, and the numbers are in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-17-stage-3-cost-pass.md`.

A frame's shading, in stream order:

1. `shade_water_kernel`, with `SHADE_FEATURE_TRANSPARENCY`: for a pixel whose ray first meets a surface of water, its reflection and its sun ray, left in that pixel's slot in the pixels block. Where the water then goes on past what a ray looks through, it finishes the pixel, as the same sums as below, and says so with `TRAVERSE_INFO_CAPPED` in the hit. It walks the water with `shade_cell_material_kept`, which reads the tree only on leaving a section or a brick: the same answers as `shade_cell_material`, and only here, as those many more live values in `shade_kernel` cost every pixel more than they saved there.
2. `shade_kernel`: every pixel from near to far, as below. Rays to the sun and of reflection are asked here only for what is not the last surface, or for water behind something.
3. `shade_sun_kernel`, with shadows: the sun ray of a pixel's last surface.
4. `shade_ao_fresh_clear_kernel`, `shade_ao_kernel`, with AO: its history, and its rays on a pixel's turn. A pixel with nothing to go on goes on a list.
5. `shade_ao_fresh_kernel`, with AO: the `SHADE_AO_FRESH_RAYS` of each pixel on the list. As a list, so that a warp is 32 pixels with rays to send, and not 32 of which one has. The list lies behind AO's history in its block, and only the card knows its length, so this launches for as long as it could be, and what is past its end returns at once. A kernel for the whole picture with nothing to do costs about the same as a wait to hear the length.

**How 3, 4 and 5 know what to add.** A pixel's colour is a straight line in what its last surface's sun ray and AO find, so `shade_kernel` leaves, as `shade_ao_work_t` in AO's next block, what that pixel shows of its last surface per unit of light on it, and each adds its own part. The hit of that surface stays in the hits block, which nothing reads after this, and a pixel with none says `TRAVERSE_FACE_MISS` there. Sums that once ran as one multiply now round in another order, so 0.001% of channels moved by 1 in the 6 rows looked at, and nothing by more. With no history block to leave it in, as in a bare `shade_frame`, or with neither AO nor `SHADE_FEATURE_SUN_APART` asked for, the sun ray is asked in `shade_kernel` and the hits block stays as it was: the tests that hold a frame's hits to something ask for that, and `game` always sets the bit, so no setting of the game does.

**Tried, and no better, each measured:** a kernel for what has only a texel to read, in place of `shade_kernel` doing it; `__noinline__` on the ray functions (30% slower); `cudaFuncCachePreferL1`; the list's exact length before launching; no `log2f` in `shade_mip_level`, no `powf` in `post_sky`, and no sky for a pixel no fog reaches; a jump through a section all of water. What did help and is not a kernel: the tables for `shade_srgb_byte` and the light levels, `SHADE_SRGB_BYTE` and `SHADE_LEVEL_LIGHT`, which hold what this card's own `powf` gives, printed exactly, so that the pictures stayed the same to the bit. A table from another card's `powf` would round differently, by as little.

## What is here, and what is not

Point sampling, on purpose: blocks are meant to look like their pixels. The mip is by distance alone, as the plan says, so a surface seen at a low angle stays sharper than it strictly should, and shimmers a little. The temporal pass in `cuda/src/post/` is what deals with that.

## The light on a surface

`shade_surface_light`, with `shade_sun_t` as `render_day_fill` worked it out for this moment:

    face brightness x (ambient x sky level + block colour x block level) + direct x how square it is to the sun x what the ray saw

Each kind of face keeps its one brightness: a top is 1, a bottom a half, the sides facing y 0.8 and those facing x 0.6. A level goes down by 0.8 a step from 15, which is exactly 1. At noon a top in the sun comes to about 1, so a pack's colours show as they were drawn.

**The stored light is the voxel's own, the one the ray stopped in**, its byte from the hit, sky in the high half. Air is not on the card, by `run/src/render/encode/`'s format. That settles the decision Stage 08.2 issue 3 left for here, in favour of keeping the format: **`run/src/world/light/` leaves in each voxel with a block the light its faces should show, the brightest of its neighbours in the air, before the section goes out.** A flood fill that leaves a solid voxel at 0 gives dark faces, with this code doing what it says. One voxel has one byte for all six of its faces, so a wall one block thick shows the brighter side's stored light on its dark side too, though the sun ray still knows better. `run/src/world/light/` does this: see "What a block holds" in its README. What the choice costs and what would be done otherwise is in this stage's plan file.

## The sun ray

One per surface that faces the light and has any sky light stored, from a hair off that surface in the voxel's own coordinates, so it is as exact far from the eye as near. It is `traverse_trace` again, with `resume` set so that it takes only what it enters, and a surface cannot meet itself. The column heights send it to the top of the world at once, once it is above everything, and see "What a ray skips" in `cuda/src/traverse/README.md`. What it meets goes by that block's texture at the mip the surface is seen at: a hole lets it on, a part-covered texel takes its share and lets it on, an opaque texel ends it, and water goes by how much of it there is: see Water. At `SHADE_SUN_LAYERS` surfaces it calls the sun hidden, which is what a thick wood looks like anyway. At night the same ray goes to the moon. Faces away from the light pay nothing.

Shadows have hard edges, one ray's worth, and it does not bounce. The temporal pass in `cuda/src/post/` smooths the stair steps of an edge, as it does any edge, and does not make it any softer than that.

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
- **An eye in the water,** with `post_settings_t.under_water`, gives a pixel the `t` of where the water stops or of a box in it, past the clear texels of a sea plant, and not of the surface it started in, for `cuda/src/post/`. Nothing else about the pixel changes.
- **The depth tint, `SHADE_FEATURE_WATER_TINT`,** is `shade_water_tint`: what passes falls by an exponential with depth, per channel with red first, and what it took comes back as a deep colour under the light on that water's top. That light has all the sun and no ray of its own: a shadow does not lie on what is in the water, only on its surface and bed. Past `SHADE_WATER_SEEN_BLOCKS` of it the ray stops, and what it no longer shows is under a twenty-fifth of itself in the channel that lasts longest, before what the surface's alpha takes.
- **A sun ray in water** takes `SHADE_WATER_ABSORB_SUN` per block, one number and not three, as it carries no colour, and nothing for the surface's alpha. So a bed in the shallows has most of the sun on it, and a deep one has only its stored sky light. With tint off the water is clear to the sun as well. A ray with tint on that goes past `SHADE_WATER_SEEN_BLOCKS` calls the sun hidden.

From inside a body of water (a hit that says `TRAVERSE_FACE_INSIDE`) there is no surface: only the tint, out to where the water ends, and what is behind. Going out of the water into air draws nothing at that surface. A level of water below the voxel's top is not here: `run/src/world/tick/` keeps levels on the host and none reaches the card, so all water is a whole cube. The cost of each setting is in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-3-water-glass.md`.

## AO

`SHADE_FEATURE_AO`: `visible` in `shade_surface_light_near`, from 1 with nothing near down towards 0 in a corner. It takes from the stored light, which comes from all around, and not from the sun, whose own ray knows what is in its way. **What a pixel has is about the surface its ray ends on, the one seen whole:** the bed under water or the frame of a pane of glass, and not water or ice over it. A reflection has none. A surface of a part-covered block has none.

- **A ray** goes `SHADE_AO_REACH` blocks, in a direction weighted by the cosine about the face's normal, so that the mean of what they see is how much of the sky's light reaches the face, which falls on it by the same cosine. What it meets counts by `shade_ao_near`: all, touching, and nothing at its reach, so nothing pops as it comes in reach. It stops by a cap on steps that always gets past its reach, and costs no more than that. Holes let it on, part-covered texels take their share, and water takes nothing: a lake's bed is not in a corner for having a lake on it.
- **Reduced rate.** One frame in four, by `shade_ao_turn`, one ray. **By groups of 8 by 4 pixels, and not one pixel in four,** which is a warp of this kernel: a warp with one ray in it takes as long as that ray for all its 32 threads, so one pixel in four spread evenly cost 2.4 ms where this costs 1.8 (`plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-4-ao-taa-godrays.md`).
- **Temporal reuse.** `shade_ao_pixel_t` holds the mean, and which plane and which way up it is about. `post_reproject` finds where the surface was in the last frame, and `shade_ao_same_surface` asks if that place holds the same plane: in blocks from the eye's cell, so it is as exact far from the origin as near it. A surface it finds goes on from its mean, as a running mean of up to `SHADE_AO_SAMPLES_MAX`. So a camera that stands still comes to the integral, and one that moves keeps all it knew. A block placed changes no plane, so what is next to it darkens over those samples, in about two seconds at 60 frames, and not at once.
- **A pixel on another surface** from what its own place held, as along the edge of a block that has moved a little or has a part-covered neighbour, goes on from the pixels around that place on this surface, with the weight of one sample and no ray. Only with none of those either, where something has just come into view, does it send `SHADE_AO_FRESH_RAYS` at once, by way of the list in step 5 above. With TAA on, this cut those rays by 50 times on real terrain, as jitter sends some pixels along the edge of each leaf to the surface behind it.
- **What is shown** is its mean with those of the 8 pixels around it on the same surface, as each pixel's own walk through its samples is grain, which the temporal pass cannot take away since it stays still.

`SHADE_AO_FRESH_RAYS` is also what a first frame sends. Nothing random is used: the two numbers of each sample are Roberts' R2 sequence in 32 bits, from a place each pixel's hash chooses, so one script gives one picture.

## God rays

`shade_shafts_frame`, for `SHADE_FEATURE_GOD_RAYS`, for every second pixel each way: `SHADE_SHAFT_STEPS` places along the ray up to the first surface drawn or `SHADE_SHAFT_REACH`, each asking `shade_sun_seen` as a surface does, holes in leaves and all. Each counts for the light its own stretch of air scatters, `shade_shaft_part`, which add up to exactly what the whole ray would for any cut. What they find is multiplied by `shade_shaft_phase`, Henyey-Greenstein's, so that they show looking towards the sun. Where along its stretch each place is differs by `shade_shaft_noise`, a little from each texel to the next, and from frame to frame when `SHADE_FEATURE_TAA` is on to take that away, and not otherwise, so that a still picture stays still. Air is not in a voxel: rays do not know of water or fog. The cost is in the stage's plan file.
