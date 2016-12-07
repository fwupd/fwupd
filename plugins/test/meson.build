cargs = ['-DG_LOG_DOMAIN="FuPluginTest"']

shared_module('fu_plugin_test',
  sources : [
    'fu-plugin-test.c',
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
