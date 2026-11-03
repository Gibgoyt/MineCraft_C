# run/src/texture

**Purpose:** the game's textures at run time: checks the blob linked into the program, and finds a texture, a mip level or its vector data by name or id.
**Tier:** Owner.
**Owns:** `owned_region`, one 2 MiB heap region, and `arena_tables` over it, which `registry` borrows. The pixels are not on the heap: they are part of the program's image.
**Lifetime:** Process.
**Threads:** `main` for init and deinit. Everything else only reads, allocates nothing, and may be called from any thread once init has returned.
**Public functions** (`run/include/texture/index.h`):

- `texture_init`, `texture_deinit`: start from the embedded blob, and stop. Deinit is safe on a zeroed `texture_t`.
- `texture_init_from`: the same over another blob, for tests.
- `texture_find`: name to id.
- `texture_count`: how many ids there are.
- `texture_info_get`: its name and its whole entry: size, levels, frames, classes and mean colour.
- `texture_mip_get`: one frame of one mip level.
- `texture_vector_get`: one frame's palette, indices, height, normal and emissive.
- `texture_pixels_get`: all pixels as one run of bytes, for a GPU upload.

**May be called by:** `run/src/main/`, which starts and stops it, `game`, `render` and `ui`, each with its parts.
**Depends on:** `heap`, `arena`, `log`, `simd/embed_assets`, `asm/src/embed_assets/`, and its parts below.

## Parts

| Part | Job |
|---|---|
| `blob` | Refuses a blob that is not exactly what the format allows |
| `registry` | Name to id |

## How a texture gets here

`cmake/generated.cmake` runs `compile/src/texture_vector` and `compile/src/texture_blob` over `textures/`, into `build/<preset>/generated/textures/index.mctb`. `asm/src/embed_assets/` `incbin`s that. `compile/src/texture_blob/mip/README.md` says how the mip chains are made, and `run/include/types/texture_blob/` holds the format.

Vector data has no mips. The plan's shading uses a mip chosen by distance for colour only, and a normal or a height averaged over a far texel means little.

`--headless` and the default run start this. `--report` and `--test-pattern` return before it, as they do for the rest of the runtime.
