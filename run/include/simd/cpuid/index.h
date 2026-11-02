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

/* The words of one raw record for cpuid_decode_batch: max leaf, max extended leaf, leaf 1 ecx, leaf 7 ebx, extended leaf 1
 * ecx, and the low half of XCR0. */
enum : uint64_t { CPUID_RAW_WORDS = 6u };

/* For each of count records, stores the CPU_FEATURE_* bits it shows in out[i]. The one place that decision is made. */
void cpuid_decode_batch(const uint32_t *raw, uint64_t count, uint32_t *out);

/* Returns if this CPU has every feature in every one of count masks. If not, says so on stderr and ends the process with
 * status 3. It uses no instruction past the x86-64 baseline, which is the reason to call it and not cpu_check_detect. */
void cpuid_guard_batch(const uint32_t *required, uint64_t count);

/* In .preinit_array, so the dynamic loader runs it before anything compiled with -march can: cpuid_guard_batch of
 * cpuid_guard_required. Only named here so that a test can find it there. */
void cpuid_guard_preinit(void);

/* The features config/machine.yaml's cpu.simd requires. One mask. */
extern const uint32_t cpuid_guard_required[1];

#endif
