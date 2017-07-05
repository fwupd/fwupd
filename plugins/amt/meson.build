cargs = ['-DG_LOG_DOMAIN="FuPluginAmt"']

shared_module('fu_plugin_amt',
  sources : [
    'fu-plugin-amt.c',
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
    uuid,
  ],
)
