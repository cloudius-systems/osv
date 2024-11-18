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
.rela.dyn       :
    {
      *(.rela.init)
      *(.rela.text .rela.text.* .rela.gnu.linkonce.t.*)
      *(.rela.fini)
      *(.rela.rodata .rela.rodata.* .rela.gnu.linkonce.r.*)
      *(.rela.data .rela.data.* .rela.gnu.linkonce.d.*)
      *(.rela.tdata .rela.tdata.* .rela.gnu.linkonce.td.*)
      *(.rela.tbss .rela.tbss.* .rela.gnu.linkonce.tb.*)
      *(.rela.ctors)
      *(.rela.dtors)
      *(.rela.got)
      *(.rela.bss .rela.bss.* .rela.gnu.linkonce.b.*)
      *(.rela.ldata .rela.ldata.* .rela.gnu.linkonce.l.*)
      *(.rela.lbss .rela.lbss.* .rela.gnu.linkonce.lb.*)
      *(.rela.lrodata .rela.lrodata.* .rela.gnu.linkonce.lr.*)
      *(.rela.ifunc)
    } : text
  .rela.plt       :
    {
      *(.rela.plt)
      PROVIDE_HIDDEN (__rela_iplt_start = .);
      *(.rela.iplt)
      PROVIDE_HIDDEN (__rela_iplt_end = .);
    } : text
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
