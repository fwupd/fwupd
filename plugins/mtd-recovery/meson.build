if get_option('plugin_mtd').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginMtdRecovery"']
plugin_builtins += static_library('fu_plugin_mtd_recovery',
  sources: [
    'fu-mtd-recovery-device.c',
    'fu-mtd-recovery-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

