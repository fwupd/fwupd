if get_option('plugin_uefi_capsule').disable_auto_if(host_machine.system() != 'linux').allowed()
  cargs = ['-DG_LOG_DOMAIN="FuPluginUefiRecovery"']

  plugin_quirks += files('uefi-recovery.quirk')
  plugin_builtins += static_library(
    'fu_plugin_uefi_recovery',
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
