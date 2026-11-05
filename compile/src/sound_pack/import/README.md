# compile/src/sound_pack/import

**Purpose:** commits one recording, already converted to raw PCM, as a sound in `sounds/`, replacing whatever that sound was.
**Tier:** Arena. Allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** the call.
**Threads:** the tool's one thread.
**Public functions** (`compile/include/sound_pack/import/index.h`):

- `sound_pack_import`: checks the family, name, licence and text, reads the samples, and writes `index.mcsnd`, `index.yaml` and `README.md`.

**May be called by:** `sound_pack`.
**Depends on:** `sound_pack/mcsnd`, `sound_pack/tree`, `compile/src/texture_pack/support/`.

A refused recording changes nothing. Only `CC0-1.0` is accepted. The tool doesn't read a pack's licence for you: a person does that, and names it here. Then `sound_pack verify` refuses the sound until `sounds/ATTRIBUTION.md` credits its origin.
