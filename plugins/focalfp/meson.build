if gudev.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginFocalfp"']

plugin_quirks += files('focalfp.quirk')

shared_module('fu_plugin_focalfp',
  fu_hash,
  sources: [
    'fu-plugin-focalfp.c',
    'fu-focalfp-firmware.c',
    'fu-focalfp-hid-device.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  dependencies: [
    plugin_deps,
  ],
)
endif
