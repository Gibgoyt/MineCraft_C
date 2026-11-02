# 001-ahmed-bohemoth-minecraft-from-scratch — phase 9, stage 5: Entity boxes and outlines

**Status:** done, but for how it looks in play, which is Ahmed's to judge   (updated 2026-11-02)

## Goal

Entity boxes and per-tile lists, block outline and breaking overlay exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-renderer-features.md`.

**Owned paths** (edit only these, plus this file):

- `cuda/{src,include}/entity_rays/`
- `run/{src,include}/render/entities/`
- `tests/cuda/entity_rays/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement per-tile entity lists in `run/src/render/entities/index.c`
- [x] Intersect entity boxes in `cuda/src/entity_rays/index.cu`
- [x] Add the block outline and breaking overlay
- [x] Add a setting, a recorded cost and a golden image; register test `render_feature_entities`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^render_feature_entities$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Results

Run on 2026-11-02 in this worktree. Debug: the build and all 73 ctests pass, `run_render_frame` among them, with `render_feature_entities`, `standards_lint` and `clang_format`. That full run came before the last test-only additions (issue 4), after which the build, `render_feature_entities`, `standards_lint` and `clang_format` were run again and pass. Release: `render_feature_entities`, `render_feature_post`, `run_entity_drop` and `run_physics_raycast` pass, the first with 21 tests and 143474 checks. Sanitize: `render_feature_entities` and `run_physics_raycast` pass. `pgrep -a minecraft_c` prints nothing.

**What a frame does now:** the gpu thread takes the newest snapshot from the sim and makes each tile's list. After primary rays, `entity_rays_frame` finds each pixel's nearest box. Shade holds that against each surface a ray meets, near to far, so that a box shows through holes, glass and water. The crack is drawn in shade on the block being mined. After post, `entity_rays_overlay` draws the outline on the finished picture and not in the history. No frame pass was added: boxes count under primary's clock and the outline under post's.

**Checked against something else than itself:**

- Every box any ray of a tile can meet, at the most jitter a frame has, is in that tile's list: 49 boxes on the picture, 806 links for 470 that some ray needs.
- Random rays against turned boxes, against a march in tiny steps in `float64_t`: 1167 rays met a box, and all agree.
- The card's nearest box against the host's at every pixel, with and without jitter: about 4000 pixels on a box per frame, 0 different.
- A box's pixels against its texture and each face's brightness: 1227 pixels, 0 off, and no other pixel in the picture changed.
- A top under a roof against one in the open: 0.6128, and by the light 0.6128. With no light around it: 0.0352, and by the level 0.0352.
- Through water, a box's red over a bottom 2 blocks under it against one 42 under it: 0.0628 and 0.0628.
- The crack, each of its 10 frames: 1669 pixels on the block, 68 under the crack at frame 1 up to 1164 at frame 10, 0 off, 0 changed elsewhere.
- The outline on a cube on a floor: 9 of 12 edges drawn, the 3 at its hidden corner not. 252 pixels, all of them black. A wall hides all of it, and a box its share. Water does not, from above (75 pixels) or under it (217). Glass does.
- 8 frames of outline and 4 without, against 12 without, under TAA and AO: the same bytes. With the setting off, or a box behind a wall: the same bytes as bare.
- The built program, by a script that digs grass: `--no-entities` changes 0 pixels before anything falls and 536 with the dirt in the hole. `--no-outline` changes 189 before and 847 while mining. Two runs give the same bytes.

**Does the test catch a broken kernel?** Each of these was tried, and put back, with what caught it:

| Broken | Caught by |
|---|---|
| A box is always in front of the world | the picture, glass and water, and more |
| A box is drawn only where a ray misses | the picture, its own texture, and more |
| Water's tint runs on past a box | glass and water, after issue 4 |
| A box gets no sun ray | sun and shade, golden |
| The crack one frame on | the crack, golden |
| The crack on every block | the crack, golden |
| The crack takes no account of its alpha | the crack, golden |
| The outline has no depth test | the edges, water and glass, golden |
| The outline does not see through water | water and glass |
| The outline sees through anything | the edges, water and glass |
| The outline is not hidden by a box | the edges |
| The kernel keeps the last box, not the nearest | the card against the host, golden |
| Lists with no margin for jitter | the lists, the card against the host, the picture, golden |
| Lists take no account of yaw | the rectangle by hand, after issue 4 |
| Far boxes get the links first | full lists |
| From the eye without its fraction | the edges, golden, the program |
| `game` sends no items | the program |
| `game` sends no crack | the program |
| `game` sends no target | the program |

