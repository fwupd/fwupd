cargs = ['-DG_LOG_DOMAIN="FuPluginAmt"']

shared_module('fu_plugin_amt',
  fu_hash,
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
  link_with : [
    libfwupdprivate,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)
