cargs = ['-DG_LOG_DOMAIN="FuPluginRealtekBillboard"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files('realtek-billboard.quirk')
plugin_builtins += static_library(
  'fu_plugin_realtek_billboard',
  sources: [
    'fu-realtek-billboard-plugin.c',
    'fu-realtek-billboard-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files('tests/realtek-billboard.json')
