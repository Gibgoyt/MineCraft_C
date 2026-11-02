# run/src/player/camera

**Purpose:** the player's look: the mouse turning the camera, what it aims at within reach, and that block's outline on the picture.
**Tier:** Arena by depth. It allocates nothing.
**Owns:** nothing.
**Lifetime:** Each call.
**Threads:** `player_camera_look` and `player_camera_outline_draw` on `main`. `player_camera_target` on whichever holds the map, normally `sim`. The rest on any.
**Public functions** (`run/include/player/camera/index.h`):

- `player_camera_look`: mouse counts to a yaw and pitch.
- `player_camera_direction`: a yaw and pitch to a unit vector.
- `player_camera_project`: a world position to a pixel, the inverse of a traverse ray.
- `player_camera_edge`: edge number 0 to 11 of a box.
- `player_camera_target`: the block aimed at, and which pieces of its outline can be seen.
- `player_camera_outline_draw`: those pieces, as lines over a finished picture.

**May be called by:** `run/src/player/`, `run/src/game/`, and tests.
**Depends on:** `physics/raycast`, `render/frame` for `render_camera_t`, `gpu_bridge/traverse` for the rays' numbers, `display/flip` for the buffer, libm.

## Why the outline is drawn on the CPU

The block outline was to be Stage 09.5's, and that stage has not landed: see Issue 1 of `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-12-stage-2-camera-targeting.md`. So it is drawn here, on the finished picture, with nothing asked of the card.

- **It sits on the blocks.** The lines are projected through the camera that drew the picture under them, with the same numbers as the rays, which is why `game` keeps a camera per picture. How late a picture is makes no difference.
- **It is hidden by what is in front of it.** There is no depth to read on this side, so `player_camera_target` asks instead: each edge is 8 pieces, and a piece is seen if a ray from the eye comes to nothing on the way to its middle. The block's own box hides its far edges the same way. That is 96 short casts a tick. It is correct to the piece and not to the pixel, and it is worked out from the sim's eye of that tick, not the picture's. A plant counts as its aiming box here, which is more than is drawn.
- **It is a tick old.** What is aimed at is worked out on the sim thread, which is also what mining will use, so a fast turn of the mouse can leave it up to 50 ms behind the look. Where it is drawn is not behind: see the first point.
- **It only writes.** Blending would mean reading scanout memory, which is slow. So the lines are solid black, a pixel wide at 720 rows and wider in proportion.

When Stage 09.5's outline arrives, `game` should hand it this `player_target_t` and stop calling `player_camera_outline_draw`. The depth test there will be better than the pieces here.
