if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginColorHug"']

plugin_quirks += join_paths(meson.current_source_dir(), 'colorhug.quirk')

shared_module('fu_plugin_colorhug',
  fu_hash,
  sources: [
    'fu-colorhug-common.c',
    'fu-colorhug-device.c',
    'fu-plugin-colorhug.c',
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
