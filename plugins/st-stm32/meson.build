
cargs = ['-DG_LOG_DOMAIN="FuPluginStStm32"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('st-stm32.quirk')
plugin_builtins += static_library('fu_plugin_st_stm32',
  rustgen.process('fu-st-stm32.rs'),
  sources: [
    'fu-st-stm32-common.c',
    'fu-st-stm32-device.c',
    'fu-st-stm32-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/st-stm32.json',
  'tests/st-stm32-setup.json',
)
