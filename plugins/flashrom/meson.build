if get_option('plugin_flashrom')
if not get_option('libflashrom')
  error('libflashrom is required for plugin_flashrom')
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginFlashrom"']

install_data(['flashrom.quirk'],
  install_dir: join_paths(get_option('datadir'), 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_flashrom',
  fu_hash,
  sources : [
    'fu-flashrom-device.c',
    'fu-flashrom-internal-device.c',
    'fu-flashrom-lspcon-i2c-spi-device.c',
    'fu-plugin-flashrom.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  dependencies : [
    plugin_deps,
  ],
)
endif
