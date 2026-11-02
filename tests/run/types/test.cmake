# Included by mc_add_test in cmake/targets.cmake. The compiler half of this test judges the same flags the build uses, so
# a flag dropped from cmake/flags.cmake fails it.
set(mc_types_tmp "${CMAKE_BINARY_DIR}/test_tmp/run_types")
file(MAKE_DIRECTORY "${mc_types_tmp}")
set(mc_types_c_flags ${MC_C_FLAGS})
list(REMOVE_ITEM mc_types_c_flags -fcallgraph-info=su)
set(MC_TEST_ARGS
  --cc "${CMAKE_C_COMPILER}"
  --nvcc "${CMAKE_CUDA_COMPILER}"
  --nvcc-arch "sm_${CMAKE_CUDA_ARCHITECTURES}"
  --fixtures "${CMAKE_CURRENT_LIST_DIR}/fixtures"
  --tmp "${mc_types_tmp}"
  --game $<TARGET_FILE:minecraft_c>
  --include "${CMAKE_SOURCE_DIR}/run/include"
  --c-flags ${mc_types_c_flags} -D_GNU_SOURCE=1
  --cuda-flags ${MC_CUDA_FLAGS})
