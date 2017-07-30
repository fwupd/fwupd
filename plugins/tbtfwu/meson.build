cargs = ['-DG_LOG_DOMAIN="FuPluginTbtfwu"']

shared_module('fu_plugin_tbtfwu',
  sources : [
    'fu-plugin-tbtfwu.c',
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
    tbtfwu,
    gudev,
    efivar,
  ],
)
