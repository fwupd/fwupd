cargs = ['-DG_LOG_DOMAIN="FuPluginHimaxtp"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('himaxtp.quirk')
plugin_builtins += static_library('fu_plugin_himaxtp',
  rustgen.process('fu-himaxtp.rs'),
  sources: [
    'fu-himaxtp-hid-device.c',
    'fu-himaxtp-firmware.c',
    'fu-himaxtp-plugin.c',
  ],
  include_directories: [ 
    plugin_incdirs,
  ],
  link_with: [ 
    plugin_libs,
  ],
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  dependencies: plugin_deps,
)
