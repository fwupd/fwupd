cargs = ['-DG_LOG_DOMAIN="FuPluginSteelSeries"']

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
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)

steelseries_static = static_library('steelseries_static',
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
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)
