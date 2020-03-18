cargs = ['-DG_LOG_DOMAIN="FuPluginCpu"']

shared_module('fu_plugin_cpu',
  fu_hash,
  sources : [
    'fu-plugin-cpu.c',
    'fu-cpu-device.c',
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
