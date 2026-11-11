#include <stdint.h>
typedef void (*job_function_t)(void *worker, void *borrowed_argument);
void job_spawn(void *worker, void *group, job_function_t function, void *borrowed_argument);
void cg_start(void *worker, uint32_t *depth);

static void cg_tree(void *worker, void *borrowed_argument) {
    uint32_t *depth = borrowed_argument;
    if (*depth > 0u) {
        (*depth)--;
        job_spawn(worker, nullptr, cg_tree, depth);
    }
}

void cg_start(void *worker, uint32_t *depth) {
    job_spawn(worker, nullptr, cg_tree, depth);
}
