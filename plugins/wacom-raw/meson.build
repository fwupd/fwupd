cargs = ['-DG_LOG_DOMAIN="FuPluginWacomRaw"']

install_data(['wacom-raw.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_wacom_raw',
  fu_hash,
  sources : [
    'fu-plugin-wacom-raw.c',
    'fu-wacom-common.c',
    'fu-wacom-device.c',
    'fu-wacom-aes-device.c',
    'fu-wacom-emr-device.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../dfu'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
  link_with : [
    dfu,
  ],
)
