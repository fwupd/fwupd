host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginEmmc"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('emmc.quirk')
plugin_builtins += static_library('fu_plugin_emmc',
  sources: [
    'fu-emmc-plugin.c',
    'fu-emmc-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

enumeration_data += files('tests/sandisk-da4064-setup.json')
device_tests += files('tests/sandisk-da4064.json')
