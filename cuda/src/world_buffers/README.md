# cuda/src/world_buffers

**Purpose:** the voxel world on the card: the slab pool sections live in, and the toroidal column table with each column's section places and maximum height.
**Tier:** Borrow on both sides. It allocates nothing: `run/src/render/` owns the two device blocks and the host memory, and passes them in.
**Owns:** nothing. It divides what it is lent.
**Lifetime:** World: `render_open` to `render_close`.
**Threads:** one at a time: the `gpu` thread, on the stream the frame's kernels run on.
**Public functions** (`run/include/gpu_bridge/world_buffers/index.h`, for C and CUDA):

- `world_buffers_table_bytes`, `world_buffers_host_bytes`, `world_buffers_slab_bytes`: sizes.
- `world_buffers_init`, `world_buffers_deinit`: start with an empty table, on the card too; forget it all.
- `world_buffers_set_window`: scrolls the table.
- `world_buffers_column_set`, `world_buffers_column_clear`: what column a slot holds. A new load's sections go back to the pool.
- `world_buffers_section_write`: replaces a section; no bytes empties it.
- `world_buffers_flush`: sends the table's changes. Nothing a kernel reads changes before this.
- `world_buffers_probe`: a real kernel looks up voxels through the table and the tree.
- `world_buffers_voxel_lookup`, `world_buffers_section_voxel`, `world_buffers_slot_of`, `world_buffers_popcount`, `world_buffers_brick_of`, `world_buffers_brick_voxel_of`: the walk itself, inline on host and device, so a test on the host and a kernel on the card run the same lines.

Only for CUDA code (`cuda/include/world_buffers/index.hu`): `world_buffers_table_pointer`, `world_buffers_pool_pointer`, for kernels.

**May be called by:** `run/src/render/` and its parts, and `cuda/src/traverse/`, which walks the format.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/status`, `cuda/src/device_memory/`, the CUDA runtime.

## The pool

One block, cut into 1 MiB pages, each holding slabs of a single size. Twelve sizes, 128 to 13056 bytes, the last being the smallest multiple of the granule that holds the largest section there can be. A class's pages with room form a list, a page's returned slabs form a list inside it, and a page with nothing live goes back for any size, so a change in what sections look like cannot starve one size while another holds empty pages. Everything is constant time. A page that still holds one small section stays with its size: there is no compaction, since moving sections means sending them again.

The links live on the host in `borrowed_slab_next`, 2 bytes per granule. A 6 GiB pool has 96 MiB of address for that, and costs only what returned slabs touch.

## The table

256 bytes a slot. Section places are `1 + granule`, so an all-zero slot is an empty one. The host keeps a copy and sends only what changed: one copy per slot, or all of it at once when one slot in 64 or more changed. A flush has no budget, because a slab reused this frame must not be reachable through the card's old table.

## The rules

- A failure is a value, as in `device_memory`. A full pool is `GPU_STATUS_OVER_BUDGET`, not a fault: the section stays as it was, stale and whole.
- A freed slab may be reused at once. That is safe because copies and kernels run in one stream's order, on one thread: a kernel launched earlier finishes before a later copy starts. A second stream would break this.
- The format is in the header because it is the contract with `cuda/src/traverse/`. `run/src/render/encode/` writes it.
