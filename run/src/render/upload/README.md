# run/src/render/upload

**Purpose:** the budgeted upload queue: takes snapshot views of the world, has workers encode their sections, and sends them to the card a frame's budget at a time.
**Tier:** Arena: it takes its job records, staging memory and ring from the arena its Owner gives it, once.
**Owns:** nothing beyond that arena's share. Its stats are on `render_upload_t`.
**Lifetime:** World.
**Threads:** `gpu` for everything but `render_upload_resend_drain`, which belongs to whichever thread holds the world's map (`sim`, once it exists). The two sides meet only through a ring.
**Public functions** (`run/include/render/upload/index.h`):

- `render_upload_arena_bytes`: what init will take.
- `render_upload_init`, `render_upload_deinit`: set up; wait for what is running.
- `render_upload_frame`: one frame's work. Never waits for a worker.
- `render_upload_idle`: whether anything is on its way.
- `render_upload_resend_drain`: gives sections the pool refused back to the world as dirty.

**May be called by:** `render`, and `render/lod`, for its budget. It includes its sibling `render/encode`, which only it calls.
**Depends on:** `render/encode`, `gpu_bridge/world_buffers`, `world/edit`, `job`, `thread/ring`, `arena`, `assert`.

## How a section gets to the card

1. The `sim` thread publishes a view. Publishing never waits for this.
2. Once every section of the last view is dealt with, `render_upload_frame` takes the newest view. It applies the view's table to the host's copy at once: a column that left or was replaced gives its slabs back, here, before any section of this view asks for one.
3. Each section becomes a job on a worker. If the shared queue is full the rest wait for a later frame.
4. Finished sections go out in the view's order, up to the budget. A section with no blocks left costs no budget. One that is still with a worker stops the frame's sending: nothing here waits for it.
5. `world_buffers_flush` sends the table, so a frame's kernels see every section sent so far, and only them.

A view stays whole until the next is taken, and that is the only thing keeping sections' voxels alive for the workers. So a view is never let go with a job still reading it: each section's `done_bytes` is seen first.

Views wait for the budget, and the world does not wait for views: while one view drains, those published replace one another, and each replaced view's sections return to dirty. A section can be sent late, and is never lost.

## When the pool is full

`world_buffers_section_write` keeps the old bytes, stale and whole, and says so. The section goes in a ring back to the thread holding the map, which calls `render_upload_resend_drain`, and so it is dirty again, for a later view. If the ring is full the section keeps its place and sends stop for this frame, so that nothing is ever dropped. This retries for as long as the pool is full, at one re-encode per section per view, which is a fair price for a state the game should not be in. A column of a generation that has gone is ignored by the restore.
