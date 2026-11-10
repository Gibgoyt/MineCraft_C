# Run by the target mc_check_textures_local in cmake/generated.cmake, with -D MC_SOURCE_DIR=<checkout>, before anything
# compiles. Fails the build if git tracks any file under textures_local/: that tree holds Mojang's art, made on this
# machine by tools/import_minecraft_default.sh, and this repository is public. Plan 005 phase 9.
find_program(MC_GIT git)
if(NOT MC_GIT OR NOT EXISTS "${MC_SOURCE_DIR}/.git")
  return()  # No git, or no checkout: nothing can be tracked.
endif()
execute_process(COMMAND "${MC_GIT}" -C "${MC_SOURCE_DIR}" ls-files -- textures_local textures_local.new
  OUTPUT_VARIABLE mc_tracked RESULT_VARIABLE mc_git_status ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT mc_git_status EQUAL 0)
  return()  # Not a repository git can read, such as a worktree whose main checkout has gone.
endif()
if(NOT mc_tracked STREQUAL "")
  message(FATAL_ERROR "git tracks files under textures_local/, which holds Mojang's art and must never be committed. "
    "Untrack them with: git rm -r --cached textures_local\nTracked:\n${mc_tracked}")
endif()
