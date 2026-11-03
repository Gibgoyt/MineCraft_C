# Coding standard

This standard applies to every line of code in this repository. Where a rule can be checked by a machine, it is
checked: by compiler flags, by `static_assert`, or by `compile/src/standards_lint/`. A failed check fails the build.

Toolchain: **GCC 16 (`-std=c23`), nvcc 13 (`-std=c++20`, `sm_75`), NASM 3, CMake 4.4.** Linux and POSIX only for now.

**Status of this document:** written before any code existed, and in force over all of it since. The measurements Stage 01.1 owed this file are in
sections 1, 2, 8 and 9; the evidence is in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-01-stage-1-toolchain-link.md`.

---

## 1. Libraries

Allowed, and nothing else:

| Library | Why |
|---|---|
| glibc: libc, libm, pthreads | The C standard library, plus the POSIX layer in the same package |
| Linux syscalls and kernel UAPI headers (`drm/*`, `linux/input.h`, ...) | Display and input through our own code |
| CUDA runtime (`libcudart_static.a`) | **The one sanctioned exception.** Needed to run CUDA at all. Linked statically; at run time it loads the driver's `libcuda.so.1` itself |

The C++ runtime (`libstdc++`, `libgcc_s`) is **not allowed**. Stage 01.1 measured that nothing needs it: with the flags
in section 8 and static cudart, the binary needs only `libc.so.6` and `ld-linux-x86-64.so.2`, plus `libm.so.6` once our
code calls libm, and neither C++ library is loaded at run time. Shared `libcudart.so` is not used, because it depends on
both of them.

Banned: OpenGL, Vulkan, EGL, GBM, libdrm, X11, xcb, Wayland, SDL, GLFW, OptiX, stb, zlib, libpng, libyaml, and any
other third-party code. No networking: no TCP, no sockets, no server.

We write it ourselves: YAML parsing, PNG decoding, vectors, KMS, input.

---

## 2. Types

### Banned keywords

`int`, `short`, `long`, `unsigned`, `signed`, `float`, `double`. Also `size_t` and `ssize_t` in our own interfaces.

The point: anyone reading a declaration knows exactly how many bytes it takes.

### Allowed

| Use | Type |
|---|---|
| Integers | `int8_t`, `int16_t`, `int32_t`, `int64_t`, `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t` |
| Pointer as integer | `uintptr_t` |
| Floating point | `float32_t`, `float64_t` |
| Truth values | `bool` in locals, parameters and returns; `uint8_t` in structs |
| Text | `char` only for text passed to or from libc and syscalls; bytes are `uint8_t` |

### The single typedef site

`float32_t` and `float64_t` are defined once, in `run/include/types/index.h`, and that is the only file where the lint
accepts a floating-point base type:

```c
#ifdef __CUDACC__
typedef float  float32_t;
typedef double float64_t;
#define F32(x) x##F
#define F64(x) x
#else
typedef _Float32 float32_t;
typedef _Float64 float64_t;
#define F32(x) x##f32
#define F64(x) x##f64
#endif
typedef double libc_double_t;   /* only for passing to a varargs libc call */
static_assert(sizeof(float32_t) == 4);
static_assert(sizeof(float64_t) == 8);
static_assert(alignof(float32_t) == 4);
static_assert(alignof(float64_t) == 8);
```

Literals:

- **C-only files** use the suffix: `1.0f32`, `1.0f64`. An unsuffixed `1.0` is a `double` and is banned.
- **`.cu` and `.hu` files** use `1.0F` for `float32_t` and `1.0` for `float64_t`. Here an unsuffixed `1.0` is the correct
  `float64_t`, and `f32`/`f64` suffixes are banned.
- **Headers compiled on both sides** (`run/include/gpu_bridge/`, `run/include/types/`) use `F32(1.0)` and `F64(1.0)`.

Why CUDA differs, measured in Stage 01.1 on nvcc 13.4.92, `-std=c++20`, `sm_75`: `_Float32` in device code is a hard
error ("C++23 extended floating-point, which is not supported in device code"). An `f32` or `f64` literal in device
arithmetic crashes the compiler (`Internal Compiler Error (codegen): "unsupported float variant!"`), even when the
variables are plain `float`.

The two mappings are the same on the wire. A C `_Float32`/`_Float64` passed by value and by pointer across the
`extern "C"` boundary to `float`/`double` arrived with the right values.

Verified on GCC 16.2 with `-std=c23 -Wall -Wextra -Wpedantic -Wconversion -Wdouble-promotion -Werror`: `_Float32`
passes to and from libm functions such as `sinf`, and `1.0f32` is accepted. `printf("%f", x)` is **rejected** for a
`_Float64` (`-Wformat`: expects `double`), which is what `libc_double_t` is for (see "The printf rule").

### Standard library boundaries

- **`main`:** write `int32_t main(int32_t argc, char **argv)`. `int32_t` *is* `int` on this platform, so this is valid.
- **libc return values:** `int32_t rc = open(...);` works with no cast and no exception.
- **`size_t` at a libc call:** convert at the call. Store the value as `uint64_t` in our code.
- **`sizeof`:** assign to `uint64_t`, or use directly at a libc call.
- **Floats at a varargs call:** see "The printf rule" below.
- **Kernel structs** from UAPI headers are used as they are. They are system headers, so the padding check skips them.

### The printf rule

**Every float passed to a varargs call is cast to `libc_double_t` at the call, and nowhere else:**

```c
printf("%.2f ms\n", (libc_double_t)elapsed);
LOG_INFO("%.2f ms", (libc_double_t)elapsed);
```

- This covers `printf`, `fprintf`, `snprintf` and the rest of that family, and our `LOG_*` macros, which carry the same
  format attribute so that GCC checks them the same way.
- `float32_t` and `float64_t` both need the cast. To GCC they are `_Float32` and `_Float64`, distinct from `double`, and
  `-Wformat` under `-Werror` fails the build on either one. (Measured on GCC 16.2. Stage 01.1 had tried only `_Float64`.)
- `-Wno-format` is not an alternative: it would stop every format check in the program, and `standards_lint` rejects it
  (`cmake_weakening_flag`).
- `libc_double_t` is never the type of a variable, field, parameter or return value. Values stay `float32_t` or
  `float64_t`, and a plain `(double)` cast is still banned. This part is checked by review, because a token scan cannot
  tell a cast from a declaration.
- CUDA host code follows the same rule. There the cast changes nothing, and writing it keeps one rule for both languages.

ctest `run_types` asks the compiler, with the build's own flags: the bare forms must be rejected, and the casts accepted.

---

## 3. Struct layout: no compiler padding

**The compiler must never insert padding. Every byte in every struct is written down by us.**

`__attribute__((packed))` is **banned**. It does not solve this problem. It makes fields misaligned, which slows down
access and breaks aligned AVX loads. We keep natural alignment and write the padding ourselves:

```c
typedef struct section_header_t {
    uint64_t owned_blocks_offset;   /* 0  */
    uint32_t block_count;           /* 8  */
    uint16_t palette_count;         /* 12 */
    uint8_t  flags;                 /* 14 */
    uint8_t  _pad0[1];              /* 15 */
} section_header_t;
static_assert(sizeof(section_header_t) == 16);
static_assert(alignof(section_header_t) == 8);
```

Rules:

1. **Order fields largest first**, so few padding bytes are needed.
2. **Name padding `_pad0`, `_pad1`, ...** as `uint8_t` arrays. Initialize padding to zero when writing to disk or GPU.
3. **`-Wpadded -Werror=padded` is on for all C.** Interior and trailing compiler padding both fail the build. (Verified
   on GCC 16.2.)
4. **Every struct has `static_assert` on `sizeof`.** Structs shared with CUDA, NASM or files also assert `alignof` and
   each field's `offsetof`.
5. **`alignas(64)` is required on structs written by different threads**, so they do not share a cache line. Add the
   explicit trailing padding as well, or `-Wpadded` will reject the struct.
6. **Bitfields are banned.** Their layout is up to the compiler. Use shifts and masks on a `uintN_t`.
7. **Floating-point fields** follow the same rules.

`-Wpadded` is applied to GCC's C builds. CUDA code gets the same protection because shared structs live in C headers
whose `static_assert`s are compiled on both sides.

---

## 4. The memory law

### The One-Owner Principle

This is the one rule for how stack pointers lead to heap memory, used across the whole repo:

> **Every heap block has exactly one owning pointer. That pointer can be traced, through struct fields, back to one
> `app_t` on `main()`'s stack.**

```
main() stack frame
└── app_t app
    ├── world_t world
    │   └── owned_sections ───► heap region
    ├── render_t render
    │   └── arena_frame ──────► heap region
    └── ...
