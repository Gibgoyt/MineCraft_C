/* Linked only into sanitize builds of programs that also link CUDA. See README.md. */

/* The sanitizer runtime looks this symbol up by name before main. */
const char *__asan_default_options(void);

const char *__asan_default_options(void) {
    /* libcuda maps memory inside ASan's shadow gap, so protecting the gap makes CUDA fail to initialize. */
    return "protect_shadow_gap=0";
}
