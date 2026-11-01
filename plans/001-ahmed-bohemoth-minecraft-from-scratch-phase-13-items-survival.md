# 001-ahmed-bohemoth-minecraft-from-scratch — phase 13: Items and survival

**Status:** not started   (updated 2026-11-01)

## Goal

Inventory, crafting, smelting, tools, armor, health and hunger all work.

**Wave:** W8.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-stage-1-item-inventory.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-stage-2-crafting-furnace-chest.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-stage-3-tools-armor.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-stage-4-health-hunger.md`

## Todo

- [ ] Stage 13.1: Items and inventory
- [ ] Stage 13.2: Crafting, furnace and chest
- [ ] Stage 13.3: Tools and armor
- [ ] Stage 13.4: Health and hunger
- [ ] Phase gate passed

## Verification

**Gate:** All four stages pass their tests. The plan sets no separate gate for this phase.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(run_item|run_player_survival)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
