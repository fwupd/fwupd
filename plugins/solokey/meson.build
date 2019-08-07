cargs = ['-DG_LOG_DOMAIN="FuPluginSoloKey"']

install_data([
  'solokey.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_solokey',
  fu_hash,
  sources : [
    'fu-solokey-device.c',
    'fu-solokey-firmware.c',
    'fu-plugin-solokey.c',
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
    libjsonglib,
  ],
)
