if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginElanfp"']

plugin_quirks += join_paths(meson.current_source_dir(), 'elanfp.quirk')

shared_module('fu_plugin_elanfp',
  fu_hash,
  sources: [
    'fu-plugin-elanfp.c',
    'fu-elanfp-device.c',
    'fu-elanfp-firmware.c'  # fuzzing
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