```

So:

- **No owning globals.** Nothing owned hides in a `static`.
- **A stack local never owns heap memory after its function returns.** A local may borrow a pointer. To keep memory, a
  function stores it in a struct field reachable from `app`.
- **Ownership is decided once, at init,** and does not move during normal running. The only transfer is an explicit
  `handoff_` through a named ring (below).

**Allowed exception:** a `static` holding a *borrowed* pointer, when the OS gives no other way in. The only current
case is the signal handler in `run/src/display/vt_guard/`, which must restore the console after a crash. Every such
`static` is named `borrowed_`, has a comment saying why, and is listed in the lint's allowlist.

### Directory depth tells you what code may do

| Depth | Path | Tier | May do | May not do |
|---|---|---|---|---|
| 0 | `run/src/main/` | **Root** | Declare `app_t app` on the stack; call each system's `_init` in order and `_deinit` in reverse | Allocate |
| 1 | `run/src/<system>/` | **Owner** | Call `heap_reserve` and `heap_release`; own regions and arenas | Hand out ownership |
| 2 | `run/src/<system>/<part>/` | **Arena** | Allocate from an `arena_t *` given by its parent | Touch the heap; free individual allocations |
| 3+ | `run/src/<system>/<part>/<sub>/` | **Borrow** | Use pointer + length given by the caller, and stack locals | Allocate at all |

Example: seeing `run/src/world/section/index.c`, you know without opening it that it allocates only from a given arena
and never frees anything. If memory leaks, the bug is in `run/src/world/index.c`, because that is the only file in the
system that could have reserved it.

Any tier may allocate less than it is allowed. `run/src/math/vec3/` is depth 2 and allocates nothing.

### Allocation is in one place

**`run/src/heap/index.c` is the only code that calls the OS allocator.** It is the `malloc`/`free` of this codebase,
built on `mmap` with 2 MiB alignment and `MADV_HUGEPAGE`, and it counts every byte.

| Banned everywhere else | Use instead |
|---|---|
| `malloc`, `calloc`, `realloc`, `free`, `aligned_alloc` | `heap_reserve`, `heap_release` (Owner tier), `arena_alloc` (Arena tier) |
| `mmap`, `munmap`, `brk`, `sbrk` | Same |
| `alloca`, VLAs | Fixed-size locals, or the Job arena |
| Recursion | Explicit stack from an arena |

Exceptions: mapping a DRM dumb buffer is device memory, done in `run/src/display/device/`, and the lint allowlists that
one file. Memory allocated inside libc or `libcudart` is outside our control. Tests may use a test arena.

### Pairing

**Whoever initializes, deinitializes, in the same `index.c`.** Every `x_init` has an `x_deinit` right next to it. If
`x_init` fails partway, it cleans up what it did and returns an error. `goto cleanup` is allowed for this and nothing else.

### Names show ownership

| Prefix | Meaning | Duty |
|---|---|---|
| `owned_` | This struct holds the block | Must release it in `_deinit` |
| `borrowed_` | Someone else holds the block | Must not outlive the owner; never release |
| `arena_` | A bump allocator | Reset at its lifetime boundary |
| `handoff_` | In transfer through a ring | The receiver takes over; the sender stops touching it |

The prefix goes on struct fields and on `static`s. Plain function parameters are always borrowed, so they need no prefix.

### Lifetimes

| Lifetime | Reset when | Used for |
|---|---|---|
| **Process** | Exit | Tables, thread contexts, GPU pool |
| **World** | World closes | Section pools, entity stores |
| **Tick** | Each sim tick | Per-tick working data |
| **Frame** | Each rendered frame | Per-frame lists, HUD |
| **Job** | Each job | Worker scratch space; one arena per thread |

**No heap calls in hot paths.** The heap layer counts calls, and a test confirms that the count stays flat during play.

### Threads

- Each thread has its own Job arena.
- Pointers cross threads **only through a named ring**, as `handoff_` values or borrowed snapshots.
- Data written by one thread and read by another uses `<stdatomic.h>`, with its memory order stated.
- Thread stacks are small and fixed. Large data never goes on the stack: `-Wframe-larger-than=` enforces this.

### GPU memory

The same law applies. **`cudaMalloc` and `cudaFree` appear only in `cuda/src/device_memory/index.cu`.** Each other CUDA
directory receives device pointers from its owner.

### Compile-time tools

Tools in `compile/` follow the same tiers, with `compile/src/<tool>/` as the Owner and one process arena. They are short-lived,
but the rule stays the same, so nobody has to remember two models.

---

## 5. Files and directories

**Every source file is named `index`, in its own directory, next to a `README.md`:**

| Language | Source | Header |
|---|---|---|
| C | `<root>/src/<path>/index.c` | `<root>/include/<path>/index.h` |
| CUDA | `cuda/src/<path>/index.cu` | `cuda/include/<path>/index.hu` |
| NASM | `asm/src/<path>/index.asm` | `asm/include/<path>/index.inc` |

- **`.hu` is the only CUDA header extension.** A `.h` under `cuda/` is a lint error.
- **Hard limit: 2000 lines in any `.c .h .cu .hu .asm .inc` file.** One more fails the build.
- **Split at 1500 lines,** by making sub-directories. A long file is a sign of a missing directory.
- **A source directory holds only one `index.*` pair member and a `README.md`.** Headers live in the matching `include/` path.
- **Every source directory's `README.md` states:** purpose, tier, what it owns, public functions, and what may call it.

Full layout: `plans/REPO_STRUCTURE.md`.

---

## 6. Naming and C style

- Functions and variables: `lower_snake_case`, prefixed by module: `section_get_block`.
- Types: `lower_snake_case_t`. All structs are `typedef`ed.
- Constants and macros: `UPPER_SNAKE_CASE`. Machine values start with `MC_`, as in `MC_THREADS_WORKERS`.
- Functions that can fail return `bool` or an error enum, and give results through a pointer parameter.
- Small structs (32 bytes or less) may be passed and returned by value.
- `goto` is only for cleanup in `_init` functions.
- No `-ffast-math`: same seed, same world, on any run.
- Comments explain why. Matching the surrounding code matters more than any personal style.
- `clang-format` runs from a config in the repo root.

---

## 7. SIMD and NASM

**All deliberate CPU SIMD is written in NASM, in `asm/`.** Banned in C: intrinsics, `immintrin.h` and its family, GCC vector
extensions, and inline `asm`.

C builds with `-fno-tree-vectorize -fno-tree-slp-vectorize`, so every SIMD loop in the program can be found in `asm/`.
`-march=skylake` stays on so that GCC's scalar code uses VEX encoding, which avoids SSE/AVX transition penalties. GCC
still uses SIMD registers for scalar floating-point math and small struct copies. That is expected and is allowed.

### Batch only

**A NASM function must process an array.** The C vector library stays scalar, because a call to assembly for one
`vec3_add` costs more than the add. Good: `noise3_batch8(...)`. Bad: `vec3_add_asm(a, b)`.

Benchmarks must show that each kernel beats its scalar C reference. If one does not, it gets deleted.

### Target hardware

Comet Lake i9-10900K. Baseline: AVX2, FMA3, BMI1/2, POPCNT, LZCNT, F16C, with 16 YMM registers. **No AVX-512.** MMX is not used. Each
feature is guarded by a `MC_CPU_HAS_*` define from `config/machine.yaml`. At startup, `run/src/main/` checks CPUID
against the build flags and exits with a clear message if they do not match. That code is built with the same `-march`,
so on a CPU older than the flag it could die of SIGILL first. So `asm/src/cpuid/` makes the same check from `.preinit_array`,
with baseline x86-64 instructions only, before any code compiled with `-march` runs: see Stage 03.1 issue 1.

### Required form

```nasm
default rel
section .note.GNU-stack noalloc noexec nowrite progbits

