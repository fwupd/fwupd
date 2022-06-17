if get_option('hsi') and host_machine.system() == 'linux'
cargs = ['-DG_LOG_DOMAIN="FuPluginLinuxSleep"']

shared_module('fu_plugin_linux_sleep',
  fu_hash,
  sources: [
    'fu-plugin-linux-sleep.c',
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
