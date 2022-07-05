if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginIntelUsb4"']

plugin_quirks += join_paths(meson.current_source_dir(), 'intel-usb4.quirk')

shared_module('fu_plugin_intel_usb4',
  fu_hash,
  sources: [
    'fu-intel-usb4-device.c',
    'fu-intel-usb4-firmware.c',
    'fu-intel-usb4-nvm.c',
    'fu-plugin-intel-usb4.c',
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
    gudev,
  ],
)
endif
