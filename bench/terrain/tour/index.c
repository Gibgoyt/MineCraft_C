/* bench_terrain_tour: a picture of every biome, with no display and no card. See README.md. */
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "tables/biomes/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum : uint32_t {
    TOUR_CHUNK = TERRAIN_FEATURE_CHUNK,
    TOUR_COLUMNS_PER_CHUNK = TERRAIN_FEATURE_CHUNK_COLUMNS,
    TOUR_LAYERS = 1501u, /* the world, -250 to 1250, both included */
    TOUR_CHUNK_VOXELS = TOUR_COLUMNS_PER_CHUNK * TOUR_LAYERS,
    TOUR_MAP_CHUNKS = 8u,
    TOUR_MAP_ACROSS = TOUR_MAP_CHUNKS * TOUR_CHUNK, /* blocks */
    TOUR_MAP_COLUMNS = TOUR_MAP_ACROSS * TOUR_MAP_ACROSS,
    TOUR_CUT_TALL = 64u, /* blocks of the slice through the middle */
    /* What one biome's picture is, in pixels. A block is 2 by 2. */
    TOUR_SCALE = 2u,
    TOUR_WIDE = TOUR_MAP_ACROSS * TOUR_SCALE,
    TOUR_HIGH = (TOUR_MAP_ACROSS + TOUR_CUT_TALL) * TOUR_SCALE,
    TOUR_PIXELS = TOUR_WIDE * TOUR_HIGH,
    /* The sheet of them all, each at a block a pixel. */
    TOUR_SHEET_ROW = 8u,
    TOUR_SHEET_WIDE = TOUR_SHEET_ROW * TOUR_MAP_ACROSS,
    TOUR_SHEET_RANKS = (BIOME_COUNT + TOUR_SHEET_ROW - 1u) / TOUR_SHEET_ROW,
    TOUR_SHEET_HIGH = TOUR_SHEET_RANKS * (TOUR_MAP_ACROSS + TOUR_CUT_TALL),
    TOUR_SHEET_PIXELS = TOUR_SHEET_WIDE * TOUR_SHEET_HIGH,
    TOUR_WORKERS = 16u,
    /* The search: this many columns a side, this far apart. */
    TOUR_SEARCH_SIDE = 768u,
    TOUR_SEARCH_STEP = 64u,
    TOUR_PATH_BYTES = 512u,
};
static_assert(MC_THREADS_WORKERS >= TOUR_WORKERS);

/* 0xRRGGBB of what a block looks like from far off, typed by hand: this is a map to judge shapes and neighbours by, not the
 * game's renderer. A block missing here is grey, which stands out. Every block a feature or a biome's file can put on the
 * ground is here. */
