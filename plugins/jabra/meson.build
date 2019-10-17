cargs = ['-DG_LOG_DOMAIN="FuPluginJabra"']

install_data(['jabra.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_jabra',
  fu_hash,
  sources : [
    'fu-plugin-jabra.c',
    'fu-jabra-device.c',
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
