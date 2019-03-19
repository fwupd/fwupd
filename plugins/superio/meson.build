cargs = ['-DG_LOG_DOMAIN="FuPluginSuperio"']

install_data(['superio.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_superio',
  fu_hash,
  sources : [
    'fu-plugin-superio.c',
    'fu-superio-device.c',
    'fu-superio-it85-device.c',
    'fu-superio-it89-device.c',
    'fu-superio-common.c',
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
