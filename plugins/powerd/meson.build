if host_machine.system() == 'linux'
  cargs = ['-DG_LOG_DOMAIN="FuPluginPowerd"']

shared_module('fu_plugin_cros_powerd',
  fu_hash,
  sources : [
    'fu-plugin-cros-powerd.c',
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
