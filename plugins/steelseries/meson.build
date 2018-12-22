cargs = ['-DG_LOG_DOMAIN="FuPluginSteelSeries"']

install_data(['steelseries.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_steelseries',
  sources : [
    'fu-plugin-steelseries.c',
    'fu-steelseries-device.c',
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
