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
    _evtext = .;
    . = ALIGN(4096);
  }
  _etext = .;
  _text_size = . - _text;
  _text_vsize = _evtext - _text;

  . = ALIGN(4096);
  .data :
  {
   _data = .;
   *(.sdata)
   *(.data)
   *(.data1)
   *(.data.*)
   *(.got.plt)
   *(.got)

   *(.dynamic)

   /* the EFI loader doesn't seem to like a .bss section, so we stick
      it all into .data: */
   . = ALIGN(16);
   _bss = .;
   *(.sbss)
   *(.scommon)
   *(.dynbss)
   *(.bss)
   *(COMMON)
   _evdata = .;
   . = ALIGN(4096);
   _bss_end = .;
  }
  _edata = .;
  _data_vsize = _evdata - _data;
  _data_size = . - _data;

  /*
   * Note that _sbat must be the beginning of the data, and _esbat must be the
   * end and must be before any section padding.  The sbat self-check uses
   * _esbat to find the bounds of the data, and if the padding is included, the
   * CSV parser (correctly) rejects the data as having NUL values in one of the
   * required columns.
   */
  . = ALIGN(4096);
  .sbat :
  {
    _sbat = .;
    *(.sbat)
    *(.sbat.*)
    _esbat = .;
    . = ALIGN(4096);
    _epsbat = .;
  }
  _sbat_size = _epsbat - _sbat;
  _sbat_vsize = _esbat - _sbat;

  . = ALIGN(4096);
  .rodata :
  {
    _rodata = .;
    *(.rel.dyn)
    *(.rel.plt)
    *(.rel.got)
    *(.rel.data)
    *(.rel.data*)

    *(.rodata*)
    *(.srodata)
    *(.dynsym)
    *(.dynstr)
    . = ALIGN(16);
    *(.note.gnu.build-id)
    . = ALIGN(4096);
    *(.vendor_cert)
    *(.data.ident)
    _evrodata = .;
    . = ALIGN(4096);
  }
  _erodata = .;
  _rodata_size = . - _rodata;
  _rodata_vsize = _evrodata - _rodata;
  _alldata_size = . - _data;

  /DISCARD/ :
  {
    *(.rel.reloc)
    *(.eh_frame)
    *(.note.GNU-stack)
  }
  .comment 0 : { *(.comment) }
}
