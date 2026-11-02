# compile/src/struct_offsets

**Purpose:** reads every `typedef struct` from the C headers it is given and writes their layout for NASM, so that no
struct offset in `asm/` is ever typed by hand.
**Tier:** Owner of its one process arena, as every tool is. It has no parts.
**Owns:** one mapped arena, released before it exits.
**Lifetime:** Process, and the process is one build step.
**Threads:** one.
**Public functions:** `main`. Run as `struct_offsets <output directory> <header>...`. Exit status 0, 1 if a header was
refused, with the file and line on stderr and no output written, or 2 if it could not run.
**May be called by:** the build (`cmake/targets.cmake`, which also holds the list of headers), and
`tests/compile/struct_offsets/`.
**Depends on:** glibc only. It reads `run/include/` as text, since a tool may not include it.

Each header must have `include/` in its path: what follows it is what the output includes.

| Output | For | Holds |
|---|---|---|
| `build/<preset>/generated/struct_offsets/index.inc` | NASM | `%define MAT4_T_SIZEOF 64`, `MAT4_T_ALIGNOF`, and one `MAT4_T_M` per field. `_pad` fields get none |
| `build/<preset>/generated/struct_offsets/index.h` | C | The same numbers as `static_assert`s, padding fields included |

The tool works layouts out itself, from natural alignment and `alignas`, so it has to be checked against the compiler:
`tests/asm/vec_batch/` includes `index.h`, so a disagreement stops the build.

It refuses, rather than guesses at, what it cannot lay out: an unknown type, a bitfield, a function pointer, a dimension
that is not a plain number, a nested `struct`, `union` or `enum`, and any layout needing compiler padding. A header's
structs may use the structs of a header listed before it.