**Settings:** `SHADE_FEATURE_ENTITIES` and `SHADE_FEATURE_OUTLINE`, the second for both the outline and the crack, through `render_frame_features_set`. Both are in `SHADE_FEATURES_ALL`, since no frame of them reads the last. On the command line `--no-entities` and `--no-outline`, with `--test-world`.

**Golden pictures**, `tests/cuda/entity_rays/fixtures/`, each looked at, and one crop at 6 times the size: `play`, `plain`, `no_boxes`, `no_outline`. They hold grass, log, stone, dirt, leaves and coal as boxes, one behind a pane, and a dirt block at crack frame 7 with its outline. Their land is lit, as they use their own room.

**Cost**, on the card's clock, mean of 30 frames at 1920x1080, release build, real terrain at render distance 10, RTX 2080 Super, with AO and TAA on. "Lists" is the host's clock on the gpu thread before the frame, which the card's clock does not count.

| Scene | Primary ms | Shade ms | Post ms | Frame ms | Lists ms |
|---|---:|---:|---:|---:|---:|
| Both settings off | 0.325 | 5.543 | 0.496 | 7.728 | 0 |
| On, with nothing to draw | 0.325 | 5.375 | 0.492 | 7.561 | 0 |
| An outline | 0.324 | 5.434 | 0.545 | 7.677 | 0 |
| 16 items around the eye | 0.363 | 5.383 | 0.535 | 7.653 | 0.143 |
| 256 items around the eye | 0.366 | 5.501 | 0.534 | 7.771 | 0.232 |
| 4096 items within 80 blocks | 0.369 | 5.624 | 0.547 | 7.918 | 1.571 |
| 256 full-size boxes in one heap at arm's length | 1.039 | 5.248 | 0.536 | 8.189 | 16.556 |

So nothing to draw costs nothing that can be told from the noise between rows, which is about 0.2 ms here. An outline costs about 0.05 ms and boxes 0.04 ms in primary. See issue 3 for the last row. On the card's memory, 1080p takes 16 MiB of hits and about 2.5 MiB of lists out of `gpu.budget`'s 256 MiB for frames.

**Wiring outside the owned paths**, the least that makes a real run draw this:

- `run/include/gpu_bridge/entity_rays/index.h` and `run/src/gpu_bridge/entity_rays/README.md`: the C API, where the standard says it goes.
- `run/include/gpu_bridge/shade/index.h`: the two bits, `shade_box_light`, `shade_frame_entities`. `cuda/src/shade/index.cu`: `shade_box_colour`, `shade_crack_over`, and their calls in the kernel. `shade_frame` keeps its signature, with no entities.
- `run/include/gpu_bridge/frame/index.h`: `borrowed_entities` in `frame_args_t`, to 56 bytes. `cuda/src/frame/index.cu`: starts the two kernels.
- `run/include/render/frame/index.h`, `run/src/render/frame/index.c`: `borrowed_entities` in what was padding, so that `render_frame_t` stays 832 bytes, and the call before each draw.
- `run/include/render/index.h`, `run/src/render/index.c`: two blocks and a pointer in what was `_pad1`, so that `render_t` and `app_t` stay as they were.
- `run/src/game/index.c`: `game_entities_publish` at the end of each tick, and a line in `game_report`. `game_outline_draw` is gone: issue 2.
- `run/src/main/index.c`: the two flags.
- `tests/run/physics/raycast/index.c`: two tests about the outline in `game`: issue 2.
- READMEs of those, and `plans/REPO_STRUCTURE.md`.

## Commits

Appended as they land:

## Issues found

### 1. A line in front of a box was drawn anyway

- **Expected:** a box in front of the outline hides it.
- **What actually happened:** `the_outline_is_on_the_edges_that_can_be_seen_and_nowhere_else`: `a line with nothing in front 266, with a box in front 266`. A pixel that seemed hidden asks whether only water is in the way, by a ray through the world. Boxes are not in the world, so that ray said yes.
- **What was done:** the overlay kernel reads the entity hits too, and a nearer box hides the line before that ray is asked.

