SECTIONS
{
    . = SIZEOF_HEADERS;
    .hash : { *(.hash) } : text
    .gnu.hash : { *(.gnu.hash) } : text
    .dynsym : { *(.dynsym) } : text
    .dynstr : { *(.dynstr) } : text
    .dynamic : { *(.dynamic) } : dynamic : text

    .gnu.version : { *(.gnu.version) } : text
    .gnu.version_d : { *(.gnu.version_d) } : text
    .gnu.version_r : { *(.gnu.version_r) } : text

    .eh_frame_hdr : { *(.eh_frame_hdr) } : eh_frame_hdr : text
    .eh_frame : { *(.eh_frame) } : text
    .text : { *(.text*) } : text
}

/* Enforce single PT_LOAD segment by specifying all
   headers/segments using PHDRS */
PHDRS
{
    text PT_LOAD FILEHDR PHDRS FLAGS(5); /*rd+exec*/
    dynamic PT_DYNAMIC FLAGS(4); /*rdonly*/
    /*note PT_NOTE FLAGS(4);*/
    eh_frame_hdr PT_GNU_EH_FRAME FLAGS(4);
}
