if gudev.found() and gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginNordicHid"']

install_data([
  'nordic-hid.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_nordic_hid',
  fu_hash,
  sources : [
    'fu-plugin-nordic-hid.c',
    'fu-nordic-hid-cfg-channel.c',
    'fu-nordic-hid-firmware.c',
    'fu-nordic-hid-firmware-b0.c',
    'fu-nordic-hid-firmware-mcuboot.c',
    'fu-nordic-hid-archive.c',
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
endif
