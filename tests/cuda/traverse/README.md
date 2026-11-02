# tests/cuda/traverse

**Purpose:** ctest `cuda_traverse`. Tests `cuda/src/traverse/` on the real card against an exact CPU reference.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test: never above 1 GiB in a pool, so under 4 GiB in all, because the desktop shares this card.
**Lifetime:** One run.
**Threads:** main, and short-lived threads it joins, to build worlds and run the reference.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/include/gpu_bridge/`, `cuda/src/traverse/`, `cuda/src/device_memory/`, and a card. No card fails this test, on purpose.

Compute only. It opens no display, and starts no program.

What it checks:

- **By hand:** each face and its distance, a slanted ray, a value from every kind of node, a window below zero with a table that wraps, an eye outside the window and one inside a voxel, misses, and the same answer to the bit 16 million blocks out.
- **Against the reference:** an exact voxel-by-voxel Amanatides and Woo trace in `float64_t` on a dense array, independent of the tree. Random rays through random worlds holding every kind of node, and whole frames on Stage 01.3's terrain and lattice worlds and seven camera paths. Every disagreement is classified, and any that rounding cannot explain fails the test, as in Stage 01.3 issue 11. There is no allowed rate.
- **The coarse pass:** no pixel may hit anything before its tile's distance, on random cameras from 1x1 to 320x180 and very narrow to very wide, every pixel. A pass that always said 0 would pass that, so it must also skip a real share of tiles, and on a flight over terrain it must save steps.
- **That the checks can fail:** a reference a hundredth of a block off must be caught, a forged coarse distance must be caught, and changing a voxel on the card must change the answer.
- **The fallback sizes:** Stage 01.3's lower internal sizes are checked against the reference as well.

`fixtures/world.h` is the world and that encoder, and `fixtures/stress_world.h` is Stage 01.3's worlds and paths.

Its table and pool come from its own encoder, written from the dense array, and not from `run/src/render/encode/`: the test says what the format means. `tests/run/render/frame/` traces what the real encoder wrote.

## Timing

Numbers on a card the desktop shares prove nothing in a pass or fail, so timing runs on request, prints, and compares nothing:

    MC_TRAVERSE_MEASURE=1 TEST_FILTER=measure timeout 600 build/release/bin/tests/cuda_traverse
