# Flags per language and preset: plans/CODING_STANDARD.md sections 8 and 9.

# Comes from config/machine.yaml (cpu.arch_flag) once cmake/machine.cmake exists (Stage 02.3).
if(NOT DEFINED MC_CPU_ARCH_FLAG)
  set(MC_CPU_ARCH_FLAG skylake)
endif()

# -std=c23 and -std=c++20 exactly, with no GNU dialect.
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C23_STANDARD_COMPILE_OPTION -std=c23)   # CMake would otherwise pass the old spelling, -std=c2x
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)
# config/machine.yaml's gpu.architecture, such as sm_75, which CMake wants without the sm_. Stage 02.3 generates it.
if(DEFINED MC_GPU_ARCHITECTURE)
  if(NOT MC_GPU_ARCHITECTURE MATCHES "^sm_([0-9]+)$")
    message(FATAL_ERROR "config/machine.yaml: gpu.architecture is '${MC_GPU_ARCHITECTURE}', expected sm_<number>")
  endif()
  set(CMAKE_CUDA_ARCHITECTURES ${CMAKE_MATCH_1})
else()
  set(CMAKE_CUDA_ARCHITECTURES 75)
endif()
set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)

# Stage 01.1: static cudart, or the binary depends on libstdc++ and libgcc_s through libcudart.so.
set(CMAKE_CUDA_RUNTIME_LIBRARY Static)
# Stage 01.1: must come after project(). Otherwise CMake adds -lstdc++ to any target holding CUDA
# objects, even one linked as C. cmake/check_link.cmake fails the build if this ever stops working.
set(CMAKE_CUDA_IMPLICIT_LINK_LIBRARIES "")
set(CMAKE_CUDA_IMPLICIT_LINK_DIRECTORIES "")

# The presets own every optimisation flag, so CMake's per-config defaults (-O3 -DNDEBUG, -g) must not
# be appended behind them.
foreach(mc_lang C CUDA ASM_NASM)
  foreach(mc_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_${mc_lang}_FLAGS_${mc_config} "")
  endforeach()
endforeach()

set(MC_C_FLAGS
  -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wdouble-promotion -Wshadow -Wvla -Wpadded -Werror
  -march=${MC_CPU_ARCH_FLAG} -mtune=${MC_CPU_ARCH_FLAG}
  -fno-tree-vectorize -fno-tree-slp-vectorize)
# plans/CODING_STANDARD.md section 4: thread stacks are small and fixed, and large data goes in an arena. The value is
# Stage 02.4's choice; the standard names the flag but no number.
set(MC_FRAME_LIMIT_BYTES 16384)
# ASan's red zones are its own and not this program's: app_t, 15104 bytes on main's stack, took a frame of 18112 there. Every other preset holds to
# the number above, and they are what says whether this code fits.
if(MC_PRESET STREQUAL "sanitize" OR MC_PRESET STREQUAL "tsan")
  set(MC_FRAME_LIMIT_BYTES 32768)
endif()
list(APPEND MC_C_FLAGS -Wframe-larger-than=${MC_FRAME_LIMIT_BYTES})
# nvcc rejects these three bare ("Unknown option '-fno-exceptions'"), so they go through -Xcompiler.
# Without them the C link fails on __cxa_guard_* and __gxx_personality_v0 (Stage 01.1).
set(MC_CUDA_FLAGS -Xcompiler=-fno-exceptions,-fno-rtti,-fno-threadsafe-statics --Werror=all-warnings)
set(MC_NASM_FLAGS -w+all -Werror)
set(MC_LINK_FLAGS)
# -std=c23 hides POSIX and Linux declarations such as mmap flags, and those are part of what we allow.
set(MC_DEFINES _GNU_SOURCE=1)

if(MC_PRESET STREQUAL "debug")
  list(APPEND MC_C_FLAGS -O0 -g3)
  set(MC_C_CALLGRAPH ON)   # cmake/standards_lint.cmake reads this GCC output to find recursion
  list(APPEND MC_CUDA_FLAGS -g)
  list(APPEND MC_NASM_FLAGS -g -Fdwarf)
