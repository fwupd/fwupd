allow_uefi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginUefiKek"']

plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('uefi-kek.quirk')
plugin_builtins += static_library('fu_plugin_uefi_kek',
  sources: [
    'fu-uefi-kek-device.c',
    'fu-uefi-kek-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
