# tests/cuda/entity_rays

**Purpose:** ctest `render_feature_entities`. Tests the boxes in `cuda/src/entity_rays/` and their share in `cuda/src/shade/`, and the lists and snapshots in `run/src/render/entities/`, on the real card, through the real encoder and upload queue, with the settings in `run/src/render/frame/` and the flags in `run/src/main/`.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/png`, `tests/cuda/sky/fixtures/rig.h` for a real run with a world holding only what a test puts there, `run/src/game/`, and everything under it, and a card. No card fails this test, on purpose: `gpu.enabled` is true, and a skip would read as a pass.

Compute only. It opens no display and no input device, and runs the built program only with `--headless`.

What it checks:

- **By hand:** a ray against a box, turned or leaning or not, a skin's unwrap, and a line, the crack's frames and the lists' layout, on numbers worked out on paper. A box's light against a block's on the same face.
- **Against a march:** random rays against random turned boxes, against a march in tiny steps in `float64_t`.
- **The lists:** every box any ray of a tile can meet, with the most jitter a frame has, is in that tile's list, and the lists are not much longer than they must be. What is behind the eye or past the fog takes nothing. A full snapshot counts what it leaves out, and full lists leave out what is far.
- **On the card:** the kernel's nearest box, read back at every pixel, against the host's, with and without jitter.
- **The picture:** a box's pixels against its texture and each face's brightness, and a mob's part, leaning or not, against its share of its skin. Every other pixel is as with no box, and with the setting off, or a wall in front, the whole picture is, to the byte. An item that is no block has its own texture. A box with no texture is the colour that says so. Under a roof its top lacks exactly the sun, and with no light around it, it has exactly the least. It shows through glass, and water's tint stops at it whatever is below.
- **The outline:** the edges a cube shows and no others, in the outline's colour and nothing else. A wall hides it, and so does a box in front. Water does not, from above or under it. Glass does. Twelve frames of TAA and AO with eight of them outlined are the same bytes as twelve with none.
- **The crack:** each of its ten frames on the block's pixels against its texture, and every other pixel the same. It spreads as mining goes on.
- **The machine:** no heap in a frame, and a kernel asked for what is too small says so and starts nothing.
- **Golden pictures:** `fixtures/*.png`, at most 2 out of 255 apart on any channel. Ahmed's card and driver made them. To make them again, after looking at them:

      MC_GOLDEN_WRITE=1 tools/test-or-build.sh ctest --preset debug -R '^render_feature_entities$'

- **The cost:** on real terrain, at 1080p, on the card's own clock and with the lists on the host's, printed as `cost, ...` lines. Nothing here is a limit: the numbers are recorded in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-5-entities-outline.md`.
- **The built program:** a script digs grass and looks: `--no-entities` changes nothing before anything falls and shows the dirt after, `--no-outline` shows the lines and, while mining, the crack, two runs give the same bytes, and the flags are refused without a world.
