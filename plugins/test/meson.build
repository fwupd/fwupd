# See https://github.com/hughsie/fwupd/issues/472
# for more details why this is compiled with -O0
# revert it after conclusion to
# https://bugs.launchpad.net/fwupd/+bug/D1765134
cargs = ['-DG_LOG_DOMAIN="FuPluginTest"','-O0']

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
