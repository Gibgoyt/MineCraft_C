# cuda/src/shade

**Purpose:** one colour per pixel of a frame's hits: the texture of the face that was hit, at a mip chosen by distance, under that face's brightness, in linear light. A ray that lands on a texel of alpha 0 goes on to what is behind it.
**Tier:** a CUDA area. It holds no memory. Device blocks come from its caller, and are checked against what is asked of them.
**Owns:** nothing.
**Lifetime:** each call starts a kernel and returns.
**Threads:** the `gpu` thread, or the one thread of a run with no threads.
**Public functions** (`run/include/gpu_bridge/shade/index.h`, for C and CUDA):

- `shade_frame`: the kernel.
- `shade_pixels_bytes`: what its target needs.
- `shade_face_brightness`, `shade_face_uv`, `shade_texel_of`, `shade_mip_level`, `shade_level_dimension`, `shade_srgb_to_linear`, `shade_pixel_tangent`: its arithmetic, inline on host and device, so that a test on the host runs the same lines.

**May be called by:** `render`, `cuda/src/frame/`.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/status`, `gpu_bridge/traverse`, and `cuda/include/traverse/` for a pixel's direction and the ray loop.

## What is here, and what is not

Point sampling, on purpose: blocks are meant to look like their pixels. The mip is by distance alone, as the plan says, so a surface seen at a low angle stays sharper than it strictly should, and shimmers a little. Stage 09.4's temporal pass is what deals with that.

The sun is straight up: the top of a block is full brightness, a bottom half, the sides facing y 0.8 and those facing x 0.6. **The light stored per voxel is on the card and in every hit, and nothing reads it yet:** Stage 09.2 does, and Stage 08.2 issue 3 has a decision for it first.

A texel of alpha 0 is a hole. The kernel sends the ray on from there with `traverse_trace`, through the world it is given, and looks at the next surface, up to `SHADE_ALPHA_LAYERS` in all, and that one it draws: on a line of leaves that many holes long, it stops on the last, with that texture's mean colour, which is what far leaves look like anyway. Only the pixels that land on a hole pay for this, and they pay per hole. Holes behind a hole are in their own place: a leaf's far side is not drawn, since a ray takes only what it enters. The blob promises a cutout texture is 0 or 255 at every mip, so no threshold is needed, and a far tree stays about as see-through as a near one.

`shade_scene_t.features` is the setting, with `SHADE_FEATURE_ALPHA` off a hole shows the texture's mean colour, as it did before there were any. `SHADE_FEATURE_SHAPES` is carried here for `render_frame_draw`, which gives no shape table to a frame with it off. The cost of each is in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-1-non-cube-shapes.md`.

A part-covered texel still stops a ray, and shows that texture's mean colour through it, until Stage 09.3 lets rays go on for glass and water. Water is an opaque surface for now. Only frame 0 of an animated texture is drawn. A block with no texture, or a material past the table, draws magenta, which nothing else in the game is, so that the bug shows.
