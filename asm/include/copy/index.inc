; Constants of asm/src/copy/index.asm. See its README.md.
%ifndef MC_ASM_COPY_INDEX_INC
%define MC_ASM_COPY_INDEX_INC

%include "conventions/index.inc"

; One pass of the main loop. Two whole cache lines, so a write-combining buffer always fills and flushes as one burst.
%define COPY_BLOCK_BYTES (4 * YMM_BYTES)
%define COPY_BLOCK_SHIFT 7

%endif
