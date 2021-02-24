if get_option('gudev')
cargs = ['-DG_LOG_DOMAIN="FuPluginElantp"']

install_data([
  'elantp.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_elantp',
  fu_hash,
  sources : [
    'fu-plugin-elantp.c',
    'fu-elantp-common.c',
    'fu-elantp-firmware.c',   # fuzzing
    'fu-elantp-hid-device.c',
    'fu-elantp-i2c-device.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  dependencies : [
    plugin_deps,
  ],
)
endif
