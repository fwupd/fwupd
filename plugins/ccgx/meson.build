cargs = ['-DG_LOG_DOMAIN="FuPluginCcgx"']

install_data(['ccgx-dock.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_ccgx',
  fu_hash,
  sources : [
    'fu-plugin-ccgx.c',
    'fu-ccgx-dock-bb.c',
    'fu-ccgx-hid.c',
    'fu-ccgx-hpi.c',
    'fu-ccgx-i2c.c',
    'fu-ccgx-common.c',
    'fu-ccgx-cyacd-file.c',
    'fu-ccgx-cyacd-firmware.c',
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
    gudev,
  ],
)
