if get_option('plugin_powerd').disable_auto_if(host_machine.system() != 'linux').require(gio.version().version_compare('>=2.58'),
     error_message: 'gio 2.58 or later needed for powerd').allowed()
  cargs = ['-DG_LOG_DOMAIN="FuPluginPowerd"']

shared_module('fu_plugin_powerd',
  fu_hash,
  sources : [
    'fu-plugin-powerd.c',
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
