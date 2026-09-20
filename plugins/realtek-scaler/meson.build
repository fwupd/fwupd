host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginRealtekScaler"']

plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}
plugin_quirks += files('realtek-scaler.quirk')
plugin_builtins += static_library(
  'fu_plugin_realtek_scaler',
  rustgen.process('fu-realtek-scaler.rs'),
  sources: [
    'fu-realtek-scaler-device.c',
    'fu-realtek-scaler-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files('tests/realtek-scaler.json')
