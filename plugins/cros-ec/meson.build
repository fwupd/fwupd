if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginCrosEc"']

plugin_quirks += join_paths(meson.current_source_dir(), 'cros-ec.quirk')

shared_module('fu_plugin_cros_ec',
  fu_hash,
  sources: [
    'fu-plugin-cros-ec.c',
    'fu-cros-ec-usb-device.c',
    'fu-cros-ec-common.c',      # fuzzing
    'fu-cros-ec-firmware.c',    # fuzzing
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
