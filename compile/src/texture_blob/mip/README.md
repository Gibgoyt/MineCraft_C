# compile/src/texture_blob/mip

**Purpose:** makes one texture's mip chain.
**Tier:** Arena (depth 2), though it allocates nothing: the caller gives it the buffer.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** one.
**Public functions** (`compile/include/texture_blob/mip/index.h`):

- `tb_mip_chain_bytes`: the size of a chain, and where each level starts.
- `tb_mip_build`: writes the chain.

**May be called by:** `compile/src/texture_blob/` and its test.
**Depends on:** `run/include/types/texture_blob/`, and `compile/src/texture_vector/analyze/` for the sRGB conversions.

## The rules

- **Sizes.** Each level halves, rounding down, and stops at 1 by 1, so a 64 by 32 skin has 7 levels and 165 by 170 has 8. Each frame of an animation has its own chain.
- **Colour.** Each texel is the area average of exactly what it covers, in linear light, weighted by alpha, straight from level 0. That is the rule of `mean_rgba`, so a far texture stays as bright as a near one. Integer arithmetic only.
- **Holes.** A texel with nothing visible under it takes the texture's mean colour, so that a sample next to it cannot pull in whatever the pack left under its transparent pixels.
- **Alpha.** The class comes from `.mcvec`, so `textures/vector.yaml`'s overrides hold, and each level keeps that class's promise. See Stage 06.3 issue 5.
  - *Opaque:* 255 everywhere, including the few 252s of `furnace_front_lit`.
  - *Cutout:* level 0 is solid at 128 and above, so the pack's leaves at 5 become 0. Each lower level gets exactly level 0's share of solid texels, rounded, the highest averaged alphas first and ties in Bayer order, because rounding at 128 makes a tree thin out or fill in with distance, and a threshold alone could only take a level of one alpha, as glass makes, all or none. Only a texel with something under it can be solid. Something too sparse to fill one texel can vanish at the smallest levels.
  - *Translucent:* the plain average.
- **Level 0 keeps its colours,** even under a hole, so its RGB is byte for byte what `.mctex` holds.
