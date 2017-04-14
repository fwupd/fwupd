docbook2man = find_program('docbook2man', required : false)
if docbook2man.found()
  custom_target('fwupdmgr-man',
    input : 'fwupdmgr.sgml',
    output : 'fwupdmgr.1.gz',
    command : [
      docbook2man, '@INPUT@',
      '--output', '@OUTPUT@',
    ],
    install : true,
    install_dir : join_paths(get_option('mandir'), 'man1'),
  )
  custom_target('dfu-tool-man',
    input : 'dfu-tool.sgml',
    output : 'dfu-tool.1.gz',
    command : [
      docbook2man, '@INPUT@',
      '--output', '@OUTPUT@',
    ],
    install : true,
    install_dir : join_paths(get_option('mandir'), 'man1'),
  )
endif
