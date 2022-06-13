cargs = ['-DG_LOG_DOMAIN="FuPluginVbe"']

shared_module('fu_plugin_vbe',
  fu_hash,
  sources : [
    'fu-plugin-vbe.c',
    'fu-vbe-device.c',
    'fu-vbe-simple-device.c',
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
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  dependencies : [
    plugin_deps,
  ],
)
