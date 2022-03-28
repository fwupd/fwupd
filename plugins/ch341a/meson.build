if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginCh341a"']

install_data([
  'ch341a.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_ch341a',
  fu_hash,
  sources : [
    'fu-ch341a-cfi-device.c',
    'fu-ch341a-device.c',
    'fu-plugin-ch341a.c',
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
