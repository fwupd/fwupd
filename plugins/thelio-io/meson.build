cargs = ['-DG_LOG_DOMAIN="FuPluginThelioIo"']

install_data(['thelio-io.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_thelio_io',
  fu_hash,
  sources : [
    'fu-plugin-thelio-io.c',
    'fu-thelio-io-device.c',
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
