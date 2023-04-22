if get_option('plugin_scsi').require(gudev.found(),
    error_message: 'gudev is needed for plugin_scsi').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginScsi"']

plugin_quirks += files('scsi.quirk')
plugin_builtins += static_library('fu_plugin_scsi',
  structgen.process('fu-scsi.struct'),
  sources: [
    'fu-scsi-plugin.c',
    'fu-scsi-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
endif
