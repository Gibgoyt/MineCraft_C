/* A job system as small as can be: job_run calls the job through its pointer. */
typedef void (*job_function_t)(void *worker, void *borrowed_argument);
void job_spawn(void *worker, void *group, job_function_t function, void *borrowed_argument);

static void job_run(void *worker, job_function_t function, void *borrowed_argument) {
    function(worker, borrowed_argument);
}

/* Runs the job at once on this stack, as job_spawn once did when every queue was full. */
void job_spawn(void *worker, void *group, job_function_t function, void *borrowed_argument) {
    (void)group;
    job_run(worker, function, borrowed_argument);
}
