cargs = ['-DG_LOG_DOMAIN="FuPluginEbitdo"']

install_data(['ebitdo.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

install_data(['ebitdo.png'],
  install_dir : join_paths(datadir, 'icons', 'fwupd', 'plugins')
)

shared_module('fu_plugin_ebitdo',
  fu_hash,
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
  link_with : [
    libfwupdprivate,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)
