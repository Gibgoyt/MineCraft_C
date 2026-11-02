# run/src/arena

**Purpose:** bump allocators: many small allocations from one block, all taken back at once by a reset.
**Tier:** Owner by depth, but it reserves nothing. It works over memory its caller passes in.
**Owns:** nothing. An Owner holds the `heap_region_t` and the `arena_t`, as `owned_region` and `arena_<lifetime>` fields.
**Lifetime:** whatever the arena is for: Process, World, Tick, Frame or Job. It is reset at that boundary.
**Threads:** one at a time per arena; each thread has its own. Nothing here is atomic, and no call makes a syscall, so
this is what hot paths use.
**Public functions:**

- `arena_init`, `arena_deinit`: start an arena over a block, and end it. The block goes on being the caller's.
- `arena_alloc`: a block at a power-of-two alignment. Running out logs the arena's name and aborts, so it returns no
  `nullptr` to check for. Its size is a budget, and going over is a bug.
- `arena_alloc_try`: the same but returns false on running out, for a caller who can do something else.
- `arena_alloc_zeroed`, `arena_alloc_array`: filled with zeros; and a count of elements, with the size checked.
- `arena_reset`: takes everything back. Nothing is cleared, so it costs a few writes.

An Owner sets one up like this, and undoes it in the reverse order in its `_deinit`:

    if (!heap_reserve(FRAME_ARENA_BYTES, &render->owned_region)) { return false; }
    arena_init(&render->arena_frame, "frame", render->owned_region.owned_base, render->owned_region.bytes);

Allocation names all start `arena_alloc` because that is what `standards_lint` looks for to enforce the tiers.

**In the sanitize preset** memory that is not handed out is poisoned, so ASan catches a read or write past the last
allocation, in an alignment gap, past the end of the arena, or through a pointer from before a reset. It has no red
zones: running off one block into the one straight after it goes uncaught. Adding them would give the sanitize preset
different limits from the build that ships.

**May be called by:** the Owner tier sets arenas up. The Owner and Arena tiers allocate. `standards_lint` rejects the
`arena_alloc` family in the Root and Borrow tiers.
**Depends on:** `assert`, `log`, glibc.
