if get_option('plugin_spi')
cargs = ['-DG_LOG_DOMAIN="FuPluginSpi"']

shared_module('fu_plugin_spi',
  fu_hash,
  sources : [
    'fu-ifd-common.c',        # fuzzing
    'fu-ifd-firmware.c',      # fuzzing
    'fu-ifd-image.c',         # fuzzing
    'fu-plugin-spi.c',
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
