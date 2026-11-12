# compile/config

The numbers that tune how fast and how finely the game draws, in YAML, read at **build time**. Ahmed, 2026-11-12: "config
settings should be read at compile time from a yaml file so that we can very very easily change things up! and fine tune
what we need to!!!"

`compile/src/tuning/` turns each `<path>/index.yaml` here into `build/<preset>/generated/tuning/<path>/index.h` (and
`index.hu` for CUDA-only code), a header of `MC_TUNE_<PATH>_<KEY>` defines. Nothing here is read at run time: there is no
settings key, environment variable or file the game opens for these.

| Directory | What it tunes |
|---|---|
| `render/` | The renderer: see `render/README.md` |

## Changing a number

    edit compile/config/render/shade/index.yaml          # say ao_turns: value: 4 -> 2
    tools/test-or-build.sh cmake --build --preset release
    ./build/release/bin/minecraft_c --report | grep -i tuning

`--report` prints `tuning hash 0x...` and every value, and `bench_world` records the hash in its history's `tuning`
column, so every measured number says which tuning it ran with. Only what includes the changed file's header rebuilds.
A value outside its `min`..`max` stops the build at its line; widen the range in the same file if you mean it.

## A key

```yaml
ao_turns:
  value: 4
  type: u32
  unit: count
  min: 1
  max: 4
  why: "A pixel with history sends its one AO ray on one frame of this many, in turn with the three next to it."
```

| Field | Rule |
|---|---|
| key | Lower snake case, unique in its file. Its define must be unique across every file |
| `value` | Of `type`, within `min`..`max` |
| `type` | `u32`, `i32`, `f32` or `bool` |
| `unit` | One of: `blocks`, `pixels`, `chunks`, `voxels`, `levels`, `bricks`, `frames`, `count`, `steps`, `ratio`, `degrees`, `ms`, `bytes`, `kib`, `mib`; and `bool` for a `bool` |
| `min`, `max` | Of the same type; left out for a `bool` |
| `why` | Not empty: what it does, and who set it if it was a decision |

An `f32` is written with a point and a digit each side, `16.0` and not `16` or `1.6e1`, because the header carries the
very text as its literal: `0.1` in the YAML is `F32(0.1)` in the code, the float the code always had. A `u32` comes out
as `4u`, a `bool` as `1` or `0`.

## What counts as a tuning number

**A number goes here if changing it changes speed or look, and no data format and no struct.** A new number of that kind
written into code instead is a review failure (`plans/CODING_STANDARD.md` section 6).

It stays in code, beside its `static_assert`s, if it shapes a format or a struct: the tile size, brick and section sizes,
`WORLD_BUFFERS_*`, `LOD_LEVELS_MAX`, array sizes in structs, anything saved to disk or sent to the card as a layout.

Where a number in the code depends on a tuned one, such as a field that must hold it or a step cap that must cover its
reach, the code keeps a named constant defined from the `MC_TUNE_*` one, with a `static_assert`, so a YAML change that
would break it fails the build at that line. The YAML's `min` and `max` say the same in words.

Where a number goes, in short:

- facts about the machine: `config/machine.yaml`;
- what the game is: `config/<kind>/`;
- how fast or how fine it draws: `compile/config/`.
