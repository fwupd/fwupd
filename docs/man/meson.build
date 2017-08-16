docbook2man = find_program('docbook2man')
custom_target('fwupdmgr-man',
  input : 'fwupdmgr.sgml',
  output : 'fwupdmgr.1',
  command : [
    docbook2man, '@INPUT@',
    '--output', meson.current_build_dir(),
  ],
  install : true,
  install_dir : join_paths(get_option('mandir'), 'man1'),
)
custom_target('dfu-tool-man',
  input : 'dfu-tool.sgml',
  output : 'dfu-tool.1',
  command : [
    docbook2man, '@INPUT@',
    '--output', meson.current_build_dir(),
  ],
  install : true,
  install_dir : join_paths(get_option('mandir'), 'man1'),
)
