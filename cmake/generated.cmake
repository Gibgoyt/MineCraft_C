# Custom commands for what the game embeds. Included before cmake/targets.cmake, so tools are named here before they exist,
# which add_custom_command allows. targets.cmake makes mc_asm wait for the mc_texture_blob target.

# ---- Texture blob: Stage 06.4 ------------------------------------------------------------------------------------------

# Which tree the game is built from. textures/ is committed: Pixel Perfection and our recipes. textures_local/ is the same
# tree with Minecraft's default art, made on this machine by tools/import_minecraft_default.sh and ignored by git, since
# the art is Mojang's and this repository is public. Plan 005 phase 9. Delete textures_local/ and configure again to go
# back. Tests whose expected pictures or digests come from the committed art are disabled with it: MC_ART_TESTS below.
if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/textures_local")
  set(MC_TEXTURE_ROOT "${CMAKE_SOURCE_DIR}/textures_local")
  set(MC_TEXTURES_LOCAL ON)
  message(STATUS "Textures: textures_local/, Minecraft's default pack, on this machine only")
else()
  set(MC_TEXTURE_ROOT "${CMAKE_SOURCE_DIR}/textures")
  set(MC_TEXTURES_LOCAL OFF)
  message(STATUS "Textures: textures/, Pixel Perfection and our recipes")
