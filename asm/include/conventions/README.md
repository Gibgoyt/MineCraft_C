# asm/include/conventions

**Purpose:** what every SIMD kernel shares: the register rules in one comment, `YMM_BYTES`, `XMM_BYTES`, and `REQUIRE_CPU`,
which fails the build if `config/machine.yaml` turns off an instruction set the kernel uses.
**Tier:** none: a NASM include with no code of its own.
**Owns:** nothing.
**Lifetime:** build time.
**Threads:** none.
**Public functions:** the macro `REQUIRE_CPU <name>`, where `<name>` ends an `MC_CPU_HAS_` define, such as `AVX2`.
**May be called by:** any file under `asm/src/`.
**Depends on:** the generated `machine/index.inc`.
