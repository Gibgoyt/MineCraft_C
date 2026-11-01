# cmake/sanitize

**Purpose:** gives every sanitized program that links CUDA the ASan options CUDA needs, so nobody has to remember
`ASAN_OPTIONS` by hand.
**Tier:** Borrow. Holds no state.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** called once by the sanitizer runtime before `main`.
**Public functions:** `__asan_default_options`, returns the option string.
**May be called by:** the ASan runtime only.
**Depends on:** nothing. Built only in the `sanitize` preset.
