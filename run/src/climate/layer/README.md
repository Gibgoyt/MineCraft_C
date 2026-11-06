# run/src/climate/layer

**Purpose:** climate map layers to XRGB8888 pixels. Phase 03's four: sea distance, continentality, sea and lakes, and latitude lines; phase 04's three: currents, pressure and wind, each for a day of the year; phase 05's ten: temperature on the day, the yearly mean, the warmest and the coldest month, rain over the year and on the day, hurricane season, monsoon, climate class and snow balance. The Atlas (plan 004 phase 09) and the map tools (`bench/climate --pictures`) draw with this same code.
**Tier:** Arena by depth. It allocates nothing: the caller lends the pixels.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any; it only reads the climate, through `climate_sample_at`, `climate_cell_get`, `climate_air_at` and `climate_normals_at`.
**Public functions:**

- `climate_layer_draw`: `width` by `height` pixels, rows `pitch` bytes apart, pixel (0, 0) at the north-west corner and `blocks_per_pixel` apart; each pixel the warped, blended sample at its centre. Grey (`0x808080`) where a tile is not built. `CLIMATE_LAYER_LATITUDE` changes only its lines' pixels, so it is drawn over another layer.
- `climate_layer_colour`: one sample's colour in a layer of phase 03's.
- `climate_layer_pressure_colour`, `climate_layer_wind_colour`: phase 04's ramps. `climate_layer_pressure_finish`: the pressure layer's second pass, from hPa in each pixel to colours and lines.
- `climate_layer_temperature_colour`, `climate_layer_rain_colour`, `climate_layer_normals_colour` (phase 05): the ramps, and one place's `climate_normals_at_t` in a phase 05 layer.

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
| Temperature: day, mean, warmest, coldest (phase 05) | `climate_normals_at` at the blended ground: the normal daily mean on the day, the yearly mean, the warmest and the coldest month's mean. Stops -30 °C `0x3040A0`, -10 `0x60A0E0`, 0 `0xF0F0F0`, 15 `0xF0D060`, 30 `0xE06020`, 45 `0x801010`, linear in each channel between stops, held past the ends |
| Rain: year, day (phase 05) | Yearly rain, or the day's normal times 365 so a day reads on the year's scale. Stops 0 mm `0xC8A060`, 250 `0xE0D090`, 500 `0xA0C060`, 1000 `0x40A040`, 2000 `0x2070C0`, 3000 `0x203080` |
| Hurricane season (phase 05) | The sea-and-lakes layer underneath. Sea with `s_hur > 0` on the day from `0x603030` (just above 0) to `0xFF2020` (1); hurricane-coast land in season on the day `0xFF9040` |
| Monsoon (phase 05) | The sea-and-lakes layer underneath. Land with the monsoon flag `0x208080`; monsoon today `0x30E0E0` |
| Climate class (phase 05) | Ice `0xFFFFFF`, Tundra `0xB0B8C0`, Desert `0xF0D080`, Steppe `0xD0B060`, Rainforest `0x006020`, Monsoon `0x208040`, Savanna `0xA0C040`, Mediterranean `0xE0A040`, Temperate `0x60B060`, Continental `0x6080C0`, Boreal `0x305060`; the sea (no class) as the sea-and-lakes layer |
| Snow balance (phase 05) | Land at or above its `snow_balance_z`: white. Below: grey from `0xA0A0A0` (within 50 blocks) to `0x404040` (2000 or more below). Sea and large lakes: the sea-and-lakes layer |

Phase 04's and phase 05's layers take `climate_layer_draw`'s `day`, the Atlas's day slider; phase 05's year layers ignore it. Pixel (0, 0) is the north-west corner, so a vector's `v` is drawn upward. Arrows are integer line walks (Bresenham's), clipped to the picture, with no allocation. The pressure layer keeps each pixel's hPa as a `float32_t` in the picture itself for its second pass (`climate_layer_pressure_finish`), which colours from the south-east corner back so each pixel's north and west neighbours are still numbers.

A latitude line's row is each row whose y range, `[north - (row + 1) b, north - row b)`, holds a y where φ is the line's value, for every repeat of the 153,191-block pattern in view: φ is a triangle wave, so the value is met at `L Y / 90 + 4 Y k` and `2 Y - L Y / 90 + 4 Y k`.

**May be called by:** `bench/climate`, and later `ui` (the Atlas).
**Depends on:** its parent `run/src/climate/`, `types`, libm.
