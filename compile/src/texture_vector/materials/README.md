# compile/src/texture_vector/materials

**Purpose:** reads `textures/vector.yaml`: what pixels cannot say about a texture, and the list of every texture the game needs.
**Tier:** Arena (depth 2). It allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** a `tv_materials_t` lasts until the caller resets the arena.
**Threads:** one.
**Public functions** (`compile/include/texture_vector/materials/index.h`):

- `tv_materials_load`: reads and checks the file. Any complaint has a line number.
- `tv_materials_find`: what the table says of one texture.

**May be called by:** `compile/src/texture_vector/`, and its test.
**Depends on:** `texture_vector/analyze` for `tv_material_t`, `compile/src/yaml/`, and `compile/src/texture_pack/` parts `support` and `output`.

## The file

It is separate from each texture's `index.yaml` because a re-import replaces those of pack textures, and a torch must stay lit after one.

```yaml
materials:
  - { texture: blocks/lava_still, emissive: 15 }                               # every visible pixel gives light
  - { texture: blocks/torch_floor, emissive: 14, min_luma: 150, warm: true }   # only the flame, not the stick
  - { texture: blocks/water_still, alpha: translucent }                        # blends, whatever its pixels say
needs:
  blocks: [dirt, stone]
  mobs: [zombie]
```

`needs` is what `texture_vector check` holds the tree to: each name must exist, from the pack or from a recipe. A later stage that wants a new texture adds it there and gives it a source.

A row that asks for light and that no pixel passes is refused, by `check` and by `vectors`, and so by the build: otherwise the block would stay dark with this table saying it shines.
