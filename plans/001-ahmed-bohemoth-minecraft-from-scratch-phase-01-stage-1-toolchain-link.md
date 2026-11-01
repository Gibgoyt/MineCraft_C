# 001-ahmed-bohemoth-minecraft-from-scratch — phase 1, stage 1: Toolchain link spike

**Status:** done   (updated 2026-11-01)

## Goal

We know whether CMake links C, NASM and CUDA through a `main.c`, whether `-fno-exceptions -fno-rtti -fno-threadsafe-statics` remove the C++ runtime symbols, and whether cudart should be static or shared. Pass: a binary runs a kernel and the exact link list is recorded for the coding standard.

**Wave:** W1. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-feasibility-spikes.md`.

**Owned paths** (edit only these, plus this file):

- `/tmp/mc_c_spikes/toolchain_link/` (throwaway)
- this stage file

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create a throwaway CMake project in `/tmp/mc_c_spikes/toolchain_link/` with languages C, ASM_NASM and CUDA
- [x] Link `main.c`, one NASM `.o` and one `.cu` kernel built for `sm_75`, and run the kernel
- [x] Run `nm -u` on the CUDA object with and without `-fno-exceptions -fno-rtti -fno-threadsafe-statics`, and record the symbols that remain
- [x] Try static and shared cudart, and record what each needs at link and run time
- [x] Try `_Float32` in device code, to settle the CUDA `float32_t` mapping
- [x] Record the exact link list in this file for `plans/CODING_STANDARD.md`

## Verification

Accepted exception (plan section 6): `tools/test-or-build.sh` does not exist until Stage 02.1, so these commands are not wrapped.

    cmake -S /tmp/mc_c_spikes/toolchain_link -B /tmp/mc_c_spikes/toolchain_link/build
    cmake --build /tmp/mc_c_spikes/toolchain_link/build
    timeout 60 /tmp/mc_c_spikes/toolchain_link/build/spike
    pgrep -a spike    # must print nothing

Verification result, from a clean `build/` on 2026-11-01 (GCC 16.2.1, nvcc 13.4.92, NASM 3.02, CMake 4.4.3, driver 615.71.09, RTX 2080 Super):

    spike: kernel+nasm sum=524800 expect=524800 scale_last=512.0 expect=512.0
    spike: abi_mix=3.75 expect=3.75
    exit=0
    pgrep -a spike: printed nothing (rc 1)

The same binary built with `-O3 -flto -march=skylake -mtune=skylake -fno-tree-vectorize -fno-tree-slp-vectorize` for C, `-O3` for CUDA and `-lm` added also exits 0, with `NEEDED` of `libm.so.6`, `libc.so.6` and `ld-linux-x86-64.so.2`. GPU memory use read 270 MiB before the runs and 270 MiB after one of them.

## Results

**Answers.** CMake links C, NASM and CUDA through `main.c`. The flags remove all C++ runtime symbols. cudart should be **static**, and the binary then has no C++ runtime at all.

**The link list:**

    gcc <nasm objects> <c objects> <cuda objects> -L/opt/cuda/lib64 -lcudart_static -lm

`-lm` is for our own code; the spike runs without it. The CMake form is in `plans/CODING_STANDARD.md` section 9.

**`nm -u` on the CUDA object**, showing C++ runtime symbols and `__stack_chk_fail` only:

| `-Xcompiler=` flags | Remaining |
|---|---|
| none | `__cxa_guard_abort __cxa_guard_acquire __cxa_guard_release __gxx_personality_v0 __stack_chk_fail _Unwind_Resume` |
| `-fno-exceptions` | `__cxa_guard_acquire __cxa_guard_release __stack_chk_fail` |
| `-fno-rtti` | `__cxa_guard_abort __cxa_guard_acquire __cxa_guard_release __gxx_personality_v0 __stack_chk_fail _Unwind_Resume` |
| `-fno-threadsafe-statics` | `__stack_chk_fail` |
| all three | `__stack_chk_fail` |
| all three plus `-fno-stack-protector` | nothing |

`__stack_chk_fail` is in glibc, so `-fno-stack-protector` is not needed and is not in the standard. With all three flags, the complete list is:

    atexit exit __stack_chk_fail
    cudaDeviceSynchronize cudaFree cudaGetErrorString cudaGetLastError cudaMalloc cudaMemcpy
    __cudaGetKernel __cudaLaunchKernel __cudaPopCallConfiguration __cudaPushCallConfiguration
    __cudaRegisterFatBinary __cudaRegisterFatBinaryEnd __cudaRegisterFunction __cudaUnregisterFatBinary

**Static against shared cudart**, linked directly with gcc. Every row that linked ran the kernel and exited 0:

| Kernel object | Link libraries | Link | Binary `NEEDED` | Size |
|---|---|---|---|---|
| three flags | `-lcudart_static` | ok | `libc`, `ld-linux` | 1101648 |
| three flags | `-lcudart_static -ldl -lrt -lpthread -lm` | ok | `libm`, `libc`, `ld-linux` | 1101648 |
| three flags | same, plus `-Wl,--gc-sections` | ok | `libm`, `libc`, `ld-linux` | 1101432 |
| no flags | `-lcudart_static -ldl -lrt -lpthread -lm` | **fails**: undefined `__cxa_guard_*` (3) and `__gxx_personality_v0` | | |
| three flags | `-lcudart` with rpath | ok | `libcudart.so.13`, `libc` | 23496 |
| no flags | `-lcudart` with rpath | **fails**: `__cxa_guard_acquire@@CXXABI_1.3`, DSO missing from command line | | |
| no flags | `-lcudart -lstdc++` with rpath | ok | `libcudart.so.13`, `libstdc++`, `libgcc_s`, `libc` | 23936 |

Shared needs more at run time than its `NEEDED` line shows. `libcudart.so` itself depends on `libstdc++.so.6` and `libgcc_s.so.1` (`ldd`). It also has to be found by the loader: on this machine `/etc/ld.so.conf.d/cuda.conf` registers it and `LD_LIBRARY_PATH` names it too, so the rpath in these rows was not what made them run. The static rows need none of this.

Static is what the standard's "no C++ runtime" rule needs. `LD_DEBUG=files` on the final static binary shows every library it loads: `libc`, `libm`, `libdl`, `libpthread`, `librt`, plus `libcuda.so.1` (opened by the binary) and `libnvcuextend.so` (opened by `libcuda.so.1`). Neither `libstdc++` nor `libgcc_s` appears. It still needs the NVIDIA driver's `libcuda.so.1`, like any CUDA program.

**What CMake produces**, all with the three-flag object and all exiting 0:

| `LINKER_LANGUAGE` | cudart | Implicit CUDA libraries cleared | Binary `NEEDED` |
|---|---|---|---|
| C | static | yes (**chosen**) | `libc`, `ld-linux` |
| C | static | no | `libstdc++`, `libm`, `libc`, `ld-linux` |
| C | shared | yes | `libcudart.so.13`, `libc` |
| CUDA (g++ links) | static | no | `libstdc++`, `libm`, `libgcc_s`, `libc`, `ld-linux` |
| CUDA (g++ links) | shared | no | `libcudart.so.13`, `libstdc++`, `libm`, `libgcc_s`, `libc` |

**`_Float32` in device code:** does not work; see the issue below. The mapping is `float` and `double` under `__CUDACC__`, now in `plans/CODING_STANDARD.md` section 2. It was run across the boundary: C passes `1.5f32` and `2.25f64` by value and receives `3.75`, and kernel output written through a `float32_t *` reads back as `512.0` in C.

The spike sources and the matrix scripts stay in `/tmp/mc_c_spikes/toolchain_link/` (`link_matrix.sh`, `flag_matrix.sh`, `cmake_matrix.sh`).

## Commits

Appended as they land:

## Issues found

**1. `_Float32` and `_Float64` cannot be used in device code.**
- Expected: possibly usable, as the standard left this open.
- Actual: a hard error from nvcc 13.4.92, with or without a literal suffix: `kernel.cu(10): error: "float32_t" (aka "std::float32_t") is a C++23 extended floating-point, which is not supported in device code`.
- Done: resolved by the `__CUDACC__` mapping to `float` and `double` in `plans/CODING_STANDARD.md` section 2.

**2. `f32` and `f64` literals crash nvcc in device arithmetic, even on plain `float`.**
- Expected: the suffix to work once the typedef was `float`, because a literal passed straight to a `float` parameter did compile.
- Actual: with `(0.5f32 + 0.5f32)` in the kernel, `nvcc -std=c++20 -arch=sm_75 -DSPIKE_LITERAL_F32 -c kernel.cu` prints `Error: Internal Compiler Error (codegen): "unsupported float variant!"` and returns 2. `-DSPIKE_LITERAL_F64` gives the same result.
- Done: worked around. The master plan and the standard had said `1.0f32` everywhere, so the standard now has separate literal rules for C, for CUDA, and an `F32()`/`F64()` macro pair for shared headers. The macros were built and run on both sides. Stage 03.1 (`run/include/types/`) needs them, and `standards_lint` (Stage 02.4) should ban `f32`/`f64` suffixes in `.cu` and `.hu`. The master plan is left as written, per README rule 1.

**3. `printf("%f", x)` fails on `_Float64` under `-Werror`.**
- Expected: `float64_t` to pass as a `double`, since the standard's probe only covered libm calls.
- Actual: `main.c:25:31: error: format '%f' expects argument of type 'double', but argument 2 has type 'float64_t' {aka '_Float64'} [-Werror=format=]`.
- Done: the typedef site now also defines `libc_double_t`, for use only at varargs calls, and `plans/CODING_STANDARD.md` section 2 says so. Built and run under the standard's warning flags. It is a third float name at the single typedef site; the alternative would be `-Wno-format`, which would lose format checking everywhere.

**4. nvcc rejects the flags as the plan wrote them.**
- Expected: `-fno-exceptions -fno-rtti -fno-threadsafe-statics` to be accepted by nvcc.
- Actual: `nvcc fatal   : Unknown option '-fno-exceptions'`. Both `-Xcompiler=-fno-exceptions,-fno-rtti,-fno-threadsafe-statics` and `-forward-unknown-to-host-compiler` (which CMake adds to its nvcc lines) work, with zero C++ runtime symbols in each object.
- Done: the standard uses the `-Xcompiler=` form, which works with or without CMake.

**5. CMake adds `-lstdc++` to a C-linked target holding CUDA objects.**
- Expected: `LINKER_LANGUAGE C` to give a C link line.
- Actual: `link.txt` ended in `-lcudart_static -lcudadevrt -lcudart_static -lrt -lpthread -ldl -lstdc++ -lm`, and the binary had `NEEDED libstdc++.so.6`, although nothing used it.
- Done: clearing `CMAKE_CUDA_IMPLICIT_LINK_LIBRARIES` and `CMAKE_CUDA_IMPLICIT_LINK_DIRECTORIES` after `project()` removes it. Recorded in section 9 for Stage 02.1. This relies on CMake internals, so Stage 02.1 should assert it with a `readelf -d` check that fails the build if `libstdc++` appears.

**6. Measurement error in my own first matrix run, corrected.**
- Expected: the `exit:` lines of `link_matrix.sh` to report the spike's exit code.
- Actual: they reported `sed`'s, because the run was piped through it.
- Done: fixed the script and re-ran. The results above come from the corrected run.

**7. Spike location differs from the workflow's instruction.**
- The workflow task said to build only inside the worktree; this stage's owned paths and Verification name `/tmp/mc_c_spikes/toolchain_link/`. I followed the stage file, since the spike is meant to stay out of the repo. Nothing was built in the worktree.

**Not measured:** other nvcc versions; a kernel with its own statics or exceptions; static-cudart behaviour on any other machine. A process using cudart under the KMS path is for Stages 01.4 and 05. The phase file's Stage 01.1 checkbox is left for the phase owner, since that file is outside this stage's owned paths.
