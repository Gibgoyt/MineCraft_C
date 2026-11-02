# compile/src/texture_vector

**Purpose:** paints the textures no pack gives from recipes, and derives vector data for every texture.
**Tier:** Owner (depth 1 of `compile/src`).
**Owns:** one mapped process arena. Its parts borrow it.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is a command line, and `compile/include/texture_vector/index.h` holds its exit statuses and `tv_image_t`:

- `texture_vector recipes --textures textures`: writes `index.mctex` and `README.md` of each `source.kind: procedural` texture. Run by hand after editing a recipe; the outputs are committed.
- `texture_vector vectors --textures textures --materials textures/vector.yaml --out <directory>`: writes `<out>/<group>/<name>/index.mcvec` for every texture. A file already holding the right bytes is not touched.
- `texture_vector check --textures textures --materials textures/vector.yaml`: writes nothing. Each recipe still makes exactly what is committed, every `.mctex` is whole, everything in `needs` exists, and every material names a real texture and gives the light it asks for.

**May be called by:** a person editing recipes, ctest `compile_texture_vector`, and Stage 06.4's blob step for `vectors`.
**Depends on:** its parts below, `compile/src/yaml/`, and `compile/src/texture_pack/` parts `support`, `mctex` and `output`.

## Why index.c holds no computing

`cmake/targets.cmake` makes a tool of an `index.c` with a `main`, and the lint allows `mmap` only at depth 1 of `compile/src`, so this file has to be the tool. That leaves the computing in `analyze/`, as a library that the test and Stage 06.4 can link.

`texture_pack/support`'s `tp_fail` prints the running program's name, so that this tool's messages say `texture_vector:`.

## Parts

| Part | Job |
|---|---|
| `analyze` | Palette, mean colour, height, normal, emissive and alpha class of one image |
| `palette` | The palette and indices, exact or by median cut |
| `mcvec` | The `.mcvec` format |
| `materials` | `textures/vector.yaml`: what pixels cannot say, and what the game needs |
| `recipe` | Paints one recipe. Lists every operation |
