
cargs = ['-DG_LOG_DOMAIN="FuPluginLenovoLdc"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('lenovo-ldc.quirk')
plugin_builtins += static_library('fu_plugin_lenovo_ldc',
  rustgen.process('fu-lenovo-ldc.rs'),
  sources: [
    'fu-lenovo-ldc-common.c',
    'fu-lenovo-ldc-device.c',
    'fu-lenovo-ldc-firmware.c',
    'fu-lenovo-ldc-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

executable(
  'main',
  sources: ['main.c'],
  include_directories: plugin_incdirs,
  dependencies: plugin_deps,
  link_with: [
    fwupd,
    fwupdplugin,
  ],
)

#device_tests += files(
#  'tests/vendor-example.json',
#  'tests/vendor-example-setup.json',
#)