section .text
global noise3_batch8
align 32
noise3_batch8:
    ; rdi = out, rsi = xs, rdx = ys, rcx = zs, r8 = count
    ; ...
    vzeroupper
    ret
```

- **SysV AMD64 ABI:** integers in `rdi, rsi, rdx, rcx, r8, r9`; floats in `xmm0-7`; preserve `rbx, rbp, r12-r15`.
- **`default rel`** always.
- **`vzeroupper` before every `ret`** from code that used YMM.
- **`.note.GNU-stack`** in every file, so the stack stays non-executable.
- **Aligned data at 32 bytes**, and say in a comment whether a pointer must be aligned.
- **Each kernel has a C prototype** in `run/include/simd/<name>/index.h`, and a test against a scalar C reference.
- **`asm/src/cpuid/` is the one directory here that is not SIMD.** CPUID and XGETBV have no C form and inline `asm` is
  banned, so they are written in NASM, along with the early guard that needs them. They use no vector register and, like
  everything here, take arrays. Their test has no scalar C reference to run against, so it compares with `/proc/cpuinfo`.
- **Struct offsets come from `compile/src/struct_offsets/`.** Numbers are never typed by hand.
- NASM produces `.o` files only. They link into the program that the C entry point starts.

---

## 8. CUDA

nvcc compiles `.cu` as C++, so we restrict it to a **C-like subset**:

- Allowed: functions, plain structs, the sized types above, `static_assert`, `__global__`/`__device__`/`__host__`.
- Banned: exceptions, RTTI, `new`/`delete`, STL, classes with methods, inheritance, virtual functions, templates, and
  `int`/`float`/`double`, apart from the mapping at the typedef site in section 2.
- Build flags always include `-Xcompiler=-fno-exceptions,-fno-rtti,-fno-threadsafe-statics`. Stage 01.1 confirmed that
  they remove every C++ runtime symbol from the object. On the spike kernel, `-fno-threadsafe-statics` alone removed all
  of them. The kernel source has no function-local static of its own, so that guard code comes from nvcc's generated code.
  `-fno-exceptions` alone left `__cxa_guard_acquire` and `__cxa_guard_release`, and `-fno-rtti` alone removed nothing. All
  three stay, because a later kernel could bring in what the others guard against. nvcc rejects these flags without
  `-Xcompiler` unless `-forward-unknown-to-host-compiler` is also given. Without them the link fails, so forgetting them
  cannot go unnoticed.
- What remains undefined is cudart's `cuda*` and `__cuda*` symbols, plus glibc's `atexit`, `exit` and `__stack_chk_fail`.

### The boundary

```
run/include/gpu_bridge/<part>/index.h      C API, extern "C", shared structs
cuda/include/<dir>/index.hu                includes it; adds CUDA-only parts
cuda/src/<dir>/index.cu                    implements it
```

- **C code never includes a `.hu`.** It uses only `gpu_bridge` headers.
- **Everything crossing the boundary is C-compatible,** wrapped in `extern "C"` under `#ifdef __cplusplus`.
- **Shared structs follow section 3.** Both compilers check the same `static_assert`s.
- **Every CUDA call's result is checked.** Failures become an error value at the boundary.

