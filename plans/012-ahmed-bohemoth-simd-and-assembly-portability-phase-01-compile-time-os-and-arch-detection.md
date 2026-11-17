# 012-ahmed-bohemoth-simd-and-assembly-portability — phase 1: Compile-time OS and arch detection

**Status:** not started   (updated 2026-11-17). Waits for Ahmed's go on plan 012.

Read first: `plans/012-ahmed-bohemoth-simd-and-assembly-portability-phase-00-vision-and-plan-files.md` (its Issues found
3 and 4 above all), then the master `plans/012-ahmed-bohemoth-simd-and-assembly-portability.md` (sections 1.3 items 1, 2
and 10, section 3, and Q1 and Q2 in section 9).

## Goal

**Which OS and which CPU architecture the build is for is one fact, written once in `config/machine.yaml`, and every
language sees it at compile time.**

- **The yaml** has `platform: { os: linux, arch: x86_64, posix: true }`. `os` is one of `linux, windows`, and `arch` is one
  of `x86_64, arm64`.
- **The generator** writes, through its existing choice-word code:
  - `MC_PLATFORM_OS_LINUX` and `MC_PLATFORM_OS_WINDOWS`;
  - `MC_PLATFORM_ARCH_X86_64` and `MC_PLATFORM_ARCH_ARM64`, each 1 or 0;
  - into `machine/index.h`, `index.hu`, `index.inc` and `index.cmake`.
- **Configure** checks the yaml against CMake's own view (`CMAKE_SYSTEM_NAME`, `CMAKE_SYSTEM_PROCESSOR`) and stops on a
  mismatch, naming both. It picks the NASM object format from the OS: `elf64` for linux, `win64` for windows. A Windows
  or arm64 configure then stops with a sentence saying what is not ported yet (master Q2).
- **C and CUDA** get a header-only `run/include/types/platform/index.h`. It stops the compile (`#error`) if the generated
  defines disagree with the compiler's own `__x86_64__`/`_M_X64`, `__aarch64__`/`_M_ARM64`, `__linux__`/`_WIN32`.
- **NASM** gets the same check in `asm/include/conventions/index.inc`, against `__?OUTPUT_FORMAT?__`. It also refuses a
  `win64` object outright until phase 02's layer exists. That closes master 1.3 item 10, the silent Win64 assemble.
- **The lint** bans `windows.h`.

Nothing in the game changes behaviour: on this machine every new define is the value it already implicitly had.

## Where this fits

**The vision.** Ahmed: "Please use compile time windows/linux detection so that we can optimize for either", and "please
make it extremely easy to also have at compile time definition arch x86-64". This phase is the definitions. Phase 02 is
the first thing to optimise by them.

**What it reads:** the generator's choice keys (`compile/src/machine_config/index.c:99`, `:450-475`), which already turn
each allowed word into `<MACRO>_<WORD>` (master 1.3 item 2).

**What later phases read from it:**
- phase 02 picks its register table from `__?OUTPUT_FORMAT?__`, cross-checked with `MC_PLATFORM_OS_*`, and removes this
  phase's Win64 stop;
- phase 06 picks source files by `MC_PLATFORM_ARCH_*`.

**What this phase must NOT build:**
- No Windows or arm64 port of anything. A Windows or arm64 configure still stops, with a sentence that says why.
- No change to any kernel's code, and no `MC_FUNC` (phase 02).
- No runtime check of the OS. Everything here is compile time.
- No second naming scheme (`MC_OS_*`, `MC_ARCH_*`) beside the generator's (master Q1; phase 00 Issues found 3).

## Ahmed's words

- "Please use compile time windows/linux detection so that we can optimize for either"
- "as of right now, we are only writing x86-64 assembly, whenever we have arm64 in front of us, we will add its respective
  assembly, but please make it extremely easy to also have at compile time definition arch x86-64"

---

## Background, explained

### Today (read 2026-11-17)

