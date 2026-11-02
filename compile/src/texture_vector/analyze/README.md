# compile/src/texture_vector/analyze

**Purpose:** derives one texture's vector data from its pixels: palette, mean colour, height, normal, emissive map, alpha class.
**Tier:** Arena (depth 2). It allocates only from the `tp_arena_t` it is given.
**Owns:** nothing. The arena's memory belongs to `compile/src/texture_vector/`.
**Lifetime:** a `tv_vector_t` lasts until the caller resets the arena.
**Threads:** any single thread. There is no shared state.
**Public functions** (`compile/include/texture_vector/analyze/index.h`):

- `tv_analyze`: everything below, for one image.
- `tv_luma`: Rec. 709 luma of sRGB bytes.
- `tv_linear_from_srgb`, `tv_srgb_from_linear`: the conversions the mean uses.

**May be called by:** `compile/src/texture_vector/`, its part `materials` (for `tv_material_t`), its test, and Stage 06.4's blob step.
**Depends on:** `texture_vector/palette`, `texture_vector/mcvec` (the class values), `compile/src/texture_pack/support/`.

## What each field means

All integer arithmetic, so the bytes are the same on any machine and at any optimization level.

| Field | How |
|---|---|
| Alpha class | Opaque if every pixel of every frame is 255. Cutout if every pixel is 0 or 255. Otherwise translucent. `textures/vector.yaml` can override it, for what must blend whatever its pixels say. |
| Mean colour | For LOD. Alpha-weighted mean in linear light, converted back to sRGB. Averaging sRGB bytes directly would make far terrain darker than near terrain. Mean alpha is the plain mean over all pixels. Fully transparent gives zero. |
| Height | Luma stretched to 0-255 across all frames, so that an animation does not pulse. Transparent is 0. A texture whose luma spans under 8 levels is flat at 128: stretching it would only amplify noise. |
| Normal | Sobel on luma (not the stretched height, so that flat-looking art stays flat-looking), at strength `TV_NORMAL_STRENGTH`, per frame. Edges wrap, because block faces tile. A transparent neighbour counts as level with the centre, so that the edge of a leaf is not a cliff. +x is right on the texture, +y is down it. Only x and y are stored; z is positive. |
| Emissive | The material's level for each visible pixel that passes `min_luma` and `warm`. Which textures give light is not in the pixels, so it comes from `textures/vector.yaml`. Class full if every visible pixel gives light, partial if some do. |