---

## 9. Compile flags

```
-std=c23 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wdouble-promotion
-Wshadow -Wvla -Wpadded -Werror -march=<cpu.arch_flag> -mtune=<cpu.arch_flag>
-fno-tree-vectorize -fno-tree-slp-vectorize
```

Release adds `-O3 -flto`. Exact values live in `cmake/` and come from `config/machine.yaml`.

### Link list (measured in Stage 01.1)

gcc performs the final link, never nvcc or g++:

```
gcc <nasm objects> <c objects> <cuda objects> -L/opt/cuda/lib64 -lcudart_static -lm
```

Measured: without `-lm`, the spike links and runs with `-lcudart_static` alone. Adding `-lm` puts `libm.so.6` in `NEEDED`,
but the spike itself calls no libm function, so `-lm` is here for our own code. `-ldl -lrt -lpthread` add nothing.

In CMake, `-lstdc++` must be kept out explicitly, because the CUDA language adds it to any target holding CUDA objects:

```cmake
set(CMAKE_CUDA_RUNTIME_LIBRARY Static)
set(CMAKE_CUDA_IMPLICIT_LINK_LIBRARIES "")          # after project(); otherwise -lstdc++ -lm are added
set(CMAKE_CUDA_IMPLICIT_LINK_DIRECTORIES "")
set_target_properties(minecraft_c PROPERTIES LINKER_LANGUAGE C)   # otherwise g++ links
target_link_directories(minecraft_c PRIVATE /opt/cuda/lib64)
target_link_libraries(minecraft_c PRIVATE cudart_static m)
```

