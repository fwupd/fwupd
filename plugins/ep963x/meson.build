if host_machine.system() != 'linux'
  subdir_done()
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginEp963x"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('ep963x.quirk')
plugin_builtins += static_library('fu_plugin_ep963x',
  rustgen.process('fu-ep963x.rs'),
  sources: [
    'fu-ep963x-device.c',
    'fu-ep963x-firmware.c',
    'fu-ep963x-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
