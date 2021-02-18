if get_option('systemd')
if host_machine.system() != 'linux'
  error('linux is required for systemd')
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginLogind"']

shared_module('fu_plugin_logind',
  fu_hash,
  sources : [
    'fu-plugin-logind.c',
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
