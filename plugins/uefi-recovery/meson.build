if allow_uefi
cargs = ['-DG_LOG_DOMAIN="FuPluginUefiRecovery"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('uefi-recovery.quirk')
plugin_builtins += static_library('fu_plugin_uefi_recovery',
  sources: [
    'fu-uefi-recovery-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: [
    cargs,
  ],
  dependencies: plugin_deps,
)
endif
