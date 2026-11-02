# run/src/thread/ring

**Purpose:** the named channel pointers and values cross threads through: one producer, one consumer, no locks.
**Tier:** Arena by depth. It allocates nothing itself: the caller gets `ring_storage_bytes` from its own arena or region
and passes the storage to `ring_init`.
**Owns:** nothing. `borrowed_storage` belongs to the caller. Elements are copied by value, and a pointer sent as an
element is a `handoff_`: the receiver takes it over.
**Lifetime:** That of the storage; the caller decides.
**Threads:** exactly one producer thread and one consumer thread per ring. Two producers or two consumers on one ring
corrupt it, and nothing here can detect that. For many senders, use one ring per sender. `ring_count` may be called from
any thread.
**Public functions:**

- `ring_storage_bytes`: the storage a ring needs, or 0 for invalid sizes.
- `ring_init`, `ring_deinit`: a pair, around storage the caller owns.
- `ring_try_push`, `ring_push_many`: producer side; never block, and report how much went in.
- `ring_try_pop`, `ring_pop_many`: consumer side; never block, and report how much came out.
- `ring_count`: how full it is.

**May be called by:** anything in `run/` from this layer up. Named in `plans/CODING_STANDARD.md` section 4 as the only
way a pointer may cross threads.
**Depends on:** glibc. `machine/index.h` for the cache line size, so the layout follows `config/machine.yaml`.
