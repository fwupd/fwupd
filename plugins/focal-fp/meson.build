host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginFocalFp"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('focal-fp.quirk')
plugin_builtins += static_library('fu_plugin_focal_fp',
  rustgen.process('fu-focal-fp.rs'),
  sources: [
    'fu-focal-fp-plugin.c',
    'fu-focal-fp-firmware.c',
    'fu-focal-fp-hid-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
