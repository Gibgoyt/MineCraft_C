/* Prototypes of asm/src/cpuid/index.asm. See its README.md. */
#ifndef MC_RUN_SIMD_CPUID_INDEX_H
#define MC_RUN_SIMD_CPUID_INDEX_H

#include <stdint.h>

/* For each i below count, runs CPUID with eax = leaves[i] and ecx = subleaves[i], and stores eax, ebx, ecx and edx in
 * out[4 * i] onward. So out holds 4 * count values. */
void cpuid_read_batch(const uint32_t *leaves, const uint32_t *subleaves, uint64_t count, uint32_t *out);

/* For each i below count, reads extended control register registers[i] into out[i]. Faults unless CPUID.1:ECX.OSXSAVE
 * is set, so check that first. */
void cpuid_xgetbv_batch(const uint32_t *registers, uint64_t count, uint64_t *out);

#endif
