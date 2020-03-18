cargs = ['-DG_LOG_DOMAIN="FuPluginCcgx"']

shared_module('fu_plugin_ccgx',
  fu_hash,
  sources : [
    'fu-plugin-ccgx.c',
    'fu-ccgx-cyacd-firmware.c',
    'fu-ccgx-cyacd-firmware-image.c',
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
    gudev,
  ],
)
