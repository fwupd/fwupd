cargs = ['-DG_LOG_DOMAIN="FuPluginUnifying"']

shared_module('fu_plugin_unifying',
  sources : [
    'fu-plugin-unifying.c',
    'fu-device-unifying.c',
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

executable(
  'fu-unifying-util',
  sources : [
    'fu-unifying-tool.c',
    'fu-device.c',
    'fu-device-unifying.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  dependencies : [
    plugin_deps,
  ],
  link_with : [
    fwupd,
  ],
  c_args : cargs,
)
