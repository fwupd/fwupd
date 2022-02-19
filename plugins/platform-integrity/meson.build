if get_option('hsi') and get_option('plugin_platform_integrity')
cargs = ['-DG_LOG_DOMAIN="FuPluginPlatformIntegrity"']

install_data([
  'platform-integrity.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

if libsystemd.found()
install_data(['fwupd-platform-integrity.conf'],
  install_dir: systemd_modules_load_dir,
)
endif

shared_module('fu_plugin_platform_integrity',
  fu_hash,
  sources : [
    'fu-plugin-platform-integrity.c',
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
