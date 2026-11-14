# cuda/src/lod

**Purpose:** rays past level 0, the blocks: from the finest coarse grid out, each pixel's ray that went on past where level 0 stopped for it.
**Tier:** a CUDA area. It holds no memory. Blocks come from its caller, and are checked against what is asked of them.
**Owns:** nothing.
**Lifetime:** each call starts a kernel and returns. `traverse_wait` waits.
**Threads:** the `gpu` thread.
**Public functions** (`run/include/gpu_bridge/lod/index.h`, for C and CUDA):

- `lod_frame`: one `lod_hit_t` per pixel of a camera, into what `cuda/src/shade/` reads.
- `lod_rays`: the same trace over a list of rays, with no bands, for a test.
- With `lod_scene_t.borrowed_turn`, `lod_frame` keeps the turn cache (plan 008 phase 06, below): its state on the host, its texels on the card.
- `lod_frame_counted` (plan 009 phase 01): `lod_frame` with every traversal's steps in a counts block, by second copies of its two kernels: far rays (`TRAVERSE_COUNT_FAR`) and their sun rays (`TRAVERSE_COUNT_FAR_SUN`) by level, in blocks along the ray, and the ask past level 0 (`TRAVERSE_COUNT_PAST`).

**May be called by:** `cuda/src/frame/`, and tests.
**Depends on:** `cuda/include/traverse/` for the ray loop and `traverse_view_t`, and `cuda/include/shade/` for `shade_texture_t` and what shade's own ray meets: `shade_surface_read`, `shade_water_through`, `shade_own_kind_behind`. Its level-to-level loop (`lod_trace`, `lod_sun_reaches`, `lod_sun_onwards`, the hand-over `lod_level_from`, `lod_view_from`) is in `cuda/include/lod/index.hu`, as shade's sun ray walks it too (plan 009 phase 06).

## How a ray goes

A level is a world of the format `run/src/render/encode/` writes, with a voxel `1 << shift` blocks each way. So a ray in a level is the world's ray scaled down: the same direction, and an eye at `eye / size`, worked out from the whole block and the fraction so that it stays as exact as ever, and `traverse_trace` does the rest. Its `t` is in that level's voxels, and times `size` is blocks.

`traverse_trace`'s window is only a place to clip and a corner to count slots from, so a level's view is given the square rays are let into, `shown_*`, and not the table's. A level only shows what is built.

A ray starts at the finest level that has anything to show and goes on, each from where it left the last one. Each is traced with `resume` set, which takes only what it enters from there on. A coarser voxel can hold what a finer one said was air, and a ray never comes to in the middle of a block.

## Bands: where each level starts (plan 009 phase 04)

Level k, of voxels of `s = 2^k` blocks, moves a surface by at most `s / 2` blocks, which seen from `d` blocks is `s / (2 d theta)` pixels, theta being one pixel's angle. So it is used only past `d_k = s / (2 E theta)`, E being the tuning file's `pixel_error`, and never nearer than its `near_guarantee`, Ahmed's 16 blocks. `traverse_world_t` carries `band_base = 1 / (2 E theta)`, worked out each frame by `run/src/render/frame/` from the camera's field of view and the picture's rows, so `d_k = band_base 2^k` and everything follows the field of view, the render scale and the resolution by itself. With no far field it is 0, and level 0 goes to its edge.

A pixel's band edges all move by one factor within `band_dither` of 1 (8 %), a hash of the pixel and, with TAA on, of the frame (`traverse_band_factor`): so a band edge is a spread of pixels TAA blends, not a line, as Godot's dithered visibility ranges and Unreal's dithered LOD transitions are. With TAA off the hash is the pixel's alone, and a picture comes out the same each time. The dither never takes `d_1` under the near guarantee.

- **Primary rays** (`cuda/src/traverse/`) trace level 0 with `t_end = d_1`. A ray that gets there with nothing met says `TRAVERSE_FACE_PAST`: a miss whose `t` is `t_end`. One that enters a column level 0 does not hold (not loaded yet, or no room in the pool) says the same, with `t` where it came in, so the far field shows in level 0's holes as it did.
- **This area** starts each far ray where level 0 stopped for it: that `t`, or `d_1`, or where the ray leaves level 0's square, whichever is nearest (`lod_frame_kernel_body`). Each level is walked to where its band ends, the next shown level's `d` by the same factor, or to its own square's edge, whichever is nearer; and the next takes over from `LOD_HANDOVER_BACK` before that. A level is never entered before its band, so there is nothing to pass by: the up to 32 re-traversals a level that passing by the old ring's columns could cost are gone.
- **Shade's own ray** past glass, leaves and water (`shade_kernel`) uses the same `t_end`, and on a miss reads this area's hit as before. Shadow rays go by the light's cone instead (see The sun); AO and reflection rays trace level 0 with no end.
- Levels nest (plan 009 phase 03 builds level 1 and 2 from the blocks where they are loaded), so at a hand-over the coarse surface is within half the coarser voxel of the fine one; `render_feature_lod_bands` holds every pixel by a band edge to that.

