# Custom commands for what the game embeds. Included before cmake/targets.cmake, so tools are named here before they exist,
# which add_custom_command allows. targets.cmake makes mc_asm wait for the mc_texture_blob target.

# ---- Texture blob: Stage 06.4 ------------------------------------------------------------------------------------------

# textures/ -> vector data per texture -> mip chains and everything else in one file -> asm/src/embed_assets/.
set(mc_texture_blob_file "${MC_GENERATED_DIR}/textures/index.mctb")
set(mc_texture_vectors "${MC_GENERATED_DIR}/textures/vectors")
set(mc_texture_table "${CMAKE_SOURCE_DIR}/textures/vector.yaml")
file(GLOB mc_texture_inputs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/textures/*/*/index.mctex")

# The touch: the tool leaves a file with the same bytes alone, and then Ninja would see the output as old on every build.
add_custom_command(OUTPUT "${mc_texture_blob_file}"
  COMMAND mc_tool_texture_vector vectors --textures "${CMAKE_SOURCE_DIR}/textures" --materials "${mc_texture_table}"
    --out "${mc_texture_vectors}"
  COMMAND mc_tool_texture_blob --textures "${CMAKE_SOURCE_DIR}/textures" --vectors "${mc_texture_vectors}"
    --out "${mc_texture_blob_file}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${mc_texture_blob_file}"
  DEPENDS mc_tool_texture_vector mc_tool_texture_blob ${mc_texture_inputs} "${mc_texture_table}"
  COMMENT "Generating the texture blob"
  VERBATIM)
# Only order: NASM's -MD lists what it incbins, so a new blob rebuilds asm/src/embed_assets/ by its depfile.
add_custom_target(mc_texture_blob DEPENDS "${mc_texture_blob_file}")
