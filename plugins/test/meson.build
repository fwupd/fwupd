cargs = ['-DG_LOG_DOMAIN="FuPluginTest"']

install_dummy = false
if get_option('plugin_dummy')
  install_dummy = true
endif

shared_module('fu_plugin_test',
  sources : [
    'fu-plugin-test.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : install_dummy,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)
