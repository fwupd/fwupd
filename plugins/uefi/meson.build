cargs = ['-DG_LOG_DOMAIN="FuPluginUefi"']

shared_module('fu_plugin_uefi',
  sources : [
    'fu-plugin-uefi.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
    fwup,
  ],
)

con2 = configuration_data()
con2.set('bootdir', get_option('bootdir'))

# replace @bootdir@
configure_file(
  input : 'uefi.conf.in',
  output : 'uefi.conf',
  configuration : con2,
  install: true,
  install_dir : join_paths(sysconfdir, 'fwupd')
)

