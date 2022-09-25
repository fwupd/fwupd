if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginGoodixMoc"']

plugin_quirks += join_paths(meson.current_source_dir(), 'goodixmoc.quirk')

shared_module('fu_plugin_goodixmoc',
  fu_hash,
  sources: [
    'fu-goodixmoc-device.c',
    'fu-plugin-goodixmoc.c',
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