static const uint32_t TOUR_COLOURS[BLOCK_COUNT] = {
    [BLOCK_ID_AIR] = 0xA0C8F0u,
    [BLOCK_ID_WATER] = 0x2F5FD0u,
    [BLOCK_ID_LAVA] = 0xE06010u,
    [BLOCK_ID_STONE] = 0x808080u,
    [BLOCK_ID_DEEPSLATE] = 0x505058u,
    [BLOCK_ID_BEDROCK] = 0x303030u,
    [BLOCK_ID_DIRT] = 0x866043u,
    [BLOCK_ID_COARSE_DIRT] = 0x77553Bu,
    [BLOCK_ID_ROOTED_DIRT] = 0x90684Au,
    [BLOCK_ID_MUD] = 0x3C3837u,
    [BLOCK_ID_GRASS_BLOCK] = 0x5F9F3Fu,
    [BLOCK_ID_PODZOL] = 0x5B3F1Du,
    [BLOCK_ID_MYCELIUM] = 0x6F6369u,
    [BLOCK_ID_MOSS_BLOCK] = 0x596E2Du,
    [BLOCK_ID_SAND] = 0xDBD3A0u,
    [BLOCK_ID_RED_SAND] = 0xBE6621u,
    [BLOCK_ID_SANDSTONE] = 0xD8CB9Bu,
    [BLOCK_ID_RED_SANDSTONE] = 0xB9601Fu,
    [BLOCK_ID_GRAVEL] = 0x837E7Eu,
    [BLOCK_ID_CLAY] = 0x9EA4B0u,
    [BLOCK_ID_SNOW_BLOCK] = 0xF4F8F8u,
    [BLOCK_ID_ICE] = 0x91B8F5u,
    [BLOCK_ID_PACKED_ICE] = 0x7DA5EAu,
    [BLOCK_ID_BLUE_ICE] = 0x74A7F5u,
    [BLOCK_ID_CALCITE] = 0xDFE0DCu,
    [BLOCK_ID_TUFF] = 0x6C6D66u,
    [BLOCK_ID_DRIPSTONE_BLOCK] = 0x866B5Du,
    [BLOCK_ID_COBBLED_DEEPSLATE] = 0x4B4B50u,
    [BLOCK_ID_TERRACOTTA] = 0x985F44u,
    [BLOCK_ID_WHITE_TERRACOTTA] = 0xD2B2A1u,
    [BLOCK_ID_ORANGE_TERRACOTTA] = 0xA15325u,
    [BLOCK_ID_YELLOW_TERRACOTTA] = 0xBA8523u,
    [BLOCK_ID_RED_TERRACOTTA] = 0x8F3D2Fu,
    [BLOCK_ID_BROWN_TERRACOTTA] = 0x4D3324u,
    [BLOCK_ID_LIGHT_GRAY_TERRACOTTA] = 0x876B62u,
    [BLOCK_ID_OAK_LOG] = 0x6B5130u,
    [BLOCK_ID_BIRCH_LOG] = 0xD5D0C0u,
    [BLOCK_ID_SPRUCE_LOG] = 0x3B2C18u,
    [BLOCK_ID_JUNGLE_LOG] = 0x5A4A22u,
    [BLOCK_ID_ACACIA_LOG] = 0x6A5F55u,
    [BLOCK_ID_DARK_OAK_LOG] = 0x3A2A15u,
    [BLOCK_ID_MANGROVE_LOG] = 0x5C3A28u,
    [BLOCK_ID_CHERRY_LOG] = 0x3A2028u,
    [BLOCK_ID_OAK_LEAVES] = 0x3C7A28u,
    [BLOCK_ID_BIRCH_LEAVES] = 0x6AA040u,
    [BLOCK_ID_SPRUCE_LEAVES] = 0x2F5A38u,
    [BLOCK_ID_JUNGLE_LEAVES] = 0x2C8A18u,
    [BLOCK_ID_ACACIA_LEAVES] = 0x70902Au,
    [BLOCK_ID_DARK_OAK_LEAVES] = 0x225A18u,
    [BLOCK_ID_MANGROVE_LEAVES] = 0x4E8C2Au,
    [BLOCK_ID_CHERRY_LEAVES] = 0xEFB0D0u,
    [BLOCK_ID_TALL_GRASS] = 0x6FB048u,
    [BLOCK_ID_FERN] = 0x4F9040u,
    [BLOCK_ID_DANDELION] = 0xF0E020u,
    [BLOCK_ID_ROSE] = 0xE02020u,
    [BLOCK_ID_ALLIUM] = 0xB070E0u,
    [BLOCK_ID_AZURE_BLUET] = 0xE0E8E8u,
    [BLOCK_ID_BLUE_ORCHID] = 0x2AA0E8u,
    [BLOCK_ID_TULIP] = 0xF08030u,
    [BLOCK_ID_OXEYE_DAISY] = 0xF0F0D0u,
    [BLOCK_ID_CORNFLOWER] = 0x4060E0u,
    [BLOCK_ID_LILY_OF_THE_VALLEY] = 0xF8F8F8u,
    [BLOCK_ID_CACTUS] = 0x2A7A28u,
    [BLOCK_ID_SUGAR_CANE] = 0x94C060u,
    [BLOCK_ID_DEAD_BUSH] = 0x7A5A2Au,
    [BLOCK_ID_BROWN_MUSHROOM] = 0x967050u,
    [BLOCK_ID_RED_MUSHROOM] = 0xD02820u,
    [BLOCK_ID_LILY_PAD] = 0x208030u,
    [BLOCK_ID_VINE] = 0x357520u,
    [BLOCK_ID_MELON] = 0x6A9A28u,
    [BLOCK_ID_PUMPKIN] = 0xD08020u,
    [BLOCK_ID_SEAGRASS] = 0x2E8F60u,
    [BLOCK_ID_KELP] = 0x386F40u,
};
static const uint32_t TOUR_COLOUR_UNKNOWN = 0xA0A0A0u;

