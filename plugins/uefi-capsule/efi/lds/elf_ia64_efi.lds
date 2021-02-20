OUTPUT_FORMAT("elf64-ia64-little")
OUTPUT_ARCH(ia64)
ENTRY(_start_plabel)
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
  . = ALIGN(4096);
  __gp = ALIGN (8) + 0x200000;
  .sdata :
  {
   _data = .;
   *(.got.plt)
   *(.got)
   *(.srodata)
   *(.sdata)
   *(.sbss)
   *(.scommon)
  }
  . = ALIGN(4096);
  .note.gnu.build-id : {
    *(.note.gnu.build-id)
  }
  .data.ident : {
    *(.data.ident)
  }

  . = ALIGN(4096);
  .data :
  {
   *(.rodata*)
   *(.ctors)
   *(.data*)
   *(.gnu.linkonce.d*)
   *(.plabel)	/* data whose relocs we want to ignore */
   /* the EFI loader doesn't seem to like a .bss section, so we stick
      it all into .data: */
   *(.dynbss)
   *(.bss)
   *(COMMON)
  }

  . = ALIGN(4096);
  .sbat :
  {
    _sbat = .;
    *(.sbat)
    *(.sbat.*)
    _esbat = .;
  }
  . = ALIGN(4096);
  .dynamic  : { *(.dynamic) }
  . = ALIGN(4096);
  .rela :
  {
    *(.rela.text)
    *(.rela.data*)
    *(.rela.sdata)
    *(.rela.got)
    *(.rela.gnu.linkonce.d*)
    *(.rela.stab)
    *(.rela.ctors)
  }
  _edata = .;
  _data_size = . - _data;
  . = ALIGN(4096);
  .reloc :		/* This is the PECOFF .reloc section! */
  {
    *(.reloc)
  }
  . = ALIGN(4096);
  .dynsym   : { *(.dynsym) }
  . = ALIGN(4096);
  .dynstr   : { *(.dynstr) }
  /DISCARD/ :
  {
    *(.rela.plabel)
    *(.rela.reloc)
    *(.IA_64.unwind*)
    *(.IA64.unwind*)
  }
  .note.gnu.build-id : { *(.note.gnu.build-id) }
}
