# cuda/src/entity_rays

**Purpose:** mobs' and items' boxes against a frame's rays, from each tile's list, and the outline of what the player aims at, on the finished picture.
**Tier:** a CUDA area. It holds no memory. Device blocks come from its caller, and are checked against what is asked of them.
**Owns:** nothing.
**Lifetime:** each call starts a kernel and returns.
**Threads:** the `gpu` thread, or the one thread of a run with no threads.
**Public functions** (`run/include/gpu_bridge/entity_rays/index.h`, for C and CUDA):

- `entity_rays_frame`: the kernel: per pixel, the nearest box of its tile's list. Nothing starts with no boxes.
- `entity_rays_overlay`: the outline, over a finished picture. Nothing starts with no box to outline.
- `entity_rays_box_hit`, `entity_rays_to_box`, `entity_rays_normal`, `entity_rays_edge`, `entity_rays_line_pixels`, `entity_rays_outline_pixels`, `entity_rays_hits_bytes`, `entity_rays_starts_offset`, `entity_rays_links_offset`, `entity_rays_textures_offset`: its arithmetic and its layout, inline on host and device, so that a test on the host runs the same lines.

**May be called by:** `cuda/src/frame/`, and a test.
**Depends on:** `gpu_bridge/*`, `cuda/include/traverse/`, the CUDA runtime.

## Why it is the way it is

A box is not a voxel, and nothing about it goes in the tree: a mob moves every tick, and the tree is sections on the card. So a frame has two kinds of thing a ray can meet, and asks about each in its own way. `traverse_primary` finds the world's nearest surface, this finds the nearest box, and `cuda/src/shade/` holds the two up against each other surface by surface, because it alone knows what a ray goes on through. A box behind glass or in water shows through them, and water's tint stops where the box is.

Each tile is a kernel block of `TRAVERSE_TILE_PIXELS`, as it is for the coarse pass, so an empty list costs a tile one load and no direction. With no boxes at all no kernel starts, and shade is given no hits: a world with nothing moving in it costs nothing here but the mark's few bytes.

A box is turned about z by its own yaw, for the mobs of Stage 15 and for an item going round. The test turns the ray into the box's axes and does slabs: two rotations and three divides. A ray from inside a box sees nothing of it, as there is no inside to draw.

**The outline draws after post and writes only the picture.** Not the history: a line in TAA's history would be jittered and would ghost as the aim moves. It looks at each pixel of the rectangle the box can touch, for how far the ray passes from each of the 12 edges, in pixels at that distance, in 3D. So the line is one width at any distance, and needs no projecting or clipping at the eye. It is hard-edged black, as Stage 12.2's was.

It is hidden by what the pixel shows at a nearer distance, by more than `ENTITY_RAYS_OUTLINE_DEPTH_BLOCKS` and 8 pixels' width, so that the surface it lies on and those flush with it do not hide it. `shade_pixel_t.t` is the *first* surface drawn, and that may be water, which a player aims through and which is drawn at distance 0 around an eye under it. So a pixel that seems hidden asks one more thing: whether a ray meets anything but water before the line. Glass does hide it, as glass can be aimed at.

Lines read depth from the frame's jittered rays and draw on the picture's unjittered pixels, so a line along the edge of something in front of it can move by a pixel from frame to frame.

**A box may lean,** by a pitch about its own left axis before its yaw, as a limb does: `entity_rays_to_box` and `entity_rays_normal` in the bridge header are all that knows. A body part also has a share of a skin, by `entity_rays_skin_uv`, which `cuda/src/shade/` samples instead of one texture over the whole of each face.
