# config/tiers

One directory per tier of tool, what a tool is made of, and to see what one does, open its one file:

```yaml
# config/tiers/copper/index.yaml
name: copper
harvests: stone
speed: 5000
axe_attack_speed: 0.8
hoe_attack_speed: 2
```

An item's `tier` (`config/items/README.md`) names one of these. To add a tier: make `config/tiers/<name>/index.yaml`, give tools `tier: <name>`, and build. `compile/src/game_tables/items/` turns these into `item_tiers` in `tables/items/`, with `item_tier_t` sorted by name after `none`, and refuses a field it does not know, with the line. Plan 005 phase 6 moved them here from a fixed list in code, and added copper.

## Fields

| Field | Needed | Values |
|---|---|---|
| `name` | yes | The directory's name: lower case, digits, underscores. Not `none`, which is a bare hand |
| `harvests` | yes | The rung of a block's `tier` it breaks: `none wood stone iron diamond`, the words of `config/blocks/README.md`. A tool harvests a block whose tier is this rung or lower |
| `speed` | yes | 1000 to 65535: how fast it breaks its own tool's blocks, in thousandths of a bare hand. `run/src/item/equipment/` gives it to mining; a sword mines as any sword |
| `axe_attack_speed` | yes | 0 to 20, decimals allowed: swings a second with an axe of it |
| `hoe_attack_speed` | yes | The same, for a hoe |

## The tiers

| Tier | Harvests | Speed | Made at |
|---|---|---|---|
| `wood` | wood | 2000 | the woodworking bench |
| `stone` | stone | 4000 | the stoneworking bench |
| `copper` | stone | 5000 | the smith table, from copper the furnace smelts |
| `iron` | iron | 6000 | the smith table, from iron the blast furnace smelts |
| `stainless_steel` | iron | 7000 | the smith table, from stainless steel its alloy slots make of iron and chromium: a tier above iron, and its tools last 500 uses to iron's 250 (plan 005 phase 11) |
| `gold` | wood | 12000 | the smith table: fast, and breaks only what wood breaks |
| `diamond` | diamond | 8000 | nothing yet: nothing can work diamond, so its recipes are `off` |

The numbers are Minecraft's published ones; copper is Minecraft 1.21.9's, between stone and iron. Stainless steel is ours, to tune: Minecraft has none.
