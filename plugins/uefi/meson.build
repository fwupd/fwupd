cargs = ['-DG_LOG_DOMAIN="FuPluginUefi"']

install_data(['uefi.conf'],
  install_dir : join_paths(sysconfdir, 'fwupd')
)

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
