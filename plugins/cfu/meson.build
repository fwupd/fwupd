if get_option('plugin_cfu').require(gudev.found(),
    error_message: 'gudev is needed for plugin_cfu').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginCfu"']

plugin_quirks += join_paths(meson.current_source_dir(), 'cfu.quirk')

shared_module('fu_plugin_cfu',
  fu_hash,
  sources: [
    'fu-cfu-device.c',
    'fu-cfu-module.c',
    'fu-plugin-cfu.c',
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
