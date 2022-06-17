if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginRts54Hid"']

plugin_quirks += join_paths(meson.current_source_dir(), 'rts54hid.quirk')

shared_module('fu_plugin_rts54hid',
  fu_hash,
  sources: [
    'fu-rts54hid-device.c',
    'fu-rts54hid-module.c',
    'fu-plugin-rts54hid.c',
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
