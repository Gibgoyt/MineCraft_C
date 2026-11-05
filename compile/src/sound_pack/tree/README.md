# compile/src/sound_pack/tree

**Purpose:** reads and checks a whole `sounds/` tree, and has every sound's samples ready, from its recording or its recipe.
**Tier:** Arena. Allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** what it gives back lasts as long as the arena.
**Threads:** the tool's one thread.
**Public functions** (`compile/include/sound_pack/tree/index.h`):

- `sound_tree_load`: the whole tree, in order, or false with every problem printed.
- `sound_tree_parse_name`: whether a directory's name is a sound's, and which.

**May be called by:** `sound_pack` and its parts.
**Depends on:** `compile/src/yaml/`, `compile/src/texture_pack/support/`, `sound_pack/mcsnd`, `sound_pack/synth`, `run/include/types/sound_blob/`.

## What it refuses

Every problem is printed, and reading goes on, so one build shows them all.

- A field it does not know, a missing field, and a `name` or `family` that is not its directory's.
- A directory in `sounds/` that is no family, or a directory in a family that is not `<hit|break|place>_<1 to 8>`.
- A family with no sound of some kind, and variants with a gap.
- A recipe out of range: `compile/src/sound_pack/synth/` says what's allowed.
- A sound made from a recipe with a recording beside it, and a recording with a recipe in its file.
- A recording with no `origin`, `file` or `url`, a licence other than `CC0-1.0`, or an origin not credited in `sounds/ATTRIBUTION.md`.
- A missing `README.md` on a family or a sound, and no `sounds/ATTRIBUTION.md`.
