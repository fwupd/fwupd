host_machine.system() in ['linux', 'android'] or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginNovatekTs"']

plugins += { meson.current_source_dir().split('/')[-1] : true }

plugin_quirks += files('novatek-ts.quirk')
plugin_builtins += static_library(
  'fu_plugin_novatek_ts',
  rustgen.process('fu-novatek-ts.rs'),
  sources: [
    'fu-novatek-ts-device.c',
    'fu-novatek-ts-firmware.c',
    'fu-novatek-ts-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files('tests/novatek-ts.json')
