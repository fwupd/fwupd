cargs = ['-DG_LOG_DOMAIN="FuPluginEbitdo"']

install_data(['ebitdo.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_ebitdo',
  sources : [
    'fu-plugin-ebitdo.c',
    'fu-ebitdo-common.c',
    'fu-ebitdo-device.c',
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
