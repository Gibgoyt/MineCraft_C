/* See README.md. */
#include "main/cpu_check/index.h"

#include "machine/index.h"
#include "simd/cpuid/index.h"

#include <stdio.h>
#include <string.h>

/* Intel SDM volume 2, the CPUID instruction. */
enum : uint32_t {
    LEAF1_ECX_OSXSAVE = 1u << 27,
    LEAF_EXTENDED_MAX = 0x80000000u,
    LEAF_EXTENDED_FEATURES = 0x80000001u,
};

/* In bit order, so a bit's index finds its name. These are the keys under cpu.simd in config/machine.yaml. */
static const char *const cpu_check_feature_names[CPU_FEATURE_COUNT] = {
    "sse42", "avx", "avx2", "fma3", "bmi1", "bmi2", "popcnt", "lzcnt", "f16c", "avx512",
};

uint32_t cpu_check_required(void) {
    uint32_t required = 0;
#if MC_CPU_HAS_SSE42 || defined(__SSE4_2__)
    required |= CPU_FEATURE_SSE42;
#endif
#if MC_CPU_HAS_AVX || defined(__AVX__)
    required |= CPU_FEATURE_AVX;
#endif
#if MC_CPU_HAS_AVX2 || defined(__AVX2__)
    required |= CPU_FEATURE_AVX2;
#endif
#if MC_CPU_HAS_FMA3 || defined(__FMA__)
    required |= CPU_FEATURE_FMA3;
#endif
#if MC_CPU_HAS_BMI1 || defined(__BMI__)
    required |= CPU_FEATURE_BMI1;
#endif
#if MC_CPU_HAS_BMI2 || defined(__BMI2__)
    required |= CPU_FEATURE_BMI2;
#endif
#if MC_CPU_HAS_POPCNT || defined(__POPCNT__)
    required |= CPU_FEATURE_POPCNT;
#endif
#if MC_CPU_HAS_LZCNT || defined(__LZCNT__)
    required |= CPU_FEATURE_LZCNT;
#endif
#if MC_CPU_HAS_F16C || defined(__F16C__)
    required |= CPU_FEATURE_F16C;
#endif
#if MC_CPU_HAS_AVX512 || defined(__AVX512F__)
    required |= CPU_FEATURE_AVX512;
#endif
    return required;
}

void cpu_check_read_raw(cpu_check_raw_t *out) {
    enum { QUERY_COUNT = 5, REGS_PER_QUERY = 4, EAX = 0, EBX = 1, ECX = 2 };
    /* A leaf past the maximum returns another leaf's data, not an error, so cpu_check_decode looks at the maximums. */
    static const uint32_t leaves[QUERY_COUNT] = {0u, 1u, 7u, LEAF_EXTENDED_MAX, LEAF_EXTENDED_FEATURES};
    static const uint32_t subleaves[QUERY_COUNT] = {0u, 0u, 0u, 0u, 0u};
    uint32_t regs[QUERY_COUNT * REGS_PER_QUERY];
    cpuid_read_batch(leaves, subleaves, QUERY_COUNT, regs);

    *out = (cpu_check_raw_t){
        .xcr0 = 0,
        .max_leaf = regs[0 * REGS_PER_QUERY + EAX],
        .max_extended_leaf = regs[3 * REGS_PER_QUERY + EAX],
        .leaf1_ecx = regs[1 * REGS_PER_QUERY + ECX],
        .leaf7_ebx = regs[2 * REGS_PER_QUERY + EBX],
        .extended1_ecx = regs[4 * REGS_PER_QUERY + ECX],
        ._pad0 = {0},
    };
    if ((out->leaf1_ecx & LEAF1_ECX_OSXSAVE) != 0) {
        static const uint32_t control_registers[1] = {0u};
        cpuid_xgetbv_batch(control_registers, 1u, &out->xcr0);
    }
}

/* The bits themselves are read in NASM, because the early guard has to make the same decision before any C can run, and
 * one copy of it cannot disagree with itself. XCR0's high half holds no feature this looks at. */
uint32_t cpu_check_decode(const cpu_check_raw_t *raw) {
    const uint32_t flat[CPUID_RAW_WORDS] = {
        raw->max_leaf, raw->max_extended_leaf, raw->leaf1_ecx, raw->leaf7_ebx, raw->extended1_ecx, (uint32_t)raw->xcr0,
    };
    uint32_t present = 0;
    cpuid_decode_batch(flat, 1u, &present);
    return present;
}

uint32_t cpu_check_detect(void) {
    cpu_check_raw_t raw;
    cpu_check_read_raw(&raw);
    return cpu_check_decode(&raw);
}

const char *cpu_check_feature_name(uint32_t feature) {
    for (uint32_t i = 0; i < CPU_FEATURE_COUNT; i++) {
        if (feature == 1u << i) {
            return cpu_check_feature_names[i];
        }
    }
    return nullptr;
}

bool cpu_check_feature_from_name(const char *name, uint32_t *out_feature) {
    for (uint32_t i = 0; i < CPU_FEATURE_COUNT; i++) {
        if (strcmp(name, cpu_check_feature_names[i]) == 0) {
            *out_feature = 1u << i;
            return true;
        }
    }
    return false;
}

/* Adds text at offset, keeps the result NUL terminated, and cuts instead of overrunning. Returns the new offset. */
static uint64_t cpu_check_append(char *out, uint64_t capacity, uint64_t offset, const char *text) {
    if (offset >= capacity) {
        return offset;
    }
    const uint64_t room = capacity - offset - 1u;
    const uint64_t length = strlen(text);
    const uint64_t count = length < room ? length : room;
    memcpy(out + offset, text, count);
    out[offset + count] = '\0';
    return offset + count;
}

uint64_t cpu_check_names(uint32_t features, char *out, uint64_t capacity) {
    uint64_t offset = cpu_check_append(out, capacity, 0, "");
    bool first = true;
    for (uint32_t i = 0; i < CPU_FEATURE_COUNT; i++) {
        if ((features & (1u << i)) != 0) {
            if (!first) {
                offset = cpu_check_append(out, capacity, offset, " ");
            }
            offset = cpu_check_append(out, capacity, offset, cpu_check_feature_names[i]);
            first = false;
        }
    }
    return first ? cpu_check_append(out, capacity, offset, "none") : offset;
}

uint64_t cpu_check_message(uint32_t missing, char *out, uint64_t capacity) {
    char names[128];
    cpu_check_names(missing, names, sizeof(names));
    const int32_t wanted =
        snprintf(out, capacity,
                 "this CPU does not support what this build was compiled to use. Missing: %s. Build flags: "
                 "-march=%s and cpu.simd in config/machine.yaml. Change them to match this CPU and rebuild.",
                 names, MC_CPU_ARCH_FLAG);
    if (wanted <= 0 || capacity == 0) {
        return 0;
    }
    return (uint64_t)wanted < capacity ? (uint64_t)wanted : capacity - 1u;
}
