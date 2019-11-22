cargs = ['-DG_LOG_DOMAIN="FuPluginThunderbolt"']

fu_plugin_thunderbolt_power = shared_module('fu_plugin_thunderbolt_power',
  fu_hash,
  sources : [
    'fu-plugin-thunderbolt-power.c',
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
