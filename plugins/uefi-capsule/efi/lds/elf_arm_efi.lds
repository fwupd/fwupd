OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
SECTIONS
{
  .text 0x0 : {
    _text = .;
    *(.text.head)
    *(.text)
    *(.text.*)
    *(.gnu.linkonce.t.*)
    *(.srodata)
    *(.rodata*)
    . = ALIGN(16);
    _etext = .;
  }
  .dynamic  : { *(.dynamic) }

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
   *(.sdata)
   *(.data)
   *(.data1)
   *(.data*)
   *(.got.plt)
   *(.got)

   /* the EFI loader doesn't seem to like a .bss section, so we stick
      it all into .data: */
   . = ALIGN(16);
   _bss = .;
   *(.sbss)
   *(.scommon)
   *(.dynbss)
   *(.bss)
   *(COMMON)
   . = ALIGN(16);
   _bss_end = .;
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
  .rel :
  {
    *(.rel.dyn)
    *(.rel.plt)
    *(.rel.got)
    *(.rel.data)
    *(.rel.data*)
  }
  _edata = .;
  _data_size = . - _data;

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
