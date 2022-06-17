if get_option('plugin_scsi').require(gudev.found(),
    error_message: 'gudev is needed for plugin_scsi').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginScsi"']

plugin_quirks += join_paths(meson.current_source_dir(), 'scsi.quirk')

shared_module('fu_plugin_scsi',
  fu_hash,
  sources: [
    'fu-plugin-scsi.c',
    'fu-scsi-device.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  dependencies: [
    plugin_deps,
  ],
)
endif