### 2. The outline moved from the CPU to the card, and what Stage 12.2 left behind it

- **Expected:** Stage 12.2 issue 1 asks that `game` hand this stage its `player_target_t` and stop calling `player_camera_outline_draw`, and that whoever merges check that only one outline is drawn.
- **What actually happened:** done, and `grep -n outline run/src/game/index.c` finds no call of it. Two tests in `tests/run/physics/raycast/index.c` compared what `main` showed against the picture under it, and those are now the same. `on_one_thread_...` now draws the frame with the setting off and compares. `on_real_threads_...` is renamed: it checks that what is shown is the picture, byte for byte, and that main's newest aim has its lines in more than half of those asked. It can no longer ask for every edge, as the picture has the aim the gpu thread last had. Stage 12.2 issue 3's mutation, main's camera over the picture's, no longer applies, as main draws nothing.
- **Left:** `player_camera_outline_draw` is now called by that test alone, and the sim's hundred short casts per tick for `edge_visible` are read by no picture. Both are outside this stage's paths, and work. Whoever owns `run/src/player/camera/` should take them out, with the tests of them, which saves those casts. `picture_cameras` and `shown_camera` are read by nothing but that test as well.

### 3. A great heap of large boxes at arm's length costs 16.6 ms on the gpu thread

- **Expected:** lists to cost little.
- **What actually happened:** the last row of the cost table: 256 boxes a whole block wide, all within arm's length, so most of the links, about 1 million, are filled and sent each frame. 4096 real items' sizes over 80 blocks took 1.571 ms.
- **What was done:** left, as that is not what a game makes: items are a quarter of a block and merge. Mobs in Stage 15 are large but few. If it ever matters, a coarser tile near the eye is the place to start. Most of even the small rows is `starts`, sent whole each frame with any box in view, 130 KiB at 1080p.

### 4. Tests of mine that were wrong or weak at first

- **Expected:** them to pass on right kernels and to fail on wrong ones.
- **What actually happened:**
    - Glass was expected to show a box at nine pixels in ten. This pack's glass has only holes and texels that hide all: 321 and 252 of 576. So each pixel is now checked as one or the other. What part-covered texels do is checked by water.
    - A box beside the eye listed on every tile at first, 11074 links for 470 needed. That was the code: anything with a corner behind the eye was the whole picture. It now cuts each edge at the plane of the eye, and the test stands as it was.
    - Water's tint running on past a box got through, at a tolerance of 0.03 on a red of 0.06: water covers most of what is under it. It is now 0.0005.
    - Lists ignoring yaw got through twice, as a box's near corners reach about as far: the list of a turned box is wider than its true shape. There is now a test of the rectangle by hand, along each axis.
    - `the_card_finds_...` wanted 5000 pixels on a box from a scene with about 4000, and full lists counted a far box that fits in what near ones left.
- **What was done:** as said in each.

### 5. Left as they are

- **Boxes cast no shadow, take no AO and are not in reflections.** A per-tile list is about rays from the eye, and nothing else has one. Sun rays from a box's surface do see the world, so it is shaded by the world, but the world is not shaded by it.
- **Items move at the tick's rate.** Unlike the eye, a box has no position between two ticks: 20 steps a second, which shows only on what is falling. A snapshot could carry the last position when Stage 15 wants it.
- **An item that is no block is a cube with its picture on each side,** and a transparent texel shows the picture's mean colour. It reads as a lump of coal, and it is not Minecraft's flat sprite.
- **A box is seen from the frame's eye only as a surface:** from inside one there is nothing to see of it.
- **The crack's look is the pack's:** a mottled texture in `blocks/crack`, over the colour in linear light. Whether it reads well in play is for Ahmed's eye.
- **The outline is hard-edged and is not antialiased,** as Stage 12.2's was, and is hidden by what stands more than 0.05 blocks and 8 pixels' width in front, chosen from these tests. A line along something in front of it can move by a pixel between frames under TAA, which was not watched in motion.
- **The harness refused some of my compound commands** as too complex to verify, and none of those ran: a heredoc with a mkdir, and a lint with `$PWD`. They were run as plain commands.