- `config/machine.yaml:1`: `platform: { os: linux, posix: true }`. No arch.
- `compile/src/machine_config/index.c:99`: `KEY_CHOICE("platform.os", "MC_PLATFORM_OS", "linux")`. The third argument is
  the comma-separated list of allowed words. A word outside it is refused ("platform.os must be one of: linux", measured
  in `plans/REPO_STRUCTURE.md` 15.1). A key missing from the yaml is refused too ("missing key", `:331`).
- `machine_output_choices` (`:450-475`) writes one line per allowed word, upper-cased, set to whether it was chosen. The
  generated `index.inc` holds `%define MC_PLATFORM_OS "linux"` and `%define MC_PLATFORM_OS_LINUX 1`.
- `cmake/machine.cmake:39-48` runs the tool at configure and `include`s `machine/index.cmake`, before `cmake/flags.cmake`.
- `cmake/flags.cmake:25` fixes `CMAKE_ASM_NASM_OBJECT_FORMAT elf64`; `:44` passes `-march=${MC_CPU_ARCH_FLAG}`.
- No source file outside `plans/` tests `__linux__`, `_WIN32`, `__x86_64__`, `_M_X64`, `__aarch64__`, `CMAKE_SYSTEM_NAME`
  or `CMAKE_SYSTEM_PROCESSOR`.

### What the generator will write (from the yaml change alone)

| yaml | `index.h` / `index.hu` | `index.inc` | `index.cmake` |
|---|---|---|---|
| `os: linux` | `#define MC_PLATFORM_OS "linux"`, `MC_PLATFORM_OS_LINUX 1`, `MC_PLATFORM_OS_WINDOWS 0` | `%define` the same | `set(MC_PLATFORM_OS_LINUX 1)` ... |
| `arch: x86_64` | `#define MC_PLATFORM_ARCH "x86_64"`, `MC_PLATFORM_ARCH_X86_64 1`, `MC_PLATFORM_ARCH_ARM64 0` | the same | the same |

The word `x86_64` upper-cases to `X86_64`, a valid identifier in all four languages. `arm64` gives `ARM64`.

### The cross-check table

| yaml `os` | CMake `CMAKE_SYSTEM_NAME` | yaml `arch` | CMake `CMAKE_SYSTEM_PROCESSOR` | NASM format |
|---|---|---|---|---|
| linux | `Linux` | x86_64 | `x86_64` (or `AMD64`) | `elf64` |
| windows | `Windows` | x86_64 | `AMD64` (or `x86_64`) | `win64` |
| linux | `Linux` | arm64 | `aarch64` (or `arm64`) | none: NASM has no AArch64 (phase 06) |
| windows | `Windows` | arm64 | `ARM64` | none |

The Windows and arm64 values are CMake's documented spellings (from the host's `PROCESSOR_ARCHITECTURE` and `uname -m`),
not run here. Matching is case-insensitive, and the test covers each spelling in the table.

### What the C header checks

```c
/* run/include/types/platform/index.h: the yaml's platform and the compiler's must be the same machine. */
#if MC_PLATFORM_ARCH_X86_64 && !(defined(__x86_64__) || defined(_M_X64))
#error "config/machine.yaml says platform.arch x86_64, and this compiler is not targeting x86-64"
#endif
/* ... the same for ARM64 (__aarch64__ / _M_ARM64), LINUX (__linux__), WINDOWS (_WIN32), and exactly one of each set */
```

It uses `#error`, not `static_assert`, because only the preprocessor can see `defined`. It includes the generated
`machine/index.h`, or `machine/index.hu` under `__CUDACC__`.

## Owned paths

This phase may edit these, plus this file:
- `config/machine.yaml` (the `platform` line only)
- `compile/src/machine_config/index.c` (the two keys), its `README.md`
- `tests/compile/machine_config/` (`index.c`, `README.md`, `test.cmake`)
- `cmake/machine.cmake` (after the `include`), `cmake/flags.cmake` (line 25 and the `-march` lines), and a new
  `cmake/platform_check.cmake`, runnable alone with `cmake -P` so a test can drive it
