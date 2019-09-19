cargs = ['-DG_LOG_DOMAIN="FuPluginWacomRecovery"']

shared_module('fu_plugin_wacom_recovery',
  fu_hash,
  sources : [
    'fu-plugin-wacom-recovery.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : [
    cargs,
  ],
  link_with : [
    libfwupdprivate,
  ],
  dependencies : [
    plugin_deps,
    libgpiod
  ],
)
