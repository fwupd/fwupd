if libsmbios_c.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginDellEsrt"']

install_data(['metadata.xml'],
  install_dir: join_paths(datadir, 'fwupd', 'remotes.d', 'dell-esrt')
)
plugin_builtins += static_library('fu_plugin_dell_esrt',
  sources: [
    'fu-dell-esrt-plugin.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
  ],
  link_with: plugin_libs,
  dependencies: [
    plugin_deps,
    libsmbios_c,
  ],
)

# replace @datadir@
con2 = configuration_data()
con2.set('datadir', datadir)
configure_file(
  input: 'dell-esrt.conf',
  output: 'dell-esrt.conf',
  configuration: con2,
  install: true,
  install_dir: join_paths(sysconfdir, 'fwupd', 'remotes.d'),
)
endif