- `run/include/types/platform/` (`index.h`, `README.md`), new
- `asm/include/conventions/index.inc` (the format and arch check only; the layer is phase 02's), its `README.md`
- `tests/asm/conventions/` (`index.c`: the fake `machine/index.inc` gains the platform lines; new tests)
- `compile/src/standards_lint/rules_include/index.c` (one row), its `README.md`, and a fixture under
  `tests/compile/standards_lint/fixtures/`

**Not touched:** any `asm/src/`, `run/src/`, `cuda/`, `plans/CODING_STANDARD.md` (phase 02 rewrites section 7),
`plans/REPO_STRUCTURE.md` (phase 07).

## Steps

1. **The yaml and the generator.**
   - `platform.os` choices become `"linux,windows"`; a new `KEY_CHOICE("platform.arch", "MC_PLATFORM_ARCH", "x86_64,arm64")`
     joins the key table, enum and order beside it.
   - `config/machine.yaml:1` becomes `platform: { os: linux, arch: x86_64, posix: true }`.
   - *Why: one fact, one place; the choice code gives every language its defines for free.*
2. **`cmake/platform_check.cmake`.**
   - It takes `MC_PLATFORM_OS`, `MC_PLATFORM_ARCH`, `CMAKE_SYSTEM_NAME` and `CMAKE_SYSTEM_PROCESSOR`, and sets
     `MC_NASM_OBJECT_FORMAT`.
   - It stops with `FATAL_ERROR` on a mismatch: "config/machine.yaml says platform.os windows, and CMake is building for
     Linux".
   - It stops on Windows or arm64 with "plan 012: only Linux x86-64 builds today; ..." (Q2).
   - `cmake/machine.cmake` includes it after `index.cmake`; `cmake/flags.cmake:25` reads `MC_NASM_OBJECT_FORMAT`.
   - *Why: a yaml copied to the wrong machine must fail at configure, not as a strange compile error.*
3. **`run/include/types/platform/index.h`** and its README, as above. `run/src/main/index.c` is not changed, so the header
   is proven by the test, not by being included everywhere.
   - *Why: a C file that later needs `#if MC_PLATFORM_OS_WINDOWS` has one header to include, which also proves the yaml
     right.*
4. **`asm/include/conventions/index.inc`**:
   - `elf64` needs `MC_PLATFORM_OS_LINUX`; `win64` needs `MC_PLATFORM_OS_WINDOWS`; any other format is `%fatal`.
   - `MC_PLATFORM_ARCH_X86_64` is required ("asm/src/*/index.asm is x86-64").
   - Then, under `win64`, `%fatal` "Win64 needs plan 012 phase 02's conventions layer".
   - The header comment (lines 5-7) gains one line: "These register rules are SysV's; Win64's differ (plan 012)".
   - *Why: closes the silent Win64 assemble (master 1.3 item 10) now, not after phase 02.*
5. **`windows.h` banned**: `{"windows.h", "banned_header:windows", INCLUDE_MATCH_EXACT, {0}}` and the same for
   `Windows.h`, in `compile/src/standards_lint/rules_include/index.c`'s table (`:23-48`).
   - *Why: a Windows port, if it comes, starts as one platform layer and not as `#ifdef _WIN32` scattered through the
     game.*
6. **Tests**, below; then the whole debug suite. *Why: every generated file changed, so everything rebuilds.*

## Todo

From the master (section 8, phase 01):

- [ ] `config/machine.yaml`: `platform.arch: x86_64`; `platform.os` accepts `linux | windows`
- [ ] `compile/src/machine_config`: the arch key; `MC_PLATFORM_OS_*` and `MC_PLATFORM_ARCH_*` in `machine/index.{h,hu,inc,cmake}`
- [ ] `cmake/platform_check.cmake`: the cross-check against `CMAKE_SYSTEM_NAME`/`CMAKE_SYSTEM_PROCESSOR`, the NASM object format, the "not ported yet" stop
- [ ] `cmake/flags.cmake:25` takes the format from it
- [ ] `run/include/types/platform/index.h` and README
- [ ] `asm/include/conventions/index.inc`: the format and arch check, and the Win64 stop
- [ ] `rules_include`: `windows.h` banned, with a fixture

Expanded:

- [ ] Every test in the table below passes
- [ ] The generated `machine/index.inc` diff is only the new lines (look at it, record it here)
- [ ] `standards_lint`, `clang_format` and the whole debug suite pass; `pgrep -af "$PWD/build"` prints nothing

## Tests

| Test (ctest) | Checks | Numbers | Fails when |
|---|---|---|---|
| `compile_machine_config` `platform_arch_and_os_reach_all_four_formats` | yaml `linux`/`x86_64` and `windows`/`arm64` | 6 defines × 4 files × 2 yamls = 48 values, each 1 or 0 as chosen | A format misses a word |
| `compile_machine_config` `an_unknown_arch_or_os_is_refused` | `arch: riscv64`; `os: macos`; arch missing | 3 refusals, messages "platform.arch must be one of: x86_64,arm64", "platform.os must be one of: linux,windows", "missing key 'platform.arch'" | A word slips through |
| `compile_machine_config` `consumers` (existing, extended) | GCC, nvcc and NASM each compile a file reading `MC_PLATFORM_ARCH_X86_64` | 3 compilers | A language misses it |
| `compile_machine_config` `the_platform_check_matches_cmake` | `cmake -P cmake/platform_check.cmake` over the cross-check table | 4 accepted rows (2 spellings × linux, Linux), 6 refused (each mismatch, Windows, arm64), each with its sentence | A mismatch configures |
| `compile_machine_config` `the_platform_header_refuses_another_machine` | GCC compiles `run/include/types/platform/index.h` with a fake `machine/index.h` | real values: compiles; `ARCH_ARM64 1`: `#error`; `OS_WINDOWS 1`: `#error`; both words 0: `#error` | The header lets a wrong yaml through |
| `asm_conventions` `a_kernel_refuses_a_format_the_yaml_does_not_name` | NASM `-f elf64` with a fake `MC_PLATFORM_OS_WINDOWS 1` | 5 SIMD files, each `%fatal` | The check missing |
| `asm_conventions` `a_win64_object_is_refused_until_the_layer_exists` | NASM `-f win64`, OS windows | 5 SIMD files and `cpuid`, each `%fatal` with the phase 02 sentence | The silent assemble is back |
| `asm_conventions` existing 3 tests | The fake `index.inc` now carries the platform lines | unchanged counts | |
| `compile_standards_lint` fixture | `#include <windows.h>` in a `run/src/` fixture | 1 breach `banned_header:windows` | The ban missing |
| Whole debug suite | Nothing else moved | all pass, or each failure shown on main too | |

## Verification

    tools/test-or-build.sh cmake --preset debug
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(compile_machine_config|asm_conventions|compile_standards_lint)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure -j 2
    pgrep -af "$PWD/build"    # must print nothing

## Gate

Ahmed:
1. Opens `config/machine.yaml` and sees `arch: x86_64` beside `os: linux`.
2. Changes `arch` to `arm64`, runs `tools/test-or-build.sh cmake --preset debug`, and reads the sentence that stops it.
3. Puts it back.
4. Answers Q1: are `MC_PLATFORM_OS_LINUX` and `MC_PLATFORM_ARCH_X86_64` the names he wants? If not, the rename is a
   todo here before phase 02.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Q1: generator names vs the outline's `MC_OS_*`/`MC_ARCH_*` | The generator's (phase 00 Issues found 3) |
| Q2: should a Windows configure pass now? | No: it stops with a sentence until a later plan ports the rest |
| nvcc's device pass may not define `__x86_64__` | Check the header under nvcc in `consumers`. If the device pass lacks it, skip the arch line under `__CUDA_ARCH__` only, and record the evidence |
| `CMAKE_SYSTEM_PROCESSOR` spellings on Windows and arm64 are not tested on real machines | The table's spellings, from CMake's documentation; recorded as unverified until such a machine configures |
| `posix` and `os: windows` contradict | Refuse `os: windows` with `posix: true` in the generator, one more line in `an_unknown_arch_or_os_is_refused` |

## Commits

Appended as they land:

- (none yet)

## Issues found

None.
