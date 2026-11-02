/* ctest asm_abi. See README.md. A kernel that clobbers a callee-saved register or returns with dirty YMM state still gives
 * the right bytes, so no comparison with a reference can see either. This looks at the machine state itself.
 *
 * Each kernel is entered through setcontext, with a known value in every callee-saved register. Its ret lands on
 * abi_landing, not back in C code that would have saved and changed those registers, and that reads them before anything
 * else can. */
#include "asm/support/index.h"

#include "math/mat4/index.h"
#include "simd/bitmask/index.h"
#include "simd/blend/index.h"
#include "simd/copy/index.h"
#include "simd/cpuid/index.h"
#include "simd/noise/index.h"
#include "simd/vec_batch/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <ucontext.h>

enum : uint64_t {
    ABI_STACK_WORDS = 8192u,
    ABI_BUFFER_BYTES = 1u << 16,
    ABI_RANDOM_SEED = 0x9E3779B97F4A7C15ull,
    ABI_RBX = 0x1111111111111111ull,
    ABI_RBP = 0x2222222222222222ull,
    ABI_R12 = 0x3333333333333333ull,
    ABI_R13 = 0x4444444444444444ull,
    ABI_R14 = 0x5555555555555555ull,
    ABI_R15 = 0x6666666666666666ull,
    ABI_XINUSE_AVX = 1u << 2,    /* XGETBV register 1: the upper halves of the YMM registers are not all zero */
    ABI_XSAVE_LEAF = 0xDu,       /* CPUID leaf */
    ABI_XSAVE_FEATURES = 1u,     /* and subleaf whose eax says what XSAVE can do */
    ABI_XGETBV1_BIT = 1u << 2,   /* in that eax: XGETBV takes register 1 */
    ABI_ARGUMENTS = 6u,          /* every kernel takes integers and pointers only, so they all fit registers */
    ABI_STACK_LANDING_SLOTS = 2, /* the kernel's return address, and the slot abi_landing reads its run from */
};

/* landed comes first, so that a pointer to the run is a pointer to it. */
typedef struct abi_run_t {
    ucontext_t landed; /* the registers as the kernel's ret left them */
    ucontext_t enter;
    ucontext_t caller;
    uint64_t rbp_landed;
    uint64_t xinuse_landed;
    uint64_t stack[ABI_STACK_WORDS];
    volatile uint64_t left_caller; /* getcontext returns twice, and this is how the second one knows */
} abi_run_t;
static_assert(sizeof(abi_run_t) ==
              3u * sizeof(ucontext_t) + 3u * sizeof(uint64_t) + ABI_STACK_WORDS * sizeof(uint64_t));

/* The rest of abi_landing, out of line so that abi_landing has nothing to keep in a register across its getcontext. */
[[gnu::noinline]] static void abi_landing_finish(const uint64_t *frame) {
    abi_run_t *run = (abi_run_t *)(uintptr_t)frame[1];
    run->rbp_landed = frame[0];
    const uint32_t xinuse_register = 1u;
    cpuid_xgetbv_batch(&xinuse_register, 1u, &run->xinuse_landed);
    (void)setcontext(&run->caller);
}

/* Reached by the kernel's ret, never called. So what it takes for its return address is the stack slot above the kernel's,
 * where abi_call put the run, and rsp is 8 away from where a called function would have it, which nothing here minds.
 *
 * Using the frame address makes GCC push rbp and set up a frame in every preset, so what rbp held is at frame[0]. Nothing
 * here is held across the getcontext, so no callee-saved register is touched before it reads them. */
[[gnu::noinline, gnu::no_sanitize("address")]] static void abi_landing(void) {
    (void)getcontext((ucontext_t *)__builtin_return_address(0));
    abi_landing_finish((const uint64_t *)__builtin_frame_address(0));
}

static void abi_call(abi_run_t *run, uintptr_t kernel, const uint64_t *arguments) {
    /* Where the ABI says a function starts: the stack aligned to 16 before the call pushed its 8 bytes. */
    const uintptr_t top = (uintptr_t)&run->stack[ABI_STACK_WORDS - ABI_STACK_LANDING_SLOTS] & ~(uintptr_t)15u;
    uint64_t *stack = (uint64_t *)(top - sizeof(uint64_t));
    stack[0] = (uint64_t)(uintptr_t)abi_landing;
    stack[1] = (uint64_t)(uintptr_t)run;

    (void)getcontext(&run->enter);
    greg_t *registers = run->enter.uc_mcontext.gregs;
    registers[REG_RIP] = (greg_t)kernel;
    registers[REG_RSP] = (greg_t)(uintptr_t)stack;
    registers[REG_RDI] = (greg_t)arguments[0];
    registers[REG_RSI] = (greg_t)arguments[1];
    registers[REG_RDX] = (greg_t)arguments[2];
    registers[REG_RCX] = (greg_t)arguments[3];
    registers[REG_R8] = (greg_t)arguments[4];
    registers[REG_R9] = (greg_t)arguments[5];
    registers[REG_RBX] = (greg_t)ABI_RBX;
    registers[REG_RBP] = (greg_t)ABI_RBP;
    registers[REG_R12] = (greg_t)ABI_R12;
    registers[REG_R13] = (greg_t)ABI_R13;
    registers[REG_R14] = (greg_t)ABI_R14;
    registers[REG_R15] = (greg_t)ABI_R15;

    run->left_caller = 0u;
    (void)getcontext(&run->caller);
    if (run->left_caller == 0u) {
        run->left_caller = 1u;
        (void)setcontext(&run->enter);
    }
}

