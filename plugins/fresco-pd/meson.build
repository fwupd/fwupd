if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginFrescoPd"']

plugin_quirks += files('fresco-pd.quirk')

shared_module('fu_plugin_fresco_pd',
  fu_hash,
  sources: [
    'fu-plugin-fresco-pd.c',
    'fu-fresco-pd-common.c',
    'fu-fresco-pd-device.c',
    'fu-fresco-pd-firmware.c',
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
