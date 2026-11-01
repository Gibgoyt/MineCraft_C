/* Adds the same numbers with NASM and with CUDA, and fails unless both match the known answer. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/* Stage 02.3's proof reads "changing a yaml value changes the binary's reported define". This program is that binary until
 * Stage 03.1 replaces it, so it reports one. The test keeps it building if the stages land in another order. */
#if __has_include("machine/index.h")
#include "machine/index.h"
#define MC_HELLO_REPORTS_MACHINE 1
#endif

enum { MC_HELLO_COUNT = 1024 };

uint64_t mc_hello_simd_sum_batch(const uint32_t *values, uint64_t count);
bool mc_hello_gpu_sum(const uint32_t *values, uint32_t count, uint32_t *out_sum);

int32_t main(int32_t argc, char **argv) {
    (void)argc;
    (void)argv;

    uint32_t values[MC_HELLO_COUNT];
    for (uint32_t i = 0; i < MC_HELLO_COUNT; i++) {
        values[i] = i + 1;
    }
    const uint64_t expected = (uint64_t)MC_HELLO_COUNT * (MC_HELLO_COUNT + 1) / 2;

    const uint64_t simd_sum = mc_hello_simd_sum_batch(values, MC_HELLO_COUNT);
    uint32_t gpu_sum = 0;
    if (!mc_hello_gpu_sum(values, MC_HELLO_COUNT, &gpu_sum)) {
        fprintf(stderr, "minecraft_c hello: the CUDA kernel failed\n");
        return 1;
    }

#ifdef MC_HELLO_REPORTS_MACHINE
    printf("minecraft_c hello: MC_THREADS_WORKERS=%" PRId32 "\n", (int32_t)MC_THREADS_WORKERS);
#endif
    printf("minecraft_c hello: nasm=%" PRIu64 " cuda=%" PRIu32 " expected=%" PRIu64 "\n", simd_sum, gpu_sum, expected);
    return (simd_sum == expected && gpu_sum == expected) ? 0 : 1;
}
