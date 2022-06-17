if get_option('plugin_emmc').require(gudev.found(),
    error_message: 'gudev is needed for plugin_emmc').allowed()

cargs = ['-DG_LOG_DOMAIN="FuPluginEmmc"']

plugin_quirks += join_paths(meson.current_source_dir(), 'emmc.quirk')

shared_module('fu_plugin_emmc',
  fu_hash,
  sources: [
    'fu-plugin-emmc.c',
    'fu-emmc-device.c',
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
