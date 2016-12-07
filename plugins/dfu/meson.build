cargs = ['-DG_LOG_DOMAIN="FuPluginDfu"']

shared_module('fu_plugin_dfu',
  sources : [
    'fu-plugin-dfu.c',
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
  link_with : [
    dfu,
  ],
)
