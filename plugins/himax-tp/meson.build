cargs = ['-DG_LOG_DOMAIN="FuPluginHimaxTp"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('himax-tp.quirk')
plugin_builtins += static_library('fu_plugin_himax_tp',
  rustgen.process('fu-himax-tp.rs'),
  sources: [
    'fu-himax-tp-hid-device.c',
    'fu-himax-tp-firmware.c',
    'fu-himax-tp-plugin.c',
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

device_tests += files('tests/himax-touch.json')
