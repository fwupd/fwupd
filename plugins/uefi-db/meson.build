allow_uefi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginUefiDb"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('uefi-db.quirk')
plugin_builtins += static_library('fu_plugin_uefi_db',
  sources: [
    'fu-uefi-db-device.c',
    'fu-uefi-db-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
