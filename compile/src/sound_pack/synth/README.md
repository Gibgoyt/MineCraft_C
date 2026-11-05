# compile/src/sound_pack/synth

**Purpose:** makes a crunch from the recipe in a sound's `index.yaml`, while building, for a material with no recording. `sounds/` holds recordings for all of them today, so no committed sound uses this, and a test keeps it working.
**Tier:** Arena (depth 2), though it allocates nothing. The caller gives it the buffers.
**Owns:** nothing.
**Lifetime:** the call.
**Threads:** any. There is no shared state.
**Public functions** (`compile/include/sound_pack/synth/index.h`):

- `sound_synth_sample_count`: how many samples a recipe makes.
- `sound_synth_recipe_problem`: what is wrong with a recipe, or nullptr.
- `sound_synth_render`: fills the samples.

**May be called by:** `sound_pack` and its parts.
**Depends on:** nothing but `run/include/types/`.

## How a recipe becomes a sound

1. `grains` bursts of white noise, each dying away with time constant `grain_ms`. The first is at the very start, at full strength. The rest fall at random and crowd towards the start.
2. A band: one pole of high pass at `low_hz`, and two of low pass at `high_hz`.
3. A decaying sine at `tone_hz`, a second at `overtone_hz` at half strength, with time constant `tone_ms` and strength `tone_level`. That's the knock of wood, the thud of a placed block, and the ring of glass and metal.
4. Short ramps at both ends, reaching exactly zero, so a voice has no click.
5. Scaled so the loudest sample is `level` percent of full scale.

**The same bytes everywhere:** only `+ - * /` on `float64_t`, our own generator, and no libm, so one recipe makes one sound on any machine with IEEE arithmetic.

**What's measured and what isn't:** `compile_sound_pack` holds length, ends, peak, repeatability and how bright each family is. How it actually sounds is for a person to judge.