static void abi_check(test_context_t *t, abi_run_t *run, const char *name, uintptr_t kernel,
                      const uint64_t *arguments) {
    abi_call(run, kernel, arguments);
    const greg_t *registers = run->landed.uc_mcontext.gregs;
    const uint64_t failures_before = t->failure_count;
    CHECK_EQ_U64((uint64_t)registers[REG_RBX], ABI_RBX);
    CHECK_EQ_U64(run->rbp_landed, ABI_RBP);
    CHECK_EQ_U64((uint64_t)registers[REG_R12], ABI_R12);
    CHECK_EQ_U64((uint64_t)registers[REG_R13], ABI_R13);
    CHECK_EQ_U64((uint64_t)registers[REG_R14], ABI_R14);
    CHECK_EQ_U64((uint64_t)registers[REG_R15], ABI_R15);
    CHECK_EQ_U64(run->xinuse_landed & ABI_XINUSE_AVX, 0u);
    if (t->failure_count != failures_before) {
        (void)printf("  in %s, with a count of %" PRIu64 "\n", name, arguments[ABI_ARGUMENTS]);
    }
}

/* Registers in the order of the kernel's parameters, then the count for abi_check to name if it fails. */
#define ABI_CHECK(kernel, count, a0, a1, a2, a3, a4, a5)                                                               \
    do {                                                                                                               \
        const uint64_t abi_arguments[ABI_ARGUMENTS + 1u] = {                                                           \
            (uint64_t)(a0), (uint64_t)(a1), (uint64_t)(a2), (uint64_t)(a3), (uint64_t)(a4), (uint64_t)(a5), (count)};  \
        abi_check(t, run, #kernel, (uintptr_t)kernel, abi_arguments);                                                  \
    } while (false)

TEST(this_cpu_can_report_dirty_ymm_state) {
    const uint32_t leaf = ABI_XSAVE_LEAF;
    const uint32_t subleaf = ABI_XSAVE_FEATURES;
    uint32_t out[4] = {0};
    cpuid_read_batch(&leaf, &subleaf, 1u, out);
    CHECK((out[0] & ABI_XGETBV1_BIT) != 0u);
}

TEST(every_kernel_keeps_the_callee_saved_registers_and_leaves_no_dirty_ymm) {
    abi_run_t *run = test_alloc(t, sizeof(abi_run_t));
    uint8_t *a = test_alloc(t, ABI_BUFFER_BYTES);
    uint8_t *b = test_alloc(t, ABI_BUFFER_BYTES);
    uint8_t *c = test_alloc(t, ABI_BUFFER_BYTES);
    uint8_t *out = test_alloc(t, ABI_BUFFER_BYTES);
    uint64_t random_state = ABI_RANDOM_SEED;
    test_random_fill(&random_state, a, ABI_BUFFER_BYTES);
    test_random_fill(&random_state, b, ABI_BUFFER_BYTES);
    test_random_fill(&random_state, c, ABI_BUFFER_BYTES);
    mat4_t m;
    mat4_identity(&m);
    const uint64_t pointer_m = (uint64_t)(uintptr_t)&m;
    const uint64_t pa = (uint64_t)(uintptr_t)a;
    const uint64_t pb = (uint64_t)(uintptr_t)b;
    const uint64_t pc = (uint64_t)(uintptr_t)c;
    const uint64_t po = (uint64_t)(uintptr_t)out;

    /* Empty, all tail, a batch and a tail, and past the first widening of the popcount. */
    const uint64_t counts[] = {0u, 3u, 9u, 200u};
    for (uint64_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
        const uint64_t n = counts[i];
        ABI_CHECK(copy_stream_batch, n, po, pa, n, 0, 0, 0);
        ABI_CHECK(copy_stream_rows_batch, n, po, 256u, pa, 256u, n, 3u);
        ABI_CHECK(blend_over_batch, n, po, pa, n, 0, 0, 0);
        ABI_CHECK(blend_premul_batch, n, po, pa, n, 0, 0, 0);
        ABI_CHECK(bitmask_popcount_batch, n, pa, n, 0, 0, 0, 0);
        ABI_CHECK(bitmask_and_batch, n, po, pa, pb, n, 0, 0);
        ABI_CHECK(bitmask_or_batch, n, po, pa, pb, n, 0, 0);
        ABI_CHECK(bitmask_andnot_batch, n, po, pa, pb, n, 0, 0);
        ABI_CHECK(bitmask_from_nonzero_u8_batch, n, pa, n, po, 0, 0, 0);
        ABI_CHECK(bitmask_from_nonzero_u16_batch, n, pa, n, po, 0, 0, 0);
        ABI_CHECK(bitmask_from_equal_u16_batch, n, pa, n, 7u, po, 0, 0);
        ABI_CHECK(noise3_batch8, n, po, pa, pb, pc, n, 0x51D7A3C9u);
        ABI_CHECK(vec_batch_transform_points, n, po, pa, n, pointer_m, 0, 0);
        ABI_CHECK(vec_batch_transform_directions, n, po, pa, n, pointer_m, 0, 0);
        ABI_CHECK(vec_batch_mul_vec4, n, po, pa, n, pointer_m, 0, 0);
    }
}

TEST_MAIN()
