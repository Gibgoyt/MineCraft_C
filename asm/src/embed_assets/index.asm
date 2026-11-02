; The generated texture blob, linked into the program as read-only data. There is no code here, so no register or ABI rule
; has anything to apply to. Declared for C in run/include/simd/embed_assets/index.h. See README.md.
default rel
section .note.GNU-stack noalloc noexec nowrite progbits

%include "embed_assets/index.inc"

section .rodata align=EMBED_ASSETS_TEXTURE_BLOB_ALIGN
global embed_assets_texture_blob:data (embed_assets_texture_blob_end - embed_assets_texture_blob)
global embed_assets_texture_blob_bytes:data 8

align EMBED_ASSETS_TEXTURE_BLOB_ALIGN, db 0
embed_assets_texture_blob:
    incbin EMBED_ASSETS_TEXTURE_BLOB_PATH
embed_assets_texture_blob_end:

align 8, db 0
embed_assets_texture_blob_bytes:
    dq embed_assets_texture_blob_end - embed_assets_texture_blob
