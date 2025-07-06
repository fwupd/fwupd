if not allow_uefi
  subdir_done()
endif

cargs = ['-DG_LOG_DOMAIN="FuPluginUefiSbat"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_builtins += static_library('fu_plugin_uefi_sbat',
  sources: [
    'fu-uefi-sbat-device.c',
    'fu-uefi-sbat-firmware.c',
    'fu-uefi-sbat-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
