if host_machine.system() != 'linux'
  subdir_done()
endif

cargs = ['-DG_LOG_DOMAIN="FuPluginScsi"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('scsi.quirk')
plugin_builtins += static_library('fu_plugin_scsi',
  rustgen.process('fu-scsi.rs'),
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