CMake still adds `-lcudadevrt -lrt -lpthread -ldl` and an unused `-rpath`. None of them changes `NEEDED`.

| Run-time need | Source |
|---|---|
| `libc.so.6`, `ld-linux-x86-64.so.2`, and `libm.so.6` once used | `NEEDED` in the binary |
| `libcuda.so.1`, `libnvcuextend.so` | Loaded by cudart and the driver with `dlopen`; come with the NVIDIA driver, not with us |

---

## 10. What enforces what

| Rule | Enforced by |
|---|---|
| Banned keywords, intrinsics, inline asm, banned allocators | `standards_lint` |
| 2000-line limit | `standards_lint` |
| `index.*` and `README.md` in each directory, `.hu` only | `standards_lint` |
| Allocation by tier | `standards_lint`, using path depth |
| Compiler padding | `-Werror=padded` |
| Struct sizes and offsets | `static_assert`; `standards_lint` requires them for every struct (`sizeof`) and for shared structs (`alignof`, each field's `offsetof`) |
| Flags set only in `cmake/flags.cmake`, none taken away | `standards_lint` reads the build files |
| Formatting | ctest `clang_format` |
| Floats at varargs calls: the printf rule | `-Wformat` under `-Werror`; ctest `run_types`; review for `libc_double_t` used as anything but a cast |
| VLAs | `-Wvla` |
| Recursion | Call-graph check from GCC's `-fcallgraph-info`, on every preset |
| Large stack frames | `-Wframe-larger-than=` |
| Leaks | Heap counters must read zero at exit; sanitize preset |
| Ownership prefixes, thread rules | Review; the lint checks prefixes where it can; the `tsan` preset, run by hand, for what threads do to each other's memory |

---

## 11. Commits

A commit message is one sentence, in plain language. **Never include** `Co-Authored-By`, `noreply@anthropic.com`,
`support@anthropic.com`, or "Generated with Claude Code". Commit with `git commit -F -`, then confirm that this prints
nothing:

```bash
git log -1 --format=%B | grep -iE 'co-authored|anthropic|claude|opus|sonnet'
```

Never push unless asked. Never leave a process running: tests run under `timeout`, then confirm with `pgrep`.
