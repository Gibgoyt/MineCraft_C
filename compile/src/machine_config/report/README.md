# compile/src/machine_config/report

**Purpose:** prints machine defines as compiled into it, proving a `config/machine.yaml` change reaches a built binary.
**Tier:** Arena (depth 2), though it allocates nothing.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** `main`. Run `build/<preset>/bin/tools/machine_config_report`; it takes no arguments.
**May be called by:** people, and `tests/compile/machine_config/`.
**Depends on:** the generated `machine/index.h`.
