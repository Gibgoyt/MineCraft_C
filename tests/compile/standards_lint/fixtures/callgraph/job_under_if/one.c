#include <stdint.h>
typedef void (*job_function_t)(void *worker, void *borrowed_argument);
void job_spawn(void *worker, void *group, job_function_t function, void *borrowed_argument);
void cg_start(void *worker, uint32_t *depth);

/* Code an #if leaves out is not compiled, so the job it would hand over is no job of this build. */
#if 0
static void cg_absent(void *worker, void *borrowed_argument) {
    job_spawn(worker, nullptr, cg_absent, borrowed_argument);
}
#endif

void cg_start(void *worker, uint32_t *depth) {
    (void)worker;
    (*depth)++;
}
