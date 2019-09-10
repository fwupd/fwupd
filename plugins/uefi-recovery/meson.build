cargs = ['-DG_LOG_DOMAIN="FuPluginUefiRecovery"']

install_data(['uefi-recovery.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_uefi_recovery',
  fu_hash,
  sources : [
    'fu-plugin-uefi-recovery.c',
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
  c_args : [
    cargs,
  ],
  dependencies : [
    plugin_deps,
  ],
)