The near guarantee follows by construction: within 16 blocks a primary ray, and shade's ray after it, are traced in level 0 exactly as with no bands, and `render_feature_lod_bands` checks every such pixel's hit and colour to the bit against level 0 everywhere.

The far field is asked only about rays that can still get there: what got past level 0 in primary rays, and what shade's own ray, going on past all it can see through, takes past it, so that a window or a lake looks out on the hills. It is under the primary pass's clock, as the rest of those same rays. `cuda/src/shade/` looks at `lod_hit_t` only where a ray of its own gets past level 0, so water and glass work out what they let through, and fog comes from `t` as for anything else.

## Only where shade will look

For a pixel whose first surface is not a miss, the far field asks in turn, so that a far ray is sent only where shade will look at it (plan 005 phase 8 measured far rays for every such pixel at 4 to 29 ms a frame):

1. Does shade go on past it? The texel shade will read there, by `shade_surface_read` from `cuda/include/shade/`, the same lines: one that is opaque, and is not water, ends shade's ray. Most leaves and grass end here.
2. Does anything opaque, or water, stand behind it before level 0 ends for that pixel? One `traverse_trace_stopping` from that surface, with the pixel's `t_end`, which goes through every block but the stops: the blocks no face of which has anything to see through, and water. The renderer works them out once from the same texture table and sends them at the end of the face table (`frame_lod_stops` in `run/src/render/frame/`, `lod_light_t.stops_after_faces`). Almost every ray past leaves, glass or a plant stops here on near ground, and gets nothing. One that gets out gets its far ray.
3. If water stopped it, does shade's own ray get through? `lod_goes_out` follows it as `shade_kernel` does, with the same `t_end`, surface by surface, a body of water walked by `shade_water_through` with `shade_water_kernel`'s cursor, to `SHADE_WATER_SEEN_BLOCKS` with tint on, up to `SHADE_ALPHA_LAYERS`. A sea seen from above is cut short there, and shade never looks past it. With no stops, every pixel shade sees past asks this. The walk itself is a walk of level 0's cells and is not cut at `t_end`; the trace after it, which starts past `t_end` if the water went that far, says PAST at once.

Steps 1 and 2 are `lod_past_kernel`, and 3 and the far rays are `lod_frame_kernel`. Shade's ray can stop sooner than these say, never later, so every far pixel is the same as before, and a test holds far hills to showing through glass and water.

What was tried and dropped: a second trace on through water to its bed, so that a ray down into a lake would stop there. It cost more than step 3, as a ray towards the horizon crosses hundreds of voxels of water one at a time. And step 3 for every pixel past leaves, in place of step 2: it costs what shade's own walk through a canopy costs, about 7 ms on `jungle/orbit`.

## The sun (plan 009 phase 06)

A shadow ray, sun or moon, needs only the detail its light can show. The sun and the full moon are about 0.53 degrees across (`light_angular_diameter`), so an occluder L blocks from a surface blurs a real shadow's edge over L tan(0.53 degrees) = 0.0093 L blocks. Level k moves a surface by at most half its voxel, so past L_k = (2^k / 2) / (0.0093 x `shadow_cone_scale`) it changes nothing a real light would not blur away: **108 blocks for level 1, 216 for level 2, 432 for level 3** (`shade_shadow_cone_base`, 54, times 2^k). Shadows stay one ray, hard-edged; the cone only says which detail they could never show.

So both paths hand a shadow ray from level to level by one function, `lod_level_from` in `cuda/include/lod/index.hu`, the one a pixel's ray uses with its bands, given the cone (`lod_reach_t`: base 54, factor 1, no floor, measured from the surface) in place of the bands:

- **A far surface's ray** (`lod_sun_reaches`, here) starts in the level the surface is in, from just outside its face: a surface in level k is within half a voxel of the truth itself, and its shadow cannot be finer. It goes on in that level to the next level's L, and so on, each level also to its square's edge.
- **A near surface's ray** (`shade_sun_seen` in `cuda/src/shade/`) starts in level 0, through everything see-through as before, to L_1, or level 0's edge, or a column level 0 does not hold, whichever comes first; `lod_sun_onwards` takes it on from there through the levels to the render distance. So near ground gets a far hill's shadow, which it did not before this phase.

Out in the levels anything a voxel holds stops a shadow ray: a far surface is its mean colour, and water is water. A level's column that is not built yet (one put off as covered, nearer than its own band, until the rest are asked for) lets a shadow ray through to the next level, as it does a pixel's ray: in the seconds after a far field opens, a near surface can miss a shadow cast from more than 108 blocks off, until that column is built.

## What is simple about it

A far surface is its face's mean colour and has no texture: a voxel is many blocks, and `mean_rgba` is what `compile/*/texture_vector` made for this. Water out there is what near water comes to when deep: the sky reflected for how square a ray meets it, the texture's mean as far as that covers, and what deep water gives back, with no ray to its bottom. It has no sides, as a side is only where a level rounds the surface, and a ray that came in through near water sees only more water. A far surface takes no AO and no reflections, and has no shape but a cube.

## The turn cache (plan 008 phase 06)

Turning in place sends the same far rays over and over: the far field is past d_1 (193 blocks at 50 % of 1080 rows, 385 at 100 %), so from an eye that has not moved a direction's answer does not change. `lod_scene_t.borrowed_turn` is a cube round the eye, `lod_turn_cache_t`, of 6 faces of `side` texels each way (1024 in the game, `compile/config/render/lod/index.yaml`: about a pixel of 1080 rows at a face's middle, 96 MiB), each a `lod_turn_texel_t`: the `lod_hit_t` a far ray found, the `t` it started from, and the generation it was written under. `lod_turn_texel_of` and `lod_turn_texel_direction` (`run/include/gpu_bridge/lod/`) number them, the same on both sides.

- **Every far ray traced writes its texel**, whole, in 16 bytes. Two pixels of a frame may write one; their answers are a texel apart, and either does.
- **On a turn a pixel reads its texel instead of tracing**, if the texel is of this generation and its ray started within `2 band_dither + 2 %` of where the pixel's starts: so the dither of a pixel's d_1 is allowed, and a pixel whose ray leaves level 0 elsewhere (a hole in it, a change of render scale that moves d_1, shade's ray through water) traces. It takes the texel's sun too.
- **A frame is a turn** when the camera's middle moved by more than `LOD_TURN_PIXELS` (2) pixels since the last frame; TAA's jitter moves it by under one. A frame that is not a turn traces every far ray, as before, and writes the texels it owns (below): so a still camera's pictures are the same to the bit with the cache or without, and the first frame of a turn already reads what the still ones wrote. (The plan had every frame read; this keeps the still picture exact, as plan 008 phase 04's first guess does.)
- **A generation** begins when the eye has moved `LOD_TURN_EYE_BLOCKS` (0.25) from where the last began, the far field's `revision` changed (`run/src/render/lod/`: any level's square shown at once, columns sent at most once every `turn_revise_frames`), the sun moved by a quarter of a degree, or the sun's rays came or went. All texels of an older one are invalid at once, with no pass to clear them; the block is zeroed once on the card, on the first frame. Walking passes 0.25 blocks every few frames and traces nearly as before; standing and turning traces almost nothing twice.
- **The same picture whatever order the pixels run in** (plan 009 phase 08's follow-up). A texel is written once in a generation, by the one pixel its middle falls in on that frame's camera (`lod_turn_owns`, worked out alike by every pixel), and only while it holds no answer of this generation; and it is read only on a frame after the one that wrote it, by a 7-bit stamp of frames kept in the top of its hit's sun byte (`LOD_TURN_STAMP_MASK`; the sun itself is the top bit, as a `lod_hit_t`'s sun is only ever seen or hidden). Before, every pixel of a texel wrote it, the last one in won, and a pixel could read what another wrote in the same frame: so a settled row that turns, `bench_world`'s orbits, drew a different picture each run.
- **The tally**, two counters after the texels, counts far rays traced and read, one atomic add a warp. `render_lod_turn_tally` reads it for a test or a bench.

What it does not do: reproject with parallax as the eye moves, or cache level 0's rays, which change with the slightest movement.
