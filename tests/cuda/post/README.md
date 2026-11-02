# tests/cuda/post

**Purpose:** ctest `render_feature_post`. Tests AO and its history in `cuda/src/shade/`, the temporal pass and god rays in `cuda/src/post/`, on the real card, through the real encoder and upload queue, with the settings in `run/src/render/frame/` and the flags in `run/src/main/`.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/png`, `tests/cuda/sky/fixtures/rig.h` for a real run with a world holding only what a test puts there, `run/src/game/`, and everything under it, and a card. No card fails this test, on purpose: `gpu.enabled` is true, and a skip would read as a pass.

Compute only. It opens no display and no input device, and runs the built program only with `--headless`.

What it checks:

- **By hand:** the jitter's points, a camera moved by one, the way back to the last frame's picture, what TAA mixes coming back as it was, the filter's weights, what an AO ray counts for, whose turn it is, directions off a face, whose history is whose, and what god rays add up to, on numbers worked out on paper.
- **AO's rate:** read back from the card: a first frame sends four rays per pixel. Each frame after, exactly the pixels whose turn it is have one sample more and the rest are the same to the bit. After a small step nearly all keep their history, and after a jump next to none do.
- **AO against the integral:** with the light plain, what each pixel's history says, against that point's integral over the same directions in `float64_t`, and what the picture shows, as a ratio to the picture with AO off, by tiles. A wide floor with nothing near does not change by a byte. A block put next to a place darkens it over the frames the history lasts, and not at once.
- **TAA against all of each pixel:** the converged picture is nearer to the same camera at four times the size each way, averaged down, than one frame alone is. It settles. Plain sky keeps its colour.
- **TAA with things moving:** after a walk, a block going away, and a jump, the picture is near that place's own, and with the setting off it owes nothing to what went before.
- **Byte for byte:** one set of frames with every setting on gives one picture, in two runs.
- **God rays against an exact walk:** in open air each texel is exactly its share. In a room with a window, each texel against a walk in `float64_t` from the same six places. The picture only ever gets brighter.
- **The settings:** each off leaves the rest as they were, and a kernel told to use a block it was not given says so and starts nothing.
- **Golden pictures:** `fixtures/*.png`, at most 2 out of 255 apart on any channel. Ahmed's card and driver made them. To make them again, after looking at them:

      MC_GOLDEN_WRITE=1 tools/test-or-build.sh ctest --preset debug -R '^render_feature_post$'

- **The cost:** each setting on real terrain, at 1080p, on the card's own clock, printed as `cost, ...` lines. Nothing here is a limit: the numbers are recorded in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-4-ao-taa-godrays.md`.
- **The built program:** the three flags are taken with a world, and refused without one.
