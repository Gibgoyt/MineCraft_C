# run/src/math/det

**Purpose:** deterministic float64 maths for anything that decides a block (plan 006 master 2.3): `det_exp`, `det_sin`,
`det_cos`, `det_asin`, `det_atan2`. `run/src/terrain/ore/README.md` and `run/src/terrain/fields/README.md` say why: nothing
that decides a block may lean on how some libm rounds, and glibc's transcendental functions promise no bits across versions.
These are range-reduced polynomials with fixed hexadecimal coefficients, evaluated by Horner's rule in a written order.
**Tier:** Arena by depth. It allocates nothing.
**Owns:** nothing.
**Lifetime:** Values only.
**Threads:** any. No shared state.
**Public functions:**

- `det_exp`: `x = k ln 2 + r` (Cody-Waite, two-part ln 2), `|r| <= ln 2 / 2`, `e^r` by the degree-13 Taylor polynomial, then
  times `2^k` built from its bits, in two halves so a subnormal result rounds once. Relative error under 1e-12 over
  `[-700, 700]`; `+inf` above 709.78, 0 below -745.13.
- `det_sin`, `det_cos`: `x = n pi/2 + r` with pi/2 in three 33-bit parts (fdlibm's), `|r| <= pi/4`, then the degree-17 sine
  or degree-18 cosine Taylor polynomial by quadrant. Absolute error under 1e-12 for `|x|` up to 8e5; deterministic but less
  exact beyond; 0 at or above `2^40`. `det_sin(0) == 0`, `det_cos(0) == 1`.
- `det_asin`: the series to `x^45` on `|x| <= 1/2`; above, `pi/2 - 2 asin(sqrt((1 - |x|) / 2))`. Absolute error under 1e-12.
- `det_atan2`: quadrant by the signs and by which of `|x|`, `|y|` is larger, then `atan` of a ratio within 1 by two
  half-angle steps and the series to `u^27`. Absolute error under 1e-12. Zeros as the header says.

`sqrt` and `fabs` are used: IEEE 754 makes them exact or correctly rounded, like `+ - * /`, so they lean on no library's
choices. `-std=c23` keeps GCC from contracting `a * b + c` into an FMA, so debug and release give the same bits, which
`run_math_det` pins.

**May be called by:** anything in `run/` from the math layer up; the climate's cell fields (`heat`, `rain`, `season`,
`harmonic`, `cell`, `air`, `sun`) and `terrain/climate_sample` are the reason it exists.
**Depends on:** `types`, `sqrt` and `fabs` from `math.h`.
