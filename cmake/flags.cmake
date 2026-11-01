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
set(CMAKE_CUDA_ARCHITECTURES 75)
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
# nvcc rejects these three bare ("Unknown option '-fno-exceptions'"), so they go through -Xcompiler.
# Without them the C link fails on __cxa_guard_* and __gxx_personality_v0 (Stage 01.1).
set(MC_CUDA_FLAGS -Xcompiler=-fno-exceptions,-fno-rtti,-fno-threadsafe-statics --Werror=all-warnings)
set(MC_NASM_FLAGS -w+all -Werror)
set(MC_LINK_FLAGS)
# -std=c23 hides POSIX and Linux declarations such as mmap flags, and those are part of what we allow.
set(MC_DEFINES _GNU_SOURCE=1)

if(MC_PRESET STREQUAL "debug")
  list(APPEND MC_C_FLAGS -O0 -g3)
  list(APPEND MC_CUDA_FLAGS -g)
  list(APPEND MC_NASM_FLAGS -g -Fdwarf)
elseif(MC_PRESET STREQUAL "sanitize")
  set(mc_sanitize -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  list(APPEND MC_C_FLAGS -O1 -g3 ${mc_sanitize})
  list(APPEND MC_CUDA_FLAGS -g)
  list(APPEND MC_NASM_FLAGS -g -Fdwarf)
  list(APPEND MC_LINK_FLAGS ${mc_sanitize})
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

# Any linker warning is a failure too. A missing .note.GNU-stack, for one, shows up here.
list(APPEND MC_LINK_FLAGS -Wl,--fatal-warnings -Wl,-z,noexecstack)

add_compile_options(
  "$<$<COMPILE_LANGUAGE:C>:${MC_C_FLAGS}>"
  "$<$<COMPILE_LANGUAGE:CUDA>:${MC_CUDA_FLAGS}>"
  "$<$<COMPILE_LANGUAGE:ASM_NASM>:${MC_NASM_FLAGS}>")
add_compile_definitions("$<$<COMPILE_LANGUAGE:C,CUDA>:${MC_DEFINES}>")
add_link_options(${MC_LINK_FLAGS})
