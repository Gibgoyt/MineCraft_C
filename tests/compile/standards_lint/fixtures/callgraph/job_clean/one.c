#include <stdint.h>
typedef void (*job_function_t)(void *worker, void *borrowed_argument);
void job_spawn(void *worker, void *group, job_function_t function, void *borrowed_argument);
void cg_start(void *worker, uint32_t *depth);

static void cg_leaf(void *worker, void *borrowed_argument) {
    (void)worker;
    uint32_t *depth = borrowed_argument;
    (*depth)++;
}

static void cg_other(void *worker, void *borrowed_argument) {
    (void)worker;
    (void)borrowed_argument;
}

void cg_start(void *worker, uint32_t *depth) {
    job_spawn(worker, nullptr, *depth > 0u ? cg_leaf : cg_other, depth);
}
