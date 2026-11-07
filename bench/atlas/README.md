# bench/atlas

**Purpose:** what the Atlas (plan 004 phase 09) costs, against the phase file's budget: opening it over 100 km² of
generated land, `M` to the first complete picture, under 100 ms; each layer's whole picture at 1920 by 1080 on 16 workers
under 60 ms; the main thread's frame with the Atlas up under 2 ms; a column's write on the sim under 5 µs and its top on
a worker under 20 µs.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region for the job system, a `ui_t`, a `texture_t`, a climate, a terrain and the pictures, with what
those reserve, all released before it exits; a temporary directory `/tmp/mc_bench_atlas_*` of tile files, removed at the end.
**Lifetime:** Process.
**Threads:** main and 16 workers.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 300 build/release/bin/bench_atlas`, so nothing else
shares the CPU. Exit status 0 if it ran, 2 if it could not start. A target missed is printed as `MISSED` and does not change
the status, as `bench/world/README.md` explains.
**May be called by:** people.
**Depends on:** `ui` and `ui/atlas`, `climate`, `terrain`, `texture`, `job`, `thread`, `heap`, `time`, the generated
`tables/blocks`. No card, no display, no input.

What it does:

1. Works out the climate tiles round the view and 8192 blocks beyond (seed 2198175988018515243): temperature now's
   weather reads every tile within 6 km of each place, as the census keeps them built round explored land, and with
   them missing it would time its failures.
2. Writes 20 by 20 level-0 tiles of made-up columns (a smooth land from a few sines, 10 240 blocks a side, 105 km²) into
   the store, saving every 100 tiles so nothing is dropped, and closes it, so every tile and mip is on disk.
3. **Open:** five times, opens the world's store with nothing in memory (the files in the page cache), presses `M` at
   11.3 blocks a pixel (21.7 by 12.2 km on screen, level 3), and times main's frames until the first picture is finished.
4. **Layers:** each of the 26 layers at 1920 by 1080, 5.66 blocks a pixel, three times, on 16 workers in 16 bands, with
   the weather systems' day list built for the now layers inside the time. For each value layer it prints the share of
   pixels that got a value, so a layer timed with nothing to show is seen.
5. **Main's frame:** 200 frames of `ui_atlas_frame` and `ui_atlas_frame_draw` with a picture shown on temperature by day.
6. **Columns:** 20 000 column writes, five rounds; 2000 column tops of a column of stone to 64, five rounds.