endif()
# textures_local/ appearing or going later reconfigures, through this glob, or the build would go on using the other tree.
file(GLOB mc_texture_local_probe CONFIGURE_DEPENDS LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/textures_local")

# Nothing under textures_local/ may ever be tracked: the build fails first. A tree with no git, such as an unpacked
# release, has nothing tracked and passes.
add_custom_target(mc_check_textures_local
  COMMAND "${CMAKE_COMMAND}" -D "MC_SOURCE_DIR=${CMAKE_SOURCE_DIR}" -P "${CMAKE_SOURCE_DIR}/cmake/check_textures_local.cmake"
  COMMENT "Checking that git tracks nothing under textures_local/"
  VERBATIM)

# The tree -> vector data per texture -> mip chains and everything else in one file -> asm/src/embed_assets/.
set(mc_texture_blob_file "${MC_GENERATED_DIR}/textures/index.mctb")
set(mc_texture_vectors "${MC_GENERATED_DIR}/textures/vectors")
set(mc_texture_table "${MC_TEXTURE_ROOT}/vector.yaml")
file(GLOB mc_texture_inputs CONFIGURE_DEPENDS "${MC_TEXTURE_ROOT}/*/*/index.mctex")

# Ninja reruns a command for a new or changed input, and not for one gone from its list, so a deleted texture would stay in
# the blob. This file changes when the list does, and only then.
string(JOIN "\n" mc_texture_input_list ${mc_texture_inputs})
file(CONFIGURE OUTPUT "${MC_GENERATED_DIR}/textures/inputs.txt" CONTENT "${mc_texture_input_list}\n" @ONLY)

# The touch: the tool leaves a file with the same bytes alone, and then Ninja would see the output as old on every build.
add_custom_command(OUTPUT "${mc_texture_blob_file}"
  COMMAND mc_tool_texture_vector vectors --textures "${MC_TEXTURE_ROOT}" --materials "${mc_texture_table}"
    --out "${mc_texture_vectors}"
  COMMAND mc_tool_texture_blob --textures "${MC_TEXTURE_ROOT}" --vectors "${mc_texture_vectors}"
    --out "${mc_texture_blob_file}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${mc_texture_blob_file}"
  DEPENDS mc_tool_texture_vector mc_tool_texture_blob ${mc_texture_inputs} "${mc_texture_table}"
    "${MC_GENERATED_DIR}/textures/inputs.txt"
  COMMENT "Generating the texture blob from ${MC_TEXTURE_ROOT}"
  VERBATIM)
# Only order: NASM's -MD lists what it incbins, so a new blob rebuilds asm/src/embed_assets/ by its depfile.
add_custom_target(mc_texture_blob DEPENDS "${mc_texture_blob_file}")

# ---- Sound blob: plan 003 phase 5 --------------------------------------------------------------------------------------

# sounds/ -> one file, with every recipe made into samples here -> asm/src/embed_assets/. It needs no network and no ffmpeg.
set(mc_sound_blob_file "${MC_GENERATED_DIR}/sounds/index.mcsb")
file(GLOB mc_sound_inputs CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/sounds/ATTRIBUTION.md"
  "${CMAKE_SOURCE_DIR}/sounds/*/README.md"
  "${CMAKE_SOURCE_DIR}/sounds/*/*/index.yaml"
  "${CMAKE_SOURCE_DIR}/sounds/*/*/index.mcsnd"
  "${CMAKE_SOURCE_DIR}/sounds/*/*/README.md")

# As for textures: a deleted sound changes this list, and Ninja would not otherwise notice it had gone.
string(JOIN "\n" mc_sound_input_list ${mc_sound_inputs})
file(CONFIGURE OUTPUT "${MC_GENERATED_DIR}/sounds/inputs.txt" CONTENT "${mc_sound_input_list}\n" @ONLY)

# The touch, for the reason the texture blob has one.
add_custom_command(OUTPUT "${mc_sound_blob_file}"
  COMMAND mc_tool_sound_pack blob --sounds "${CMAKE_SOURCE_DIR}/sounds" --out "${mc_sound_blob_file}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${mc_sound_blob_file}"
  DEPENDS mc_tool_sound_pack ${mc_sound_inputs} "${MC_GENERATED_DIR}/sounds/inputs.txt"
  COMMENT "Generating the sound blob"
  VERBATIM)
# Only order: NASM's -MD lists what it incbins, so a new blob rebuilds asm/src/embed_assets/ by its depfile.
add_custom_target(mc_sound_blob DEPENDS "${mc_sound_blob_file}")

# ---- Game tables: Stage 07.1 --------------------------------------------------------------------------------------------

# config/blocks/ -> build/<preset>/generated/tables/blocks/index.{h,c}. The tool checks references into textures/ and
# config/items/, so those are inputs too. targets.cmake compiles the .c into mc_run and into the tool's test.
set(mc_tables_dir "${MC_GENERATED_DIR}/tables")
set(mc_block_table_header "${mc_tables_dir}/blocks/index.h")
set(mc_block_table_source "${mc_tables_dir}/blocks/index.c")
# config/biomes/ -> tables/biomes/index.{h,c}, by the same tool and the same command: Stage 10.6.
set(mc_biome_table_header "${mc_tables_dir}/biomes/index.h")
set(mc_biome_table_source "${mc_tables_dir}/biomes/index.c")
# config/items/ -> tables/items/index.{h,c}, the same again: Stage 13.1.
set(mc_item_table_header "${mc_tables_dir}/items/index.h")
set(mc_item_table_source "${mc_tables_dir}/items/index.c")
# config/recipes/ -> tables/recipes/index.{h,c}, and again: Stage 13.2.
set(mc_recipe_table_header "${mc_tables_dir}/recipes/index.h")
set(mc_recipe_table_source "${mc_tables_dir}/recipes/index.c")
# config/mobs/ -> tables/mobs/index.{h,c}, and again: Stage 15.3.
set(mc_mob_table_header "${mc_tables_dir}/mobs/index.h")
set(mc_mob_table_source "${mc_tables_dir}/mobs/index.c")
# config/ores/ -> tables/ores/index.{h,c}, and again: plan 005 phase 11.
set(mc_ore_table_header "${mc_tables_dir}/ores/index.h")
set(mc_ore_table_source "${mc_tables_dir}/ores/index.c")
file(GLOB mc_block_inputs CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/config/blocks/*/index.yaml"
  "${CMAKE_SOURCE_DIR}/config/items/*/index.yaml"
  "${CMAKE_SOURCE_DIR}/config/recipes/*/index.yaml"
  "${CMAKE_SOURCE_DIR}/config/biomes/*/index.yaml"
  "${CMAKE_SOURCE_DIR}/config/mobs/*/index.yaml"
  "${CMAKE_SOURCE_DIR}/config/ores/*/index.yaml"
  "${MC_TEXTURE_ROOT}/items/*/index.mctex")

# Ninja reruns a command for a new or changed input, and not for one gone from its list, so a deleted block would stay in the
# tables. This file changes when the list does, and only then. The block directories are in it for one with no file yet.
file(GLOB mc_block_directories CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/config/blocks/*"
  "${CMAKE_SOURCE_DIR}/config/items/*"
  "${CMAKE_SOURCE_DIR}/config/recipes/*"
  "${CMAKE_SOURCE_DIR}/config/biomes/*"
  "${CMAKE_SOURCE_DIR}/config/mobs/*"
  "${CMAKE_SOURCE_DIR}/config/ores/*")
string(JOIN "\n" mc_block_input_list ${mc_block_directories} ${mc_block_inputs} ${mc_texture_inputs})
file(CONFIGURE OUTPUT "${mc_tables_dir}/inputs.txt" CONTENT "${mc_block_input_list}\n" @ONLY)

# The touch: the tool leaves a file with the same bytes alone, and then Ninja would see the outputs as old on every build.
add_custom_command(OUTPUT "${mc_block_table_header}" "${mc_block_table_source}" "${mc_biome_table_header}"
    "${mc_biome_table_source}" "${mc_item_table_header}" "${mc_item_table_source}" "${mc_recipe_table_header}"
    "${mc_recipe_table_source}" "${mc_mob_table_header}" "${mc_mob_table_source}" "${mc_ore_table_header}"
    "${mc_ore_table_source}"
  COMMAND mc_tool_game_tables --config "${CMAKE_SOURCE_DIR}/config" --textures "${MC_TEXTURE_ROOT}"
    --out "${mc_tables_dir}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${mc_block_table_header}" "${mc_block_table_source}" "${mc_biome_table_header}"
    "${mc_biome_table_source}" "${mc_item_table_header}" "${mc_item_table_source}" "${mc_recipe_table_header}"
    "${mc_recipe_table_source}" "${mc_mob_table_header}" "${mc_mob_table_source}" "${mc_ore_table_header}"
    "${mc_ore_table_source}"
  DEPENDS mc_tool_game_tables ${mc_block_inputs} ${mc_texture_inputs} "${mc_tables_dir}/inputs.txt"
  COMMENT "Generating the block, item, recipe, biome, ore and mob tables"
  VERBATIM)
add_custom_target(mc_game_tables DEPENDS "${mc_block_table_header}" "${mc_block_table_source}" "${mc_biome_table_header}"
  "${mc_biome_table_source}" "${mc_item_table_header}" "${mc_item_table_source}" "${mc_recipe_table_header}"
  "${mc_recipe_table_source}" "${mc_mob_table_header}" "${mc_mob_table_source}" "${mc_ore_table_header}"
  "${mc_ore_table_source}")
# Sources of generated tables that the game links.
set(mc_generated_run_sources "${mc_block_table_source}" "${mc_biome_table_source}" "${mc_item_table_source}"
  "${mc_recipe_table_source}" "${mc_mob_table_source}" "${mc_ore_table_source}")

# ---- Tuning: plan 009 phase 02 -----------------------------------------------------------------------------------------

# compile/config/<path>/index.yaml -> generated/tuning/<path>/index.{h,hu}, and generated/tuning/hash/index.h with MC_TUNE_HASH.
# See compile/src/tuning/README.md. A header per file, so a change to one rebuilds only what includes it.
set(mc_tuning_dir "${MC_GENERATED_DIR}/tuning")
set(mc_tuning_config "${CMAKE_SOURCE_DIR}/compile/config")
file(GLOB mc_tuning_inputs CONFIGURE_DEPENDS
  "${mc_tuning_config}/*/index.yaml"
  "${mc_tuning_config}/*/*/index.yaml"
  "${mc_tuning_config}/*/*/*/index.yaml")
set(mc_tuning_headers "${mc_tuning_dir}/hash/index.h")
foreach(mc_input IN LISTS mc_tuning_inputs)
  cmake_path(GET mc_input PARENT_PATH mc_input_dir)
  cmake_path(RELATIVE_PATH mc_input_dir BASE_DIRECTORY "${mc_tuning_config}" OUTPUT_VARIABLE mc_input_rel)
  list(APPEND mc_tuning_headers "${mc_tuning_dir}/${mc_input_rel}/index.h" "${mc_tuning_dir}/${mc_input_rel}/index.hu")
endforeach()

# As for the tables: a file gone changes this list, and Ninja would not otherwise run the tool again.
string(JOIN "\n" mc_tuning_input_list ${mc_tuning_inputs})
file(CONFIGURE OUTPUT "${mc_tuning_dir}/inputs.txt" CONTENT "${mc_tuning_input_list}\n" @ONLY)

# No touch, unlike the tables: the tool leaves a header with the same bytes alone, and the headers are byproducts, so Ninja's
# restat sees them unchanged and rebuilds nothing that includes them. The stamp is what Ninja compares with the inputs.
set(mc_tuning_stamp "${mc_tuning_dir}/stamp")
add_custom_command(OUTPUT "${mc_tuning_stamp}"
  BYPRODUCTS ${mc_tuning_headers}
  COMMAND mc_tool_tuning --config "${mc_tuning_config}" --out "${mc_tuning_dir}" --stamp "${mc_tuning_stamp}"
  DEPENDS mc_tool_tuning ${mc_tuning_inputs} "${mc_tuning_dir}/inputs.txt"
  COMMENT "Generating the tuning headers from compile/config"
  VERBATIM)
# cmake/targets.cmake puts this before every compile, through mc_prebuild.
add_custom_target(mc_tuning DEPENDS "${mc_tuning_stamp}")
