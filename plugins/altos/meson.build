cargs = ['-DG_LOG_DOMAIN="FuPluginAltos"']

shared_module('fu_plugin_altos',
  sources : [
    'fu-plugin-altos.c',
    'fu-device-altos.c',
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
    gudev,
  ],
  link_with : [
    dfu,
  ],
)

executable(
  'fu-altos-tool',
  sources : [
    'fu-altos-tool.c',
    'fu-device.c',
    'fu-device-altos.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../libfwupd'),
    include_directories('../../src'),
  ],
  dependencies : [
    plugin_deps,
    gudev,
  ],
  link_with : [
    fwupd,
    dfu,
  ],
  c_args : cargs,
)