static uint32_t tour_colour(uint16_t block) {
    const uint32_t colour = TOUR_COLOURS[block];
    return colour != 0u ? colour : TOUR_COLOUR_UNKNOWN;
}

/* A colour at this many hundredths of itself, up to white. */
static uint32_t tour_shade(uint32_t colour, uint32_t percent) {
    uint32_t out = 0u;
    for (uint32_t shift = 0u; shift <= 16u; shift += 8u) {
        uint32_t channel = ((colour >> shift) & 0xFFu) * percent / 100u;
        channel = channel < 255u ? channel : 255u;
        out |= channel << shift;
    }
    return out;
}

/* ---- PNG: written out with no compression, as nothing here needs it small ------------------------------------------ */

/* All of a run's CRC table, from the polynomial the format gives. */
typedef struct tour_crc_t {
    uint32_t table[256];
} tour_crc_t;
static_assert(sizeof(tour_crc_t) == 1024);

static void tour_crc_fill(tour_crc_t *crc) {
    for (uint32_t i = 0u; i < 256u; i++) {
        uint32_t value = i;
        for (uint32_t k = 0u; k < 8u; k++) {
            value = (value & 1u) != 0u ? 0xEDB88320u ^ (value >> 1) : value >> 1;
        }
        crc->table[i] = value;
    }
}

static uint32_t tour_crc_add(const tour_crc_t *crc, uint32_t state, const uint8_t *data, uint64_t bytes) {
    for (uint64_t i = 0u; i < bytes; i++) {
        state = crc->table[(state ^ data[i]) & 0xFFu] ^ (state >> 8);
    }
    return state;
}

static void tour_put_u32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static bool tour_chunk_write(FILE *file, const tour_crc_t *crc, const char *kind, const uint8_t *head,
                             uint64_t head_bytes, const uint8_t *data, uint64_t data_bytes) {
    uint8_t number[4];
    tour_put_u32(number, (uint32_t)(head_bytes + data_bytes));
    uint32_t state = tour_crc_add(crc, 0xFFFFFFFFu, (const uint8_t *)kind, 4u);
    state = tour_crc_add(crc, state, head, head_bytes);
    state = tour_crc_add(crc, state, data, data_bytes);
    uint8_t tail[4];
    tour_put_u32(tail, state ^ 0xFFFFFFFFu);
    return fwrite(number, 1u, 4u, file) == 4u && fwrite(kind, 1u, 4u, file) == 4u &&
           (head_bytes == 0u || fwrite(head, 1u, head_bytes, file) == head_bytes) &&
           (data_bytes == 0u || fwrite(data, 1u, data_bytes, file) == data_bytes) && fwrite(tail, 1u, 4u, file) == 4u;
}

/* One PNG of 8-bit RGB. stream has room for what deflate's stored blocks make of the rows: see tour_stream_bytes. */
enum : uint32_t { TOUR_STORED_MOST = 65535u };

static uint64_t tour_stream_bytes(uint32_t wide, uint32_t high) {
    const uint64_t raw = ((uint64_t)wide * 3u + 1u) * high;
    return 2u + raw + (raw / TOUR_STORED_MOST + 1u) * 5u + 4u;
}

