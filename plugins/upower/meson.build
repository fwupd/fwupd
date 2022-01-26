if get_option('plugin_upower') and host_machine.system() == 'linux'
cargs = ['-DG_LOG_DOMAIN="FuPluginUpower"']

shared_module('fu_plugin_upower',
  fu_hash,
  sources : [
    'fu-plugin-upower.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)

endif
