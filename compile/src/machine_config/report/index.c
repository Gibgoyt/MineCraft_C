/*
 * Prints the defines this program was compiled with, read from the generated header and not from machine.yaml. It is
 * the build's own proof that a changed machine.yaml reaches compiled code: edit a value, rebuild, and this prints it.
 */
#include "machine/index.h"

#include <stdint.h>
#include <stdio.h>

int32_t main(int32_t argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("MC_PLATFORM_POSIX=%d\n", MC_PLATFORM_POSIX);
    printf("MC_THREADS_WORKERS=%d\n", MC_THREADS_WORKERS);
    printf("MC_CPU_HAS_AVX2=%d\n", MC_CPU_HAS_AVX2);
    printf("MC_GPU_ENABLED=%d\n", MC_GPU_ENABLED);
    printf("MC_CPU_ARCH_FLAG=%s\n", MC_CPU_ARCH_FLAG);
    return 0;
}
