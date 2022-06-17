if get_option('plugin_fastboot').require(gusb.found(),
    error_message: 'gusb is needed for plugin_fastboot').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginFastboot"']

plugin_quirks += join_paths(meson.current_source_dir(), 'fastboot.quirk')

shared_module('fu_plugin_fastboot',
  fu_hash,
  sources: [
    'fu-plugin-fastboot.c',
    'fu-fastboot-device.c',
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
