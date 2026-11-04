/* See README.md. The tables below are the lint's whole policy on identifiers: changing what is banned or where it is
 * allowed means changing a row here, and tests/compile/standards_lint fails until the new rule has a fixture. */
#include "standards_lint/catalog/index.h"

#include "standards_lint/support/index.h"

#include <string.h>

/* Section 3, rule 4: structs shared with CUDA (gpu_bridge), NASM (the kernels' C prototypes) and files (saves). */
static const char *const shared_struct_paths[] = {"run/include/gpu_bridge/", "run/include/simd/", "run/include/save/",
                                                  nullptr};

/* plans/CODING_STANDARD.md section 2: "the only file where the lint accepts a floating-point base type". */
static const char *const typedef_site[] = {"run/include/types/index.h", nullptr};
/* Section 4: only the heap calls the OS allocator. */
static const char *const heap_only[] = {"run/src/heap/index.c", nullptr};
/* Section 4 exceptions. The dumb buffer is device memory. A compile tool cannot include run/ (REPO_STRUCTURE section
 * 11), so its Owner maps its one process arena itself. The harness's test arena is the standard's "test arena". */
static const char *const os_map[] = {"run/src/heap/index.c", "run/src/display/device/index.c", "compile/src/*/index.c",
                                     "tests/harness/index.c", nullptr};
/* Section 4: GPU memory. */
static const char *const device_memory_only[] = {"cuda/src/device_memory/index.cu", nullptr};
/* Section 4: "Every such static ... is listed in the lint's allowlist." */
static const char *const borrowed_static_files[] = {"run/src/display/vt_guard/index.c", nullptr};

/* Section 1: the one exception to no sockets, a Unix socket to the X server on this machine. */
static const char *const x11_socket[] = {"run/src/display/x11/index.c", nullptr};

static const char why_type[] = "says nothing about its size; use the sized types in plans/CODING_STANDARD.md section 2";
static const char why_float[] = "is a floating-point base type; use float32_t or float64_t";
static const char why_alloc[] =
    "is banned; use heap_reserve, heap_release or arena_alloc (plans/CODING_STANDARD.md section 4)";
static const char why_device[] = "is only for cuda/src/device_memory/ (plans/CODING_STANDARD.md section 4)";
static const char why_net[] =
    "is networking; there are no sockets in this project (plans/CODING_STANDARD.md section 1)";
static const char why_cpp[] = "is outside the C-like subset for CUDA (plans/CODING_STANDARD.md section 8)";
static const char why_pragma[] =
    "is a pragma the #pragma rules cannot see; write none (plans/CODING_STANDARD.md section 3)";
static const char why_load[] =
    "loads code at run time, past the allowed library list (plans/CODING_STANDARD.md section 1)";

#define MC_WORD(text, why_text, paths, langs, member)                                                                  \
    {.word = text,                                                                                                     \
     .rule = "banned:" text,                                                                                           \
     .why = why_text,                                                                                                  \
     .allowed_paths = paths,                                                                                           \
     .languages = langs,                                                                                               \
     .member_ok = member,                                                                                              \
     ._pad0 = {0}}

