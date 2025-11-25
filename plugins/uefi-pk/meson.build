allow_uefi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginUefiPk"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('uefi-pk.quirk')
plugin_builtins += static_library('fu_plugin_uefi_pk',
  sources: [
    'fu-uefi-pk-device.c',
    'fu-uefi-pk-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
enumeration_data += files('tests/uefi-pk-setup.json')
device_tests += files('tests/uefi-pk.json')