elseif(MC_PRESET STREQUAL "sanitize")
  set(mc_sanitize -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  # ASan adds red zones round every local, and app_t on main's stack (section 4) is already most of the limit: measured
  # 18112 bytes against 16384, Stage 15.3. The other presets are what hold the limit; the last flag given is the one that counts.
  list(APPEND MC_C_FLAGS -O1 -g3 ${mc_sanitize} -Wframe-larger-than=32768)
  set(MC_C_CALLGRAPH ON)
  list(APPEND MC_CUDA_FLAGS -g)
  list(APPEND MC_NASM_FLAGS -g -Fdwarf)
  list(APPEND MC_LINK_FLAGS ${mc_sanitize})
elseif(MC_PRESET STREQUAL "tsan")
  # Finds what threads do to each other's memory. What is written in NASM or runs on the card is not watched: neither shares anything but through C.
  set(mc_tsan -fsanitize=thread -fno-omit-frame-pointer)
  list(APPEND MC_C_FLAGS -O1 -g3 ${mc_tsan} -Wframe-larger-than=32768)
  set(MC_C_CALLGRAPH ON)
  list(APPEND MC_CUDA_FLAGS -g)
  list(APPEND MC_NASM_FLAGS -g -Fdwarf)
  list(APPEND MC_LINK_FLAGS ${mc_tsan})
elseif(MC_PRESET STREQUAL "release")
  # =auto is -flto with parallel LTRANS jobs. Code generation happens at the link, so the link has to
  # repeat the flags that shape it. No -ffast-math in any preset: same seed, same world.
  set(mc_codegen -O3 -flto=auto -march=${MC_CPU_ARCH_FLAG} -mtune=${MC_CPU_ARCH_FLAG}
    -fno-tree-vectorize -fno-tree-slp-vectorize)
  list(APPEND MC_C_FLAGS ${mc_codegen})
  list(APPEND MC_CUDA_FLAGS -O3)
  list(APPEND MC_LINK_FLAGS ${mc_codegen})
  list(APPEND MC_DEFINES NDEBUG=1)
elseif(MC_PRESET STREQUAL "profile")
  # Release code, so timings mean something, plus symbols and walkable stacks.
  set(mc_codegen -O3 -g -flto=auto -fno-omit-frame-pointer -march=${MC_CPU_ARCH_FLAG} -mtune=${MC_CPU_ARCH_FLAG}
    -fno-tree-vectorize -fno-tree-slp-vectorize)
  list(APPEND MC_C_FLAGS ${mc_codegen})
  list(APPEND MC_CUDA_FLAGS -O3 -g)
  list(APPEND MC_NASM_FLAGS -g -Fdwarf)
  list(APPEND MC_LINK_FLAGS ${mc_codegen})
  list(APPEND MC_DEFINES NDEBUG=1 MC_PROFILE=1)
endif()

# Under -flto code is generated at the link, so anything the optimizer warns about, and the frame limit above all, can
# only fire there, and only if the link is given it.
if(MC_PRESET MATCHES "^(release|profile)$")
  list(APPEND MC_LINK_FLAGS -Wframe-larger-than=${MC_FRAME_LIMIT_BYTES} -Werror)
endif()

# GCC writes no call graph for -flto objects at compile time, so only the presets without LTO get one from the real
# objects. The others compile the same sources again for it; see cmake/standards_lint.cmake.
if(MC_C_CALLGRAPH)
  list(APPEND MC_C_FLAGS -fcallgraph-info=su)
endif()

# For those extra objects. They come after the preset's flags, so they win over -O3 -flto. Everything else stays, the
# defines above all, so code under #ifdef NDEBUG is what gets checked.
function(mc_callgraph_compile_options target)
  target_compile_options(${target} PRIVATE -O0 -fno-lto -fcallgraph-info=su)
endfunction()

# Any linker warning is a failure too. A missing .note.GNU-stack, for one, shows up here.
list(APPEND MC_LINK_FLAGS -Wl,--fatal-warnings -Wl,-z,noexecstack)

add_compile_options(
  "$<$<COMPILE_LANGUAGE:C>:${MC_C_FLAGS}>"
  "$<$<COMPILE_LANGUAGE:CUDA>:${MC_CUDA_FLAGS}>"
  "$<$<COMPILE_LANGUAGE:ASM_NASM>:${MC_NASM_FLAGS}>")
add_compile_definitions("$<$<COMPILE_LANGUAGE:C,CUDA>:${MC_DEFINES}>")
add_link_options(${MC_LINK_FLAGS})
