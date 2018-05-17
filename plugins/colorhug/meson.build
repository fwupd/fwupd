cargs = ['-DG_LOG_DOMAIN="FuPluginColorHug"']

shared_module('fu_plugin_colorhug',
  sources : [
    'fu-colorhug-device.c',
    'fu-plugin-colorhug.c',
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
    colorhug,
  ],
)

colorhug_static = static_library('colorhug_static',
  sources : [
    'fu-colorhug-device.c',
    'fu-plugin-colorhug.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : false,
  c_args : cargs,
  dependencies : [
    plugin_deps,
    colorhug,
  ],
)
