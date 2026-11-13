# tests/cuda/ao

**Purpose:** ctest `render_feature_ao`. Tests plan 008 phase 04's AO for pixels newly seen while the camera moves: the corner estimate,
the world-space AO cache (`run/include/gpu_bridge/shade/`, `cuda/src/shade/`), the section revisions it keys by (`cuda/src/world_buffers/`), and
the turn at the picture's centre that picks TAA's history read (`run/include/gpu_bridge/post/`, `run/src/render/frame/`).
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/cuda/sky/fixtures/rig.h` for a real run with a world holding only what a test puts there,
`run/src/render/`, and everything under it, and a card. No card fails this test, on purpose.

Compute only. It opens no display and no input device.

What it checks:

- **On paper, on the host, the same lines as the card:** the corner estimate is 1 in an open field, 0.5 / 0.75 / 1 across a floor beside
  one wall, and `SHADE_AO_CORNER_FLOOR` (0.25) in a two-wall corner; a cache entry packs and unpacks to the same fields; a tag that differs
  in the same slot, an entry 240 frames old or more, another face, and an entry never written are misses; a section's new revision, a new load,
  another square or face each give a new key; the newest entry wins over any samples, and of one frame the most samples; and a turn of three
  pixels' angle reads three pixels.
- **A strip that comes back hits the cache:** a room, 20 frames still, 10 turned 30 degrees left, then back. Of the pixels fresh on the
  frame back (those with exactly `SHADE_AO_FRESH_RAYS` samples without the first guess), at least 90 % take their AO from the cache, and
  the rest from the corner estimate: none sends a ray.
- **A block placed is not shown with old AO:** a floor looked at, then turned from; a stone put on it at a chunk's border; then back. No
  floor pixel beside it takes its AO from the cache, and their mean is under 0.94 (0.906 on paper, and 1 for the open floor the cache held), as the corner estimate sees it.
  A key holds the revisions of the sections within 4 blocks of the face's open cell, so the floor further from the block still hits.
- **A still camera is the same to the bit** after 200 frames, TAA on, with the first guess on and off.
- **The same turning frames give the same picture to the byte**, twice, with the first guess and TAA on: nothing a frame reads hangs on the
  order the card ran it in.
