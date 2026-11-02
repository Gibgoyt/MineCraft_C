# run/src/heap

**Purpose:** the only code that calls the OS allocator: reserves and releases 2 MiB aligned regions advised
`MADV_HUGEPAGE`, holds them to the process budget, and counts everything.
**Tier:** Owner by depth, and the layer under every other Owner.
**Owns:** nothing. It hands each region to the struct field that calls `heap_reserve`, and that field is the region's
one owner. Its only state is counters, which point at nothing.
**Lifetime:** Process. It works from the first call, so it has no `_init`.
**Threads:** any. The counters are atomic. Not for hot paths: every call is a syscall or several.
**Public functions:**

- `heap_reserve`: maps zeroed memory, rounded up to whole 2 MiB. Fails on a size of 0, on going over
  `memory.process_budget_mib`, and when the OS says no.
- `heap_release`: unmaps a region and empties its handle. An empty handle does nothing. Aborts on a misaligned handle or
  one whose memory is already unmapped. A stale copy of a handle goes undetected if something else has since been mapped
  at that address.
- `heap_counters_read`: live bytes and regions, peak, calls, failures.
- `heap_exit_check`: logs and returns false if anything is still live; `main` turns that into exit status 4.

**May be called by:** the Owner tier, `run/src/<system>/index.c`. `standards_lint` rejects `heap_reserve` and
`heap_release` from the Root, Arena and Borrow tiers. `run/src/main/` may call only the two read-only functions.
**Depends on:** `assert`, `log`, the generated `machine/index.h`, glibc.
