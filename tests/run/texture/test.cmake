# Included by mc_add_test in cmake/targets.cmake. The textures the build used (MC_TEXTURE_ROOT, cmake/generated.cmake),
# and what the build made from them, to compare what the program embeds with.
set(MC_TEST_ARGS
  "${MC_TEXTURE_ROOT}"
  "${MC_GENERATED_DIR}/textures/index.mctb"
  "${MC_GENERATED_DIR}/textures/vectors")
