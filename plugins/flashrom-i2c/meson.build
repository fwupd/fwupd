cargs = ['-DG_LOG_DOMAIN="FuPluginFlashromI2c"']

install_data(['flashrom-i2c.quirk'],
  install_dir: join_paths(get_option('datadir'), 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_flashrom_i2c',
  fu_hash,
  sources : [
    'fu-plugin-flashrom-i2c.c',
    'fu-i2c-device.c',
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
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)
