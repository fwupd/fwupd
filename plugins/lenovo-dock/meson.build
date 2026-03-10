
cargs = ['-DG_LOG_DOMAIN="FuPluginLenovoDock"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('lenovo-dock.quirk')
plugin_builtins += static_library('fu_plugin_lenovo_dock',
  rustgen.process('fu-lenovo-dock.rs'),
  sources: [
    'fu-lenovo-dock-device.c',
    'fu-lenovo-dock-firmware.c',
    'fu-lenovo-dock-image.c',
    'fu-lenovo-dock-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

executable(
  'main',
  rustgen.process('fu-lenovo-dock.rs'),
  sources: ['main.c'],
  include_directories: plugin_incdirs,
  dependencies: plugin_deps,
  link_with: [
    fwupd,
    fwupdplugin,
  ],
)

device_tests += files('tests/lenovo-dock.json')
