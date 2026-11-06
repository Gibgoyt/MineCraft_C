# run/src/climate/layer

**Purpose:** climate map layers to XRGB8888 pixels. Phase 03's four: sea distance, continentality, sea and lakes, and latitude lines; phase 04's three: currents, pressure and wind, each for a day of the year. The Atlas (plan 004 phase 09) and the map tools (`bench/climate --pictures`) draw with this same code.
**Tier:** Arena by depth. It allocates nothing: the caller lends the pixels.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any; it only reads the climate, through `climate_sample_at`, `climate_cell_get` and `climate_air_at`.
**Public functions:**

- `climate_layer_draw`: `width` by `height` pixels, rows `pitch` bytes apart, pixel (0, 0) at the north-west corner and `blocks_per_pixel` apart; each pixel the warped, blended sample at its centre. Grey (`0x808080`) where a tile is not built. `CLIMATE_LAYER_LATITUDE` changes only its lines' pixels, so it is drawn over another layer.
- `climate_layer_colour`: one sample's colour in a layer of phase 03's.
- `climate_layer_pressure_colour`, `climate_layer_wind_colour`: phase 04's ramps. `climate_layer_pressure_finish`: the pressure layer's second pass, from hPa in each pixel to colours and lines.

**Colours:**

| Layer | Colour |
|---|---|
| Sea distance | Black at 0 to white at 8192, linear |
| Continentality | κ = 0 is `0x2060C0` (blue), κ = 1 is `0xA06030` (brown), linear in each channel |
| Sea and lakes | Sea `0x103070`, large lake `0x3080E0`, small lake `0x70B0F0`, land grey by `z_ground` from `0x406040` at 0 to `0xF0F0F0` at 454 |
| Latitude | A 1-pixel line where φ crosses 0° (red `0xFF2020`), ±23.5° (orange `0xFF9020`), ±66.5° (cyan `0x20E0FF`) |
| Currents (phase 04) | The sea-and-lakes layer underneath. An arrow every 32 pixels (from pixel 16) on sea cells with a current: red `0xE03030` warm, blue `0x3060E0` cold, `4 + 12 × strength / 255` pixels along the flow (`climate_current_flow`), with two 3-pixel barbs at its head. A neutral current has no flow, so a sea cell within 600 blocks of land with one gets a small grey `0x909090` cross. Land unchanged |
| Pressure (phase 04) | `p_belt + Δp_heat` from `climate_air_at` for the day: `0x3050C0` at 990 hPa through `0xF0F0F0` at 1013 to `0xC04030` at 1030, linear in each half, held past the ends. A 1-pixel dark line (`0x202020`) where `floor(p / 4)` differs from the pixel north or west: the master plan's lines every 4 hPa (9.3) |
| Wind (phase 04) | Speed as colour, `0x202040` at 0 to `0xF0E040` at 15 blocks a second. A white arrow every 32 pixels, `2 × speed` pixels long, at most 30, pointing downwind |

Phase 04's layers take `climate_layer_draw`'s `day`, the Atlas's day slider. Pixel (0, 0) is the north-west corner, so a vector's `v` is drawn upward. Arrows are integer line walks (Bresenham's), clipped to the picture, with no allocation. The pressure layer keeps each pixel's hPa as a `float32_t` in the picture itself for its second pass (`climate_layer_pressure_finish`), which colours from the south-east corner back so each pixel's north and west neighbours are still numbers.

A latitude line's row is each row whose y range, `[north - (row + 1) b, north - row b)`, holds a y where φ is the line's value, for every repeat of the 153,191-block pattern in view: φ is a triangle wave, so the value is met at `L Y / 90 + 4 Y k` and `2 Y - L Y / 90 + 4 Y k`.

**May be called by:** `bench/climate`, and later `ui` (the Atlas).
**Depends on:** its parent `run/src/climate/`, `types`, libm.
