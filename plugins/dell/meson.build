if get_option('plugin_uefi_capsule').disable_auto_if(host_machine.system() != 'linux').allowed()

cargs = ['-DG_LOG_DOMAIN="FuPluginDell"']

plugin_quirks += files('dell.quirk')

plugin_builtin_dell = static_library('fu_plugin_dell',
  sources: [
    'fu-dell-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: [
    cargs,
  ],
  dependencies: [
    plugin_deps,
  ],
)
plugin_builtins += plugin_builtin_dell

endif
