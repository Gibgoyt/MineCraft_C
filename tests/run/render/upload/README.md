# tests/run/render/upload

**Purpose:** ctest `run_render_upload`. Tests `run/src/render/` with `upload`, and `cuda/src/world_buffers/`, on the real card, by asking the card: a real kernel looks up voxels through the table and the tree, and its answers must equal the world, voxel for voxel. Also the slab pool against a brute-force model, a pool running out and recovering, the budget, columns streaming in and out, no view let go under a worker, and a real thread editing the world while frames run.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, never more than 96 MiB.
**Lifetime:** One run.
**Threads:** main as `gpu`, the real workers, and one `sim`-role thread in the test that needs one.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/render/`, `run/src/gpu_bridge/`, `cuda/src/world_buffers/`, `run/src/world/`, `run/src/job/`, `run/src/thread/`, `run/src/time/`, and a card. No card fails this test, on purpose: `gpu.enabled` is true, and a skip would read as a pass.

Compute only. It opens no display and starts no other program.

`upload_cost_on_this_machine` prints what copies and flushes cost, for the stage file, and checks nothing about time.
