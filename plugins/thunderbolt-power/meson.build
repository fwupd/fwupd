cargs = ['-DG_LOG_DOMAIN="FuPluginThunderbolt"']

fu_plugin_thunderbolt_power = shared_module('fu_plugin_thunderbolt_power',
  fu_hash,
  sources : [
    'fu-plugin-thunderbolt-power.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    libfwupdprivate,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)
