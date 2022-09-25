if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginAnalogix"']

plugin_quirks += join_paths(meson.current_source_dir(), 'analogix.quirk')

shared_module('fu_plugin_analogix',
  fu_hash,
  sources: [
    'fu-plugin-analogix.c',
    'fu-analogix-device.c',
    'fu-analogix-common.c',
    'fu-analogix-firmware.c',
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
