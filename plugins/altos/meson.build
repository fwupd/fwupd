cargs = ['-DG_LOG_DOMAIN="FuPluginAltos"']

shared_module('fu_plugin_altos',
  sources : [
    'fu-altos-firmware.c',
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
    gudev,
    libelf,
    plugin_deps,
  ],
)

executable(
  'fu-altos-tool',
  sources : [
    'fu-altos-firmware.c',
    'fu-altos-tool.c',
    'fu-device-altos.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../libfwupd'),
    include_directories('../../src'),
  ],
  dependencies : [
    gudev,
    libelf,
    plugin_deps,
  ],
  link_with : [
    fwupd,
    libfwupdprivate,
  ],
  c_args : cargs,
)
