host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginSunplusCamera"']
plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('sunplus-camera.quirk')

plugin_builtins += static_library('fu_plugin_sunplus_camera',
  rustgen.process('fu-sunplus-camera.rs'),
  sources: [
    'fu-sunplus-camera-device.c',
    'fu-sunplus-camera-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/sunplus-camera.json',
)
