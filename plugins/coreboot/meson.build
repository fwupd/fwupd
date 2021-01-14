cargs = ['-DG_LOG_DOMAIN="FuPluginCoreboot"']

shared_module('fu_plugin_coreboot',
  fu_hash,
  sources : [
    'fu-plugin-coreboot.c',
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
  c_args : [
    cargs,
  ],
  dependencies : [
    plugin_deps,
  ],
)
