if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginGenesys"']

plugin_quirks += join_paths(meson.current_source_dir(), 'genesys.quirk')

shared_module('fu_plugin_genesys',
  fu_hash,
  sources: [
    'fu-genesys-scaler-firmware.c',   # fuzzing
    'fu-genesys-usbhub-firmware.c',   # fuzzing
    'fu-genesys-scaler-device.c',
    'fu-genesys-usbhub-device.c',
    'fu-plugin-genesys.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
)
endif
