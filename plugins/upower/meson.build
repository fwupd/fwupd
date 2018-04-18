# See https://github.com/hughsie/fwupd/issues/472
# for more details why this is compiled with -O0
# revert it after conclusion to
# https://bugs.launchpad.net/fwupd/+bug/1765134
cargs = ['-DG_LOG_DOMAIN="FuPluginUpower"','-O0']

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
