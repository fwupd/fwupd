if get_option('plugin_uefi_capsule').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginUefiEsrt"']

plugin_builtins += static_library('fu_plugin_uefi_esrt',
  sources: [
    'fu-uefi-esrt-plugin.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
  ],
  link_with: plugin_libs,
  dependencies: [
    plugin_deps,
  ],
)
endif

