if get_option('plugin_amt').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginAmt"']

plugin_quirks += join_paths(meson.current_source_dir(), 'amt.quirk')

shared_module('fu_plugin_amt',
  fu_hash,
  sources: [
    'fu-plugin-amt.c',
    'fu-amt-device.c',
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
