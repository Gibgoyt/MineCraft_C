# flags.cmake may set them, and a comment may name -Wno-padded
set(MC_C_FLAGS -Wall)
set(MC_NASM_FLAGS -w+all -Werror)
target_compile_options(mc_run PRIVATE -O0)
set(CMAKE_C_FLAGS_DEBUG "")
