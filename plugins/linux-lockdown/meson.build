cargs = ['-DG_LOG_DOMAIN="FuPluginLinuxLockdown"']

if build_plugins
  shared_module('fu_plugin_linux_lockdown',
    fu_hash,
    sources : [
      'fu-plugin-linux-lockdown.c',
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
