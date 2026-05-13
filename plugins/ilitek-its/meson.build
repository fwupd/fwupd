host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginIlitekIts"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('ilitek-its.quirk')
plugin_builtins += static_library('fu_plugin_ilitek_its',
  rustgen.process('fu-ilitek-its.rs'),
  sources: [
    'fu-ilitek-its-common.c',
    'fu-ilitek-its-device.c',
    'fu-ilitek-its-block.c',
    'fu-ilitek-its-firmware.c',
    'fu-ilitek-its-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/ilitek-touchscreen.json',
)
