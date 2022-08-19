if hsi
cargs = ['-DG_LOG_DOMAIN="FuPluginLinuxTainted"']

shared_module('fu_plugin_linux_tainted',
  fu_hash,
  sources: [
    'fu-plugin-linux-tainted.c',
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
