# sounds

The sounds the game plays when you mine and place blocks, committed here so that a build needs no pack and no network. Licences and credits are in `ATTRIBUTION.md`.

```
sounds/
├── ATTRIBUTION.md             every source of a recording, and its licence
├── stone/hit_1/
│   ├── index.yaml             its recipe, or where its recording came from
│   ├── index.mcsnd            a recording only: see compile/src/sound_pack/mcsnd/README.md
│   └── README.md              its credit, and what was changed
├── wood/ dirt/ gravel/ sand/ grass/ leaves/ snow/ glass/ metal/
```

Each family has three kinds, each with a few variants picked at random, so repeated hits don't sound identical:

| Directory | When it plays |
|---|---|
| `hit_<n>` | Each few ticks while mining |
| `break_<n>` | The block breaks: a fuller crunch |
| `place_<n>` | The block is placed: a short thud |

A block picks its family with `sound:` in `config/blocks/<block>/index.yaml`. `metal` has sounds, though no block uses it yet.

## Where a sound comes from

`source.kind` in `index.yaml` says, and there are two:

- **`recorded`:** the samples are committed in `index.mcsnd`. The licence must be `CC0-1.0`, and `ATTRIBUTION.md` must credit its origin. **No Minecraft sounds**, ever. All 90 are this today, and were chosen by name, licence and measurement. Nobody has heard them yet.
- **`synth`:** `compile/src/sound_pack/synth/` makes it from a recipe in that file, while building, for a material with no recording. It owes no credit.

The build turns this tree into one blob, `build/<preset>/generated/sounds/index.mcsb`, and `asm/src/embed_assets/` links it in. The build refuses a tree in which any family lacks any kind, so no family can be silent.

To check this tree:

    tools/test-or-build.sh cmake --build --preset debug --target mc_tool_sound_pack
    build/debug/bin/tools/sound_pack verify --sounds sounds

## To change a sound

**To go back to a recipe:** delete its `index.mcsnd`, and write a recipe in its `index.yaml`. `compile/src/sound_pack/synth/README.md` says what each field does, and commit `b8b314a` holds one for every sound, in `sounds/<family>/<name>/index.yaml`.

**To use a recording:** download it to its own directory under `/tmp/mc_c_assets/`, read its licence, add the source to `ATTRIBUTION.md`, convert it once, and import it. `ffmpeg` is only used by hand here, and the build doesn't need it:

    ffmpeg -i <recording> -af "highpass=f=50" -f s16le -ac 1 -ar 48000 /tmp/mc_c_assets/<pack>/hit_1.raw
    build/debug/bin/tools/sound_pack import --sounds sounds --raw /tmp/mc_c_assets/<pack>/hit_1.raw --family stone --name hit_1 --origin "<as credited>" --file "<file in the pack>" --url "<where it is>" --licence CC0-1.0 --changes "<what you did beyond the format>"
    build/debug/bin/tools/sound_pack verify --sounds sounds

**What the 90 had done to them:** a high pass at 50 Hz; a low pass or a change of rate on some, to make a variant or dull a bright one; cut from just before the attack to where it dies away, to at most 170 ms for a hit, 700 for a break and 280 for a place; a short ramp in and a curve out to an exact zero, so a voice has no click; and gain to about -22, -18 and -20 dBFS over the loud part, with peaks kept to 62%, 88% and 72%. So families come out about as loud as each other, whatever level they were recorded at.
