cargs = ['-DG_LOG_DOMAIN="FuPluginAltos"']

shared_module('fu_plugin_altos',
  sources : [
    'fu-altos-device.c',
    'fu-altos-firmware.c',
    'fu-plugin-altos.c',
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
    gudev,
    libelf,
    plugin_deps,
  ],
)
