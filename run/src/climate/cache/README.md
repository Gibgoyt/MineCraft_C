# run/src/climate/cache

**Purpose:** the slots of the climate's two toroidal tables, the tile cells table (`CLIMATE_TILE_SIDE`, 64 by 64 tiles) and the land-mask table (`CLIMATE_MASK_SIDE`, 128 by 128 pages): the key, the state, the reader count, and the rules for reading, claiming, publishing and evicting.
**Tier:** Arena by depth. It allocates nothing: the slots are in the Owner's region.
**Owns:** nothing.
**Lifetime:** none of its own; the slots live as long as `run/src/climate/`'s region.
**Threads:** any. Every slot is its own cache line.
**Public functions:**

- `climate_cache_key`, `climate_cache_slot_index`: a tile's key, both coordinates, and its slot, `(t_y mod N) * N + (t_x mod N)` by `& (N - 1)`, which is right for negative coordinates in two's complement. Two tiles in one slot are at least N tiles apart, and the collision rule is replacement.
- `climate_cache_keeps`: the keep square, `|t - keep| < N / 2` on both axes. Two tiles sharing a slot differ by a multiple of N on some axis, so at most one of them is in it.
- `climate_cache_empty`: every slot `EMPTY`, while nothing else can touch them.
- `climate_cache_read_begin`, `climate_cache_read_end`: the read protocol. Add 1 to `readers` (seq_cst), load `state` (seq_cst): only `READY` with the wanted key is a hit, and the data may be copied out until `read_end` takes the 1 off (release).
- `climate_cache_ready`: a look, not a hold: `READY` with the key at that moment. For "is it built".
- `climate_cache_claim`: `READY` with the key is `HAVE`; `CLAIMED` or `EVICTING` is `BUSY`; `EMPTY` in the keep square is compare-exchanged to `CLAIMED`, and the key stored; `READY` with another key, if the new key is in the keep square, is compare-exchanged to `EVICTING`, then `readers` is loaded (seq_cst): 0 stores the key and `CLAIMED`, anything else puts `READY` back and says `REFUSED`. Outside the keep square it is `REFUSED`: build into scratch and do not cache.
- `climate_cache_publish`: `CLAIMED` to `READY`, release, once the data is whole. `climate_cache_unclaim`: back to `EMPTY` for a build that will not happen.

**Why a reader and an evictor never both miss each other:** both write their own word and then read the other's, all seq_cst. Either the evictor sees `readers > 0` and backs off, or the reader sees `EVICTING` (or a later state) and backs off: Dekker's argument for two threads. tests/run/climate/cache/ runs it on real threads, and by hand under the `tsan` preset.

The job fields of a slot (`borrowed_job_owner`, `job_tile_x`, `job_tile_y`) are written by whoever claimed it, before the job that builds it is queued, and read only by that job: the queue's release and acquire carry them.

**May be called by:** its parent `run/src/climate/`, and `run/src/climate/mask/`.
**Depends on:** `types`, generated `machine/index.h`.