static const lint_word_t lint_words[] = {
    /* Section 2: banned keywords. */
    MC_WORD("int", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("short", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("long", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("unsigned", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("signed", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("size_t", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("ssize_t", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("__int128", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("_BitInt", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("__int128_t", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("__uint128_t", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("__SIZE_TYPE__", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("ptrdiff_t", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("intmax_t", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("uintmax_t", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("intptr_t", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("wchar_t", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("auto", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("__auto_type", why_type, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("float", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("double", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("_Float16", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("_Float32", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("_Float32x", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("_Float64", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("_Float64x", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("_Float128", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("__float128", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("_Decimal32", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("_Decimal64", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("_Decimal128", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("__float80", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("__ibm128", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("_Complex", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("__complex__", why_float, typedef_site, LINT_LANGUAGE_BOTH, 0),
    /* Section 4: allocation is in one place. */
    MC_WORD("malloc", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("calloc", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("realloc", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("reallocarray", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("free", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("aligned_alloc", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("posix_memalign", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("memalign", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("valloc", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("pvalloc", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("strdup", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("strndup", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("mmap", why_alloc, os_map, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("munmap", why_alloc, os_map, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("mremap", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("brk", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("sbrk", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("alloca", why_alloc, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__builtin_alloca", why_alloc, nullptr, LINT_LANGUAGE_BOTH, 1),
    /* The same allocators under other names. */
    MC_WORD("__alloca", why_alloc, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__builtin_alloca_with_align", why_alloc, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__builtin_malloc", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__builtin_calloc", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__builtin_realloc", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__builtin_free", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__libc_malloc", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__libc_calloc", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__libc_realloc", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__libc_free", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("mmap64", why_alloc, os_map, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__mmap", why_alloc, os_map, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("SYS_mmap", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("SYS_munmap", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("SYS_mremap", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("SYS_brk", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__NR_mmap", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__NR_munmap", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__NR_mremap", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("__NR_brk", why_alloc, heap_only, LINT_LANGUAGE_BOTH, 1),
    /* libc functions that hand their caller a malloc'd block to own. */
    MC_WORD("asprintf", why_alloc, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("vasprintf", why_alloc, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("getline", why_alloc, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("getdelim", why_alloc, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("open_memstream", why_alloc, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("wcsdup", why_alloc, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("cudaMalloc", why_device, device_memory_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("cudaFree", why_device, device_memory_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("cudaMallocManaged", why_device, device_memory_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("cudaMallocHost", why_device, device_memory_only, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("cudaFreeHost", why_device, device_memory_only, LINT_LANGUAGE_BOTH, 1),
    /* Section 1: no networking. */
    MC_WORD("socket", why_net, x11_socket, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("socketpair", why_net, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("connect", why_net, x11_socket, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("bind", why_net, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("listen", why_net, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("accept", why_net, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("accept4", why_net, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("send", why_net, x11_socket, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("sendto", why_net, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("sendmsg", why_net, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("recv", why_net, x11_socket, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("recvfrom", why_net, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("recvmsg", why_net, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("getaddrinfo", why_net, nullptr, LINT_LANGUAGE_BOTH, 1),
    /* Section 1: a banned library is just as banned when it is loaded by name. */
    MC_WORD("dlopen", why_load, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("dlmopen", why_load, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("dlsym", why_load, nullptr, LINT_LANGUAGE_BOTH, 1),
    MC_WORD("dlvsym", why_load, nullptr, LINT_LANGUAGE_BOTH, 1),
    /* Sections 3 and 7: the operator form of #pragma, which is a string to a token scan. */
    MC_WORD("_Pragma", why_pragma, nullptr, LINT_LANGUAGE_BOTH, 0),
    MC_WORD("__pragma", why_pragma, nullptr, LINT_LANGUAGE_BOTH, 0),
    /* Section 8: nvcc compiles .cu as C++, so these words exist there. In C they could be ordinary names. */
    MC_WORD("new", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("delete", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("try", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("catch", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("throw", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("class", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("template", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("virtual", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("typeid", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("dynamic_cast", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("namespace", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("operator", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("std", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("public", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("private", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("protected", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("friend", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("this", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
    MC_WORD("using", why_cpp, nullptr, LINT_LANGUAGE_CUDA, 1),
};
enum { LINT_WORD_COUNT = sizeof(lint_words) / sizeof(lint_words[0]) };

/* The rules that are not rows of lint_words. */
static const char *const lint_other_rules[] = {
    /* Section 2: literals. */
    "literal_unsuffixed_float",
    "literal_wrong_suffix",
    "literal_cuda_suffix",
    "literal_shared_header",
    /* Section 3: struct layout. */
    "packed",
    "pragma_pack",
    "pragma_other",
    "bitfield",
    "bool_in_struct",
    "struct_no_typedef",
    "struct_no_sizeof_assert",
    "struct_no_alignof_assert",
    "struct_no_offsetof_assert",
    /* Section 4: the memory law. */
    "owning_static",
    "borrowed_not_allowlisted",
    "borrowed_no_comment",
    "tier_root_alloc",
    "tier_arena_heap",
    "tier_borrow_alloc",
    "init_deinit_pair",
    "recursion",
    "recursion_cuda",
    /* Section 5: files and directories. */
    "file_name",
    "file_location",
    "header_location",
    "one_module",
    "cuda_header_ext",
    "line_limit",
    "readme_missing",
    "readme_fields",
    "readme_tier",
    "stray_file",
    "foreign_source",
    /* Section 9: the flags live in cmake/flags.cmake, and no .cmake file can take one away for a single source. */
    "cmake_flags_outside",
    "cmake_weakening_flag",
    /* Section 2: the sized types are the only integer types. */
    "int_fast_least",
    /* Section 6: naming and style. */
    "type_name",
    "goto_cleanup_only",
    /* Spellings a token scan would otherwise read as something harmless. */
    "digraph",
    "line_splice_in_token",
    /* Section 8: the C-like subset. */
    "cuda_struct_method",
    "cuda_inheritance",
    /* Section 7: SIMD. */
    "inline_asm",
    "intrinsic",
    "vector_extension",
    "simd_target",
    "asm_default_rel",
    "asm_gnu_stack",
    "asm_vzeroupper",
    "asm_include_ext",
    "asm_banned_register",
    /* REPO_STRUCTURE section 11, and section 8 of the standard: dependency direction. */
    "unknown_system",
    "include_upward",
    "include_sibling",
    "include_run_from_compile",
    "include_compile_tool",
    "include_cuda_from_c",
    "include_source",
    "include_relative",
    "include_computed",
    "cuda_cpp_header",
    /* Section 1: libraries, and section 7: the intrinsics headers. */
    "banned_header:intrinsics",
    "banned_header:opengl",
    "banned_header:vulkan",
    "banned_header:egl",
    "banned_header:gbm",
    "banned_header:libdrm",
    "banned_header:x11",
    "banned_header:xcb",
    "banned_header:wayland",
    "banned_header:sdl",
    "banned_header:glfw",
    "banned_header:optix",
    "banned_header:stb",
    "banned_header:zlib",
    "banned_header:libpng",
    "banned_header:libyaml",
    "banned_header:sockets",
    "banned_header:dlfcn",
    "banned_header:cuda_in_c",
};
enum { LINT_OTHER_RULE_COUNT = sizeof(lint_other_rules) / sizeof(lint_other_rules[0]) };

const lint_word_t *lint_catalog_find_word(const char *text, uint64_t length) {
    /* Called for every identifier in the repository, and most of them are not banned. 27 is the longest word above; a
     * longer one would never match, and its fixture would fail. */
    if (length > 27u) {
        return nullptr;
    }
    for (uint32_t i = 0; i < LINT_WORD_COUNT; i++) {
        const lint_word_t *row = &lint_words[i];
        if (row->word[0] == text[0] && lint_text_equal(text, length, row->word)) {
            return row;
        }
    }
    return nullptr;
}

uint32_t lint_catalog_rule_count(void) {
    return LINT_WORD_COUNT + LINT_OTHER_RULE_COUNT;
}

const char *lint_catalog_rule_at(uint32_t index) {
    return index < LINT_WORD_COUNT ? lint_words[index].rule : lint_other_rules[index - LINT_WORD_COUNT];
}

bool lint_catalog_has_rule(const char *rule) {
    const uint32_t count = lint_catalog_rule_count();
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(lint_catalog_rule_at(i), rule) == 0) {
            return true;
        }
    }
    return false;
}

bool lint_catalog_borrowed_static_allowed(const char *path) {
    for (uint32_t i = 0; borrowed_static_files[i] != nullptr; i++) {
        if (lint_path_matches(borrowed_static_files[i], path)) {
            return true;
        }
    }
    return false;
}

bool lint_catalog_struct_is_shared(const char *path) {
    for (uint32_t i = 0; shared_struct_paths[i] != nullptr; i++) {
        if (lint_path_matches(shared_struct_paths[i], path)) {
            return true;
        }
    }
    return false;
}
