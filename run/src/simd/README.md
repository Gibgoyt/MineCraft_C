# run/src/simd

**Purpose:** the Owner tier of the SIMD kernels in `asm/`: holds the scratch memory they may borrow, and refuses to start a
game whose kernels disagree with their scalar references.
**Tier:** Owner.
**Owns:** `owned_region`, one 2 MiB heap region, and `arena_scratch` over it. Today only the self-check uses them: a kernel
takes pointers from its caller and needs no memory of its own. Only the pages the check touches cost anything.
**Lifetime:** Process.
**Threads:** `main` for init, deinit and the self-check. The kernels themselves have no state, so any thread may call them.
**Public functions** (`run/include/simd/index.h`):

- `simd_init`: reserves the region and runs the self-check.
- `simd_deinit`: releases the region. Safe on a zeroed `simd_t`.
- `simd_self_check`: runs each kernel against its scalar reference. Logs the first that differs and returns false.

The kernels' own prototypes are in `run/include/simd/<name>/index.h`, one directory per file in `asm/src/`.

**May be called by:** `run/src/main/`, and any system for the kernel headers.
**Depends on:** `heap`, `arena`, `log`, `simd/reference`, `asm/src/{copy,blend,bitmask}`.
