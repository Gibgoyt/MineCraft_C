; Constants of asm/src/embed_assets/index.asm. See its README.md.
%ifndef MC_ASM_EMBED_ASSETS_INDEX_INC
%define MC_ASM_EMBED_ASSETS_INDEX_INC

; Found through the include root build/<preset>/generated, which NASM searches for incbin too. cmake/generated.cmake makes it.
%define EMBED_ASSETS_TEXTURE_BLOB_PATH "textures/index.mctb"
; TEXTURE_BLOB_ALIGNMENT in run/include/types/texture_blob/index.h. The blob's reader refuses anything less.
%define EMBED_ASSETS_TEXTURE_BLOB_ALIGN 64

%endif
