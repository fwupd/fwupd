if host_machine.system() == 'linux'
cargs = ['-DG_LOG_DOMAIN="FuPluginUpower"']

install_data(['upower.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d'))

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

install_data(['upower.conf'],
  install_dir:  join_paths(sysconfdir, 'fwupd')
)
endif
