# run/src/climate/layer

**Purpose:** climate map layers to XRGB8888 pixels. Phase 03's four: sea distance, continentality, sea and lakes, and latitude lines. The Atlas (plan 004 phase 09) and the map tools (`bench/climate --pictures`) draw with this same code.
**Tier:** Arena by depth. It allocates nothing: the caller lends the pixels.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any; it only reads the climate, through `climate_sample_at`.
**Public functions:**

- `climate_layer_draw`: `width` by `height` pixels, rows `pitch` bytes apart, pixel (0, 0) at the north-west corner and `blocks_per_pixel` apart; each pixel the warped, blended sample at its centre. Grey (`0x808080`) where a tile is not built. `CLIMATE_LAYER_LATITUDE` changes only its lines' pixels, so it is drawn over another layer.
- `climate_layer_colour`: one sample's colour in a layer.

**Colours:**

| Layer | Colour |
|---|---|
| Sea distance | Black at 0 to white at 8192, linear |
| Continentality | κ = 0 is `0x2060C0` (blue), κ = 1 is `0xA06030` (brown), linear in each channel |
| Sea and lakes | Sea `0x103070`, large lake `0x3080E0`, small lake `0x70B0F0`, land grey by `z_ground` from `0x406040` at 0 to `0xF0F0F0` at 454 |
| Latitude | A 1-pixel line where φ crosses 0° (red `0xFF2020`), ±23.5° (orange `0xFF9020`), ±66.5° (cyan `0x20E0FF`) |

A latitude line's row is each row whose y range, `[north - (row + 1) b, north - row b)`, holds a y where φ is the line's value, for every repeat of the 153,191-block pattern in view: φ is a triangle wave, so the value is met at `L Y / 90 + 4 Y k` and `2 Y - L Y / 90 + 4 Y k`.

**May be called by:** `bench/climate`, and later `ui` (the Atlas).
**Depends on:** its parent `run/src/climate/`, `types`, libm.
