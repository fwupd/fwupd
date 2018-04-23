cargs = ['-DG_LOG_DOMAIN="FuPluginUpower"']

shared_module('fu_plugin_upower',
  sources : [
    'fu-plugin-upower.c',
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
