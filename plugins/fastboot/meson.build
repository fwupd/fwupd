cargs = ['-DG_LOG_DOMAIN="FuPluginFastboot"']

install_data(['fastboot.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_fastboot',
  sources : [
    'fu-plugin-fastboot.c',
    'fu-fastboot-device.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    libfwupdprivate,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)
