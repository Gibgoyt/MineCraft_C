# compile/src/sound_pack

**Purpose:** turns `sounds/` into the sound blob the game embeds, making each sound from its recording or its recipe, and refuses a tree in which any family would be silent.
**Tier:** Owner (depth 1 of `compile/src`).
**Owns:** one mapped process arena. Its parts borrow it.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is a command line, and `compile/include/sound_pack/index.h` holds only its exit statuses:

- `sound_pack blob --sounds sounds --out <generated>/sounds/index.mcsb`: what the build runs.
- `sound_pack verify --sounds sounds`: the same checks, with a one-line count.
- `sound_pack import --sounds sounds --raw <file> --family <family> --name <hit_1> --origin <text> --file <text> --url <text> --licence CC0-1.0 --changes <text>`: one recording into the tree, run by hand and never by the build.

Exit 0, or 1 with every problem on stderr, or 2 on a bad command line.

**May be called by:** `cmake/generated.cmake`, a person, and ctest `compile_sound_pack`.
**Depends on:** its parts `mcsnd`, `synth`, `tree`, `blob` and `import`, and through them `compile/src/yaml/`, `compile/src/texture_pack/support/` and `run/include/types/sound_blob/`.

## Parts

| Part | Job |
|---|---|
| `mcsnd` | The format of one recording |
| `synth` | Makes a sound from a recipe, while building |
| `tree` | Reads and checks `sounds/` |
| `blob` | Writes the blob the game embeds |
| `import` | One recording into `sounds/` |

`sounds/README.md` says how to change a sound.