static bool tour_png_write(const char *path, const tour_crc_t *crc, const uint32_t *pixels, uint32_t wide,
                           uint32_t high, uint8_t *stream) {
    const uint64_t raw = ((uint64_t)wide * 3u + 1u) * high;
    uint64_t at = 0u;
    stream[at++] = 0x78u;
    stream[at++] = 0x01u;
    uint32_t a = 1u;
    uint32_t b = 0u;
    uint64_t done = 0u;
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint32_t channel = 3u; /* the filter byte first */
    while (done < raw) {
        const uint64_t left = raw - done;
        const uint32_t now = left < TOUR_STORED_MOST ? (uint32_t)left : TOUR_STORED_MOST;
        stream[at++] = now == left ? 1u : 0u;
        stream[at++] = (uint8_t)now;
        stream[at++] = (uint8_t)(now >> 8);
        stream[at++] = (uint8_t)~now;
        stream[at++] = (uint8_t)(~now >> 8);
        for (uint32_t i = 0u; i < now; i++) {
            uint8_t value = 0u;
            if (channel == 3u) {
                channel = 0u;
            } else {
                value = (uint8_t)(pixels[x + (uint64_t)wide * y] >> (16u - 8u * channel));
                channel++;
                if (channel == 3u) {
                    x++;
                    if (x == wide) {
                        x = 0u;
                        y++;
                    } else {
                        channel = 0u;
                    }
                }
            }
            stream[at++] = value;
            a = (a + value) % 65521u;
            b = (b + a) % 65521u;
        }
        done += now;
    }
    tour_put_u32(&stream[at], (b << 16) | a);
    at += 4u;

    FILE *const file = fopen(path, "wb");
    if (file == nullptr) {
        fprintf(stderr, "bench_terrain_tour: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    static const uint8_t SIGNATURE[8] = {0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    uint8_t header[13];
    tour_put_u32(&header[0], wide);
    tour_put_u32(&header[4], high);
    header[8] = 8u;  /* bits */
    header[9] = 2u;  /* RGB */
    header[10] = 0u; /* deflate */
    header[11] = 0u; /* the one filter set */
    header[12] = 0u; /* not interlaced */
    bool ok = fwrite(SIGNATURE, 1u, 8u, file) == 8u;
    ok = ok && tour_chunk_write(file, crc, "IHDR", header, 13u, nullptr, 0u);
    ok = ok && tour_chunk_write(file, crc, "IDAT", nullptr, 0u, stream, at);
    ok = ok && tour_chunk_write(file, crc, "IEND", nullptr, 0u, nullptr, 0u);
    if (fclose(file) != 0 || !ok) {
        fprintf(stderr, "bench_terrain_tour: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    return true;
}

/* ---- One biome ----------------------------------------------------------------------------------------------------- */

typedef struct tour_stop_t {
    int32_t x;        /* 0: the column the map is round */
    int32_t y;        /* 4 */
    int32_t z;        /* 8: the height the slice is round */
    uint8_t found;    /* 12 */
    uint8_t _pad0[3]; /* 13 */
} tour_stop_t;
static_assert(sizeof(tour_stop_t) == 16);

/* The first column seen of each biome, and for one under the ground, a z it holds. */
static void tour_search(const terrain_t *terrain, tour_stop_t *stops) {
    memset(stops, 0, BIOME_COUNT * sizeof(*stops));
    terrain_biome_column_t columns[256];
    const int32_t half = (int32_t)(TOUR_SEARCH_SIDE * TOUR_SEARCH_STEP / 2u);
    for (uint32_t row = 0u; row < TOUR_SEARCH_SIDE; row++) {
        for (uint32_t part = 0u; part < TOUR_SEARCH_SIDE / 256u; part++) {
            const terrain_fields_area_t area = {
                .origin_x = -half + (int32_t)(part * 256u * TOUR_SEARCH_STEP),
                .origin_y = -half + (int32_t)(row * TOUR_SEARCH_STEP),
                .step_x = (int32_t)TOUR_SEARCH_STEP,
                .step_y = 1,
                .count_x = 256u,
                .count_y = 1u,
            };
            terrain_biome_columns_area(terrain->borrowed_biome, &area, columns);
            for (uint32_t i = 0u; i < 256u; i++) {
                const int32_t x = area.origin_x + (int32_t)i * area.step_x;
                tour_stop_t *const stop = &stops[columns[i].biome];
                if (stop->found == 0u) {
                    const int32_t ground = (int32_t)columns[i].ground;
                    *stop = (tour_stop_t){
                        .x = x,
                        .y = area.origin_y,
                        .z = ground > MC_WORLD_Z_SEA ? ground : MC_WORLD_Z_SEA,
                        .found = 1u,
                    };
                }
                for (uint32_t u = 0u; u < BIOME_UNDERGROUND_COUNT; u++) {
                    const biome_id_t id = (biome_id_t)biome_underground[u];
                    tour_stop_t *const under = &stops[id];
                    if ((columns[i].caves & (1u << u)) == 0u || under->found != 0u) {
                        continue;
                    }
                    const biome_range_t range = biome_defs[id].height;
                    const float32_t ceiling = columns[i].ground - (float32_t)TERRAIN_BIOME_CAVE_ROOF;
                    const float32_t high = ceiling < range.high ? ceiling : range.high;
                    if (high <= range.low) {
                        continue;
                    }
                    *under = (tour_stop_t){
                        .x = x,
                        .y = area.origin_y,
                        .z = (int32_t)((high + range.low) / 2.0f32),
                        .found = 1u,
                    };
                }
            }
        }
    }
}

/* What is shared with the workers. */
typedef struct tour_run_t {
    const terrain_t *borrowed_terrain; /* 0 */
    uint16_t *borrowed_blocks;         /* 8: one chunk per worker */
    uint8_t *borrowed_opened;          /* 16: the same */
    uint16_t *borrowed_tops;           /* 24: the block on top of each column */
    int16_t *borrowed_heights;         /* 32: and at what z. Water counts as a top */
    uint16_t *borrowed_depths;         /* 40: and how much water is on what is under it */
    uint16_t *borrowed_cut;            /* 48: the slice through the middle, TOUR_MAP_ACROSS by TOUR_CUT_TALL */
    _Atomic uint32_t next;             /* 56 */
    _Atomic uint32_t next_worker;      /* 60 */
    int32_t origin_x;                  /* 64 */
    int32_t origin_y;                  /* 68 */
    int32_t cut_low;                   /* 72: the z the slice starts at */
    uint8_t _pad0[4];                  /* 76 */
} tour_run_t;
static_assert(sizeof(tour_run_t) == 80);

static void tour_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    tour_run_t *const run = borrowed_argument;
    const uint32_t me = atomic_fetch_add_explicit(&run->next_worker, 1u, memory_order_relaxed);
    uint16_t *const blocks = run->borrowed_blocks + (uint64_t)me * TOUR_CHUNK_VOXELS;
    uint8_t *const opened = run->borrowed_opened + (uint64_t)me * TOUR_CHUNK_VOXELS;
    for (;;) {
        const uint32_t chunk = atomic_fetch_add_explicit(&run->next, 1u, memory_order_relaxed);
        if (chunk >= TOUR_MAP_CHUNKS * TOUR_MAP_CHUNKS) {
            return;
        }
        const uint32_t cx = chunk % TOUR_MAP_CHUNKS;
        const uint32_t cy = chunk / TOUR_MAP_CHUNKS;
        const terrain_density_box_t box = {
            .origin_x = run->origin_x + (int32_t)(cx * TOUR_CHUNK),
            .origin_y = run->origin_y + (int32_t)(cy * TOUR_CHUNK),
            .origin_z = MC_WORLD_Z_MIN,
            .count_x = TOUR_CHUNK,
            .count_y = TOUR_CHUNK,
            .count_z = TOUR_LAYERS,
        };
        terrain_generate_box_fallback(run->borrowed_terrain, &box, opened, blocks);
        for (uint32_t oy = 0u; oy < TOUR_CHUNK; oy++) {
            for (uint32_t ox = 0u; ox < TOUR_CHUNK; ox++) {
                const uint32_t column = ox + TOUR_CHUNK * oy;
                const uint64_t map_at =
                    (uint64_t)(cx * TOUR_CHUNK + ox) + (uint64_t)TOUR_MAP_ACROSS * (cy * TOUR_CHUNK + oy);
                int32_t z = (int32_t)TOUR_LAYERS - 1;
                while (z > 0 && blocks[column + TOUR_COLUMNS_PER_CHUNK * (uint32_t)z] == BLOCK_ID_AIR) {
                    z--;
                }
                run->borrowed_tops[map_at] = blocks[column + TOUR_COLUMNS_PER_CHUNK * (uint32_t)z];
                run->borrowed_heights[map_at] = (int16_t)(z + MC_WORLD_Z_MIN);
                uint16_t depth = 0u;
                while (z > 0 && blocks[column + TOUR_COLUMNS_PER_CHUNK * (uint32_t)z] == BLOCK_ID_WATER) {
                    depth++;
                    z--;
                }
                run->borrowed_depths[map_at] = depth;
                if (cy * TOUR_CHUNK + oy == TOUR_MAP_ACROSS / 2u) {
                    for (uint32_t k = 0u; k < TOUR_CUT_TALL; k++) {
                        const int32_t cz = run->cut_low + (int32_t)k - MC_WORLD_Z_MIN;
                        const uint16_t block = cz >= 0 && cz < (int32_t)TOUR_LAYERS
                                                   ? blocks[column + TOUR_COLUMNS_PER_CHUNK * (uint32_t)cz]
                                                   : (uint16_t)BLOCK_ID_AIR;
                        run->borrowed_cut[(uint64_t)(cx * TOUR_CHUNK + ox) + (uint64_t)TOUR_MAP_ACROSS * k] = block;
                    }
                }
            }
        }
    }
}

/* From what the workers left to a picture: the map above, shaded by the slope towards the low x, and the slice below it. */
static void tour_draw(const tour_run_t *run, uint32_t *pixels) {
    for (uint32_t y = 0u; y < TOUR_MAP_ACROSS; y++) {
        for (uint32_t x = 0u; x < TOUR_MAP_ACROSS; x++) {
            const uint64_t at = (uint64_t)x + (uint64_t)TOUR_MAP_ACROSS * y;
            const uint16_t block = run->borrowed_tops[at];
            uint32_t colour = tour_colour(block);
            if (block == BLOCK_ID_WATER) {
                const uint32_t depth = run->borrowed_depths[at];
                colour = tour_shade(colour, depth >= 30u ? 70u : 130u - 2u * depth);
            } else {
                const int32_t from = (int32_t)run->borrowed_heights[at - (x > 0u ? 1u : 0u)];
                const int32_t slope = (int32_t)run->borrowed_heights[at] - from;
                int32_t percent = 100 + 12 * slope;
                percent = percent < 55 ? 55 : (percent > 145 ? 145 : percent);
                colour = tour_shade(colour, (uint32_t)percent);
            }
            for (uint32_t s = 0u; s < TOUR_SCALE * TOUR_SCALE; s++) {
                pixels[(uint64_t)(x * TOUR_SCALE + s % TOUR_SCALE) +
                       (uint64_t)TOUR_WIDE * (y * TOUR_SCALE + s / TOUR_SCALE)] = colour;
            }
        }
    }
    for (uint32_t k = 0u; k < TOUR_CUT_TALL; k++) {
        for (uint32_t x = 0u; x < TOUR_MAP_ACROSS; x++) {
            /* Upward on the page is upward in the world. */
            const uint32_t colour =
                tour_colour(run->borrowed_cut[(uint64_t)x + (uint64_t)TOUR_MAP_ACROSS * (TOUR_CUT_TALL - 1u - k)]);
            for (uint32_t s = 0u; s < TOUR_SCALE * TOUR_SCALE; s++) {
                pixels[(uint64_t)(x * TOUR_SCALE + s % TOUR_SCALE) +
                       (uint64_t)TOUR_WIDE * ((TOUR_MAP_ACROSS + k) * TOUR_SCALE + s / TOUR_SCALE)] = colour;
            }
        }
    }
}

/* A picture at half its size, on the sheet. */
static void tour_sheet_add(uint32_t *sheet, const uint32_t *pixels, uint32_t place) {
    const uint32_t tile_wide = TOUR_MAP_ACROSS;
    const uint32_t tile_high = TOUR_MAP_ACROSS + TOUR_CUT_TALL;
    const uint32_t left = (place % TOUR_SHEET_ROW) * tile_wide;
    const uint32_t top = (place / TOUR_SHEET_ROW) * tile_high;
    for (uint32_t y = 0u; y < tile_high; y++) {
        for (uint32_t x = 0u; x < tile_wide; x++) {
            sheet[(uint64_t)(left + x) + (uint64_t)TOUR_SHEET_WIDE * (top + y)] =
                pixels[(uint64_t)(x * TOUR_SCALE) + (uint64_t)TOUR_WIDE * (y * TOUR_SCALE)];
        }
    }
}

int32_t main(int32_t argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: bench_terrain_tour <directory for the pictures> [seed]\n");
        return 2;
    }
    const char *const directory = argv[1];
    const uint64_t seed = argc == 3 ? strtoull(argv[2], nullptr, 0) : (uint64_t)TERRAIN_SEED_DEFAULT;
    if (mkdir(directory, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "bench_terrain_tour: cannot make %s: %s\n", directory, strerror(errno));
        return 2;
    }
    terrain_t terrain;
    if (!terrain_init(&terrain, seed)) {
        return 2;
    }
    const uint64_t worker_bytes = (uint64_t)TOUR_WORKERS * TOUR_CHUNK_VOXELS * 3u;
    const uint64_t map_bytes = (uint64_t)TOUR_MAP_COLUMNS * 6u + (uint64_t)TOUR_MAP_ACROSS * TOUR_CUT_TALL * 2u;
    const uint64_t pixel_bytes = (uint64_t)TOUR_PIXELS * 4u;
    const uint64_t sheet_bytes = (uint64_t)TOUR_SHEET_PIXELS * 4u;
    const uint64_t stream_bytes =
        tour_stream_bytes(TOUR_SHEET_WIDE, TOUR_SHEET_HIGH) + tour_stream_bytes(TOUR_WIDE, TOUR_HIGH);
    const uint64_t stops_bytes = (uint64_t)BIOME_COUNT * sizeof(tour_stop_t);
    heap_region_t memory = {0};
    if (!heap_reserve(worker_bytes + map_bytes + pixel_bytes + sheet_bytes + stream_bytes + stops_bytes + 4096u,
                      &memory)) {
        terrain_deinit(&terrain);
        return 2;
    }
    thread_topology_t topology;
    job_system_t system;
    if (!thread_topology_read(&topology) || !job_init(&system, &topology)) {
        heap_release(&memory);
        terrain_deinit(&terrain);
        return 2;
    }

    uint8_t *cursor = memory.owned_base;
    uint16_t *const worker_blocks = (uint16_t *)cursor;
    cursor += (uint64_t)TOUR_WORKERS * TOUR_CHUNK_VOXELS * 2u;
    uint8_t *const worker_opened = cursor;
    cursor += (uint64_t)TOUR_WORKERS * TOUR_CHUNK_VOXELS;
    cursor += (8u - ((uintptr_t)cursor & 7u)) & 7u;
    uint32_t *const pixels = (uint32_t *)cursor;
    cursor += pixel_bytes;
    uint32_t *const sheet = (uint32_t *)cursor;
    cursor += sheet_bytes;
    tour_stop_t *const stops = (tour_stop_t *)cursor;
    cursor += stops_bytes;
    uint16_t *const tops = (uint16_t *)cursor;
    cursor += (uint64_t)TOUR_MAP_COLUMNS * 2u;
    int16_t *const heights = (int16_t *)cursor;
    cursor += (uint64_t)TOUR_MAP_COLUMNS * 2u;
    uint16_t *const depths = (uint16_t *)cursor;
    cursor += (uint64_t)TOUR_MAP_COLUMNS * 2u;
    uint16_t *const cut = (uint16_t *)cursor;
    cursor += (uint64_t)TOUR_MAP_ACROSS * TOUR_CUT_TALL * 2u;
    uint8_t *const stream = cursor;

    tour_crc_t crc;
    tour_crc_fill(&crc);
    tour_search(&terrain, stops);
    memset(sheet, 0, sheet_bytes);

    printf("seed 0x%016" PRIX64 ": sheet place, biome, column it is round, height of its slice\n", seed);
    int32_t status = 0;
    uint32_t pictures = 0u;
    for (uint32_t b = 0u; b < BIOME_COUNT; b++) {
        const char *const name = biome_defs[b].name;
        if (stops[b].found == 0u) {
            printf("%2u %-26s not found\n", b, name);
            status = 1;
            continue;
        }
        tour_run_t run = {
            .borrowed_terrain = &terrain,
            .borrowed_blocks = worker_blocks,
            .borrowed_opened = worker_opened,
            .borrowed_tops = tops,
            .borrowed_heights = heights,
            .borrowed_depths = depths,
            .borrowed_cut = cut,
            .origin_x = (stops[b].x >> TERRAIN_FEATURE_CHUNK_SHIFT) * (int32_t)TOUR_CHUNK -
                        (int32_t)(TOUR_MAP_CHUNKS / 2u * TOUR_CHUNK),
            .origin_y = (stops[b].y >> TERRAIN_FEATURE_CHUNK_SHIFT) * (int32_t)TOUR_CHUNK -
                        (int32_t)(TOUR_MAP_CHUNKS / 2u * TOUR_CHUNK),
            .cut_low = stops[b].z - (int32_t)(TOUR_CUT_TALL / 2u),
        };
        job_group_t group;
        job_group_init(&group);
        for (uint32_t i = 0u; i < TOUR_WORKERS; i++) {
            job_submit(&system, &group, tour_job, &run);
        }
        job_group_wait(&system, &group);
        job_group_deinit(&group);
        tour_draw(&run, pixels);
        tour_sheet_add(sheet, pixels, b);

        char path[TOUR_PATH_BYTES];
        const int32_t length = snprintf(path, sizeof(path), "%s/%s.png", directory, name);
        if (length < 0 || (uint32_t)length >= TOUR_PATH_BYTES ||
            !tour_png_write(path, &crc, pixels, TOUR_WIDE, TOUR_HIGH, stream)) {
            status = 1;
            continue;
        }
        pictures++;
        printf("%2u %-26s %7d %7d %5d\n", b, name, stops[b].x, stops[b].y, stops[b].z);
    }
    char path[TOUR_PATH_BYTES];
    const int32_t length = snprintf(path, sizeof(path), "%s/all.png", directory);
    if (length < 0 || (uint32_t)length >= TOUR_PATH_BYTES ||
        !tour_png_write(path, &crc, sheet, TOUR_SHEET_WIDE, TOUR_SHEET_HIGH, stream)) {
        status = 1;
    }
    printf("%u of %u biomes drawn, and all.png, in %s\n", pictures, (uint32_t)BIOME_COUNT, directory);
    job_deinit(&system);
    heap_release(&memory);
    terrain_deinit(&terrain);
    return status;
}
