/* See run/src/main/cpu_check/README.md. */
#ifndef MC_RUN_MAIN_CPU_CHECK_INDEX_H
#define MC_RUN_MAIN_CPU_CHECK_INDEX_H

#include <stdint.h>

/* One bit for each key under cpu.simd in config/machine.yaml. */
enum : uint32_t {
    CPU_FEATURE_SSE42 = 1u << 0,
    CPU_FEATURE_AVX = 1u << 1,
    CPU_FEATURE_AVX2 = 1u << 2,
    CPU_FEATURE_FMA3 = 1u << 3,
    CPU_FEATURE_BMI1 = 1u << 4,
    CPU_FEATURE_BMI2 = 1u << 5,
    CPU_FEATURE_POPCNT = 1u << 6,
    CPU_FEATURE_LZCNT = 1u << 7,
    CPU_FEATURE_F16C = 1u << 8,
    CPU_FEATURE_AVX512 = 1u << 9,
    CPU_FEATURE_COUNT = 10u,
};

/* What the decision rests on, as CPUID and XGETBV gave it. */
typedef struct cpu_check_raw_t {
    uint64_t xcr0;              /* XGETBV 0; 0 if OSXSAVE is clear */
    uint32_t max_leaf;          /* CPUID 0, eax */
    uint32_t max_extended_leaf; /* CPUID 0x80000000, eax */
    uint32_t leaf1_ecx;
    uint32_t leaf7_ebx;
    uint32_t extended1_ecx;
    uint8_t _pad0[4];
} cpu_check_raw_t;
static_assert(sizeof(cpu_check_raw_t) == 32);

/* What this build was compiled to use: config/machine.yaml's cpu.simd, plus whatever -march made the compiler free to
 * use on its own. */
uint32_t cpu_check_required(void);

/* Runs the instructions. */
void cpu_check_read_raw(cpu_check_raw_t *out);

/* What the CPU has and the OS has enabled. AVX on a CPU whose OS does not save YMM state is not usable, so it is absent. */
uint32_t cpu_check_decode(const cpu_check_raw_t *raw);

/* cpu_check_decode of cpu_check_read_raw. */
uint32_t cpu_check_detect(void);

/* The yaml's key for a single feature bit, or nullptr. */
const char *cpu_check_feature_name(uint32_t feature);

/* The bit for a yaml key. Returns false for a name that is not one. */
bool cpu_check_feature_from_name(const char *name, uint32_t *out_feature);

/* Writes the names in a mask, space separated, or "none". Returns the length, without the NUL. */
uint64_t cpu_check_names(uint32_t features, char *out, uint64_t capacity);

/* Writes the sentence a person sees when features are missing. Returns the length, without the NUL. */
uint64_t cpu_check_message(uint32_t missing, char *out, uint64_t capacity);

#endif
