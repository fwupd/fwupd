if host_machine.system() != 'linux'
  subdir_done()
endif

plugins += {meson.current_source_dir().split('/')[-1]: true}
cargs = ['-DG_LOG_DOMAIN="FuPluginWacomRaw"']

plugin_quirks += files('wacom-raw.quirk')
plugin_builtins += static_library('fu_plugin_wacom_raw',
  rustgen.process('fu-wacom-raw.rs'),
  sources: [
    'fu-wacom-raw-plugin.c',
    'fu-wacom-raw-common.c',
    'fu-wacom-raw-device.c',
    'fu-wacom-raw-aes-device.c',
    'fu-wacom-raw-emr-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: cargs,
  dependencies: plugin_deps,
  link_with: plugin_libs,
)

device_tests += files(
  'tests/wacom-g14t.json',
)
