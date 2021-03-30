OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)
SECTIONS
{
  . = 0;
  ImageBase = .;
  .hash : { *(.hash) }	/* this MUST come first! */
  . = ALIGN(4096);
  .text :
  {
   _text = .;
   *(.text)
   *(.text.*)
   *(.gnu.linkonce.t.*)
   _etext = .;
  }
  .reloc :
  {
   *(.reloc)
  }
  . = ALIGN(4096);
  .note.gnu.build-id : {
    *(.note.gnu.build-id)
  }
  . = ALIGN(4096);
  .data.ident : {
    *(.data.ident)
  }

  . = ALIGN(4096);
  .data :
  {
   _data = .;
   *(.rodata*)
   *(.data)
   *(.data1)
   *(.data.*)
   *(.sdata)
   *(.got.plt)
   *(.got)
   /* the EFI loader doesn't seem to like a .bss section, so we stick
      it all into .data: */
   *(.sbss)
   *(.scommon)
   *(.dynbss)
   *(.bss)
   *(COMMON)
  }

  . = ALIGN(4096);
  .vendor_cert :
  {
   *(.vendor_cert)
  }
  . = ALIGN(4096);
  .dynamic  : { *(.dynamic) }
  . = ALIGN(4096);
  .rel :
  {
    *(.rel.data)
    *(.rel.data.*)
    *(.rel.got)
    *(.rel.stab)
    *(.data.rel.ro.local)
    *(.data.rel.local)
    *(.data.rel.ro)
    *(.data.rel*)
  }
  _edata = .;
  _data_size = . - _data;
  . = ALIGN(4096);
  .sbat :
  {
    _sbat = .;
    *(.sbat)
    *(.sbat.*)
  }
  _esbat = .;
  _sbat_size = . - _sbat;

  . = ALIGN(4096);
  .dynsym   : { *(.dynsym) }
  . = ALIGN(4096);
  .dynstr   : { *(.dynstr) }
  . = ALIGN(4096);
  /DISCARD/ :
  {
    *(.rel.reloc)
    *(.eh_frame)
    *(.note.GNU-stack)
  }
  .comment 0 : { *(.comment) }
}
