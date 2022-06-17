if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginSteelSeries"']

plugin_quirks += join_paths(meson.current_source_dir(), 'steelseries.quirk')

shared_module('fu_plugin_steelseries',
  fu_hash,
  sources: [
    'fu-plugin-steelseries.c',
    'fu-steelseries-device.c',
    'fu-steelseries-firmware.c',
    'fu-steelseries-fizz-hid.c',
    'fu-steelseries-fizz-tunnel.c',
    'fu-steelseries-fizz.c',
    'fu-steelseries-mouse.c',
    'fu-steelseries-gamepad.c',
    'fu-steelseries-sonic.c',
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
