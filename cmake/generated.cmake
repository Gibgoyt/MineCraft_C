# Custom commands for what the game embeds. Included before cmake/targets.cmake, so tools are named here before they exist,
# which add_custom_command allows. targets.cmake makes mc_asm wait for the mc_texture_blob target.

# ---- Texture blob: Stage 06.4 ------------------------------------------------------------------------------------------

# textures/ -> vector data per texture -> mip chains and everything else in one file -> asm/src/embed_assets/.
set(mc_texture_blob_file "${MC_GENERATED_DIR}/textures/index.mctb")
set(mc_texture_vectors "${MC_GENERATED_DIR}/textures/vectors")
set(mc_texture_table "${CMAKE_SOURCE_DIR}/textures/vector.yaml")
file(GLOB mc_texture_inputs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/textures/*/*/index.mctex")

# Ninja reruns a command for a new or changed input, and not for one gone from its list, so a deleted texture would stay in
# the blob. This file changes when the list does, and only then.
string(JOIN "\n" mc_texture_input_list ${mc_texture_inputs})
file(CONFIGURE OUTPUT "${MC_GENERATED_DIR}/textures/inputs.txt" CONTENT "${mc_texture_input_list}\n" @ONLY)

# The touch: the tool leaves a file with the same bytes alone, and then Ninja would see the output as old on every build.
add_custom_command(OUTPUT "${mc_texture_blob_file}"
  COMMAND mc_tool_texture_vector vectors --textures "${CMAKE_SOURCE_DIR}/textures" --materials "${mc_texture_table}"
    --out "${mc_texture_vectors}"
  COMMAND mc_tool_texture_blob --textures "${CMAKE_SOURCE_DIR}/textures" --vectors "${mc_texture_vectors}"
    --out "${mc_texture_blob_file}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${mc_texture_blob_file}"
  DEPENDS mc_tool_texture_vector mc_tool_texture_blob ${mc_texture_inputs} "${mc_texture_table}"
    "${MC_GENERATED_DIR}/textures/inputs.txt"
  COMMENT "Generating the texture blob"
  VERBATIM)
# Only order: NASM's -MD lists what it incbins, so a new blob rebuilds asm/src/embed_assets/ by its depfile.
add_custom_target(mc_texture_blob DEPENDS "${mc_texture_blob_file}")

# ---- Game tables: Stage 07.1 --------------------------------------------------------------------------------------------

# config/blocks/ -> build/<preset>/generated/tables/blocks/index.{h,c}. The tool checks references into textures/ and
# config/items/, so those are inputs too. targets.cmake compiles the .c into mc_run and into the tool's test.
set(mc_tables_dir "${MC_GENERATED_DIR}/tables")
set(mc_block_table_header "${mc_tables_dir}/blocks/index.h")
set(mc_block_table_source "${mc_tables_dir}/blocks/index.c")
# config/biomes/ -> tables/biomes/index.{h,c}, by the same tool and the same command: Stage 10.6.
set(mc_biome_table_header "${mc_tables_dir}/biomes/index.h")
set(mc_biome_table_source "${mc_tables_dir}/biomes/index.c")
file(GLOB mc_block_inputs CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/config/blocks/*/index.yaml"
  "${CMAKE_SOURCE_DIR}/config/items/*/index.yaml"
  "${CMAKE_SOURCE_DIR}/config/biomes/*/index.yaml"
  "${CMAKE_SOURCE_DIR}/textures/items/*/index.mctex")

# Ninja reruns a command for a new or changed input, and not for one gone from its list, so a deleted block would stay in the
# tables. This file changes when the list does, and only then. The block directories are in it for one with no file yet.
file(GLOB mc_block_directories CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/config/blocks/*"
  "${CMAKE_SOURCE_DIR}/config/biomes/*")
string(JOIN "\n" mc_block_input_list ${mc_block_directories} ${mc_block_inputs} ${mc_texture_inputs})
file(CONFIGURE OUTPUT "${mc_tables_dir}/inputs.txt" CONTENT "${mc_block_input_list}\n" @ONLY)

# The touch: the tool leaves a file with the same bytes alone, and then Ninja would see the outputs as old on every build.
add_custom_command(OUTPUT "${mc_block_table_header}" "${mc_block_table_source}" "${mc_biome_table_header}"
    "${mc_biome_table_source}"
  COMMAND mc_tool_game_tables --config "${CMAKE_SOURCE_DIR}/config" --textures "${CMAKE_SOURCE_DIR}/textures"
    --out "${mc_tables_dir}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${mc_block_table_header}" "${mc_block_table_source}" "${mc_biome_table_header}"
    "${mc_biome_table_source}"
  DEPENDS mc_tool_game_tables ${mc_block_inputs} ${mc_texture_inputs} "${mc_tables_dir}/inputs.txt"
  COMMENT "Generating the block and biome tables"
  VERBATIM)
add_custom_target(mc_game_tables DEPENDS "${mc_block_table_header}" "${mc_block_table_source}" "${mc_biome_table_header}"
  "${mc_biome_table_source}")
# Sources of generated tables that the game links.
set(mc_generated_run_sources "${mc_block_table_source}" "${mc_biome_table_source}")
