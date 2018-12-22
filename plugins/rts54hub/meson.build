cargs = ['-DG_LOG_DOMAIN="FuPluginRts54Hub"']

install_data([
  'rts54hub.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_rts54hub',
  sources : [
    'fu-rts54hub-device.c',
    'fu-plugin-rts54hub.c',
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
