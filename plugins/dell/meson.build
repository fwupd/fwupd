libsmbios_c = dependency('libsmbios_c', version: '>= 2.4.0', required: get_option('plugin_dell'))

if get_option('plugin_dell').require(get_option('plugin_uefi_capsule').allowed(),
    error_message: 'plugin_uefi_capsule is needed for plugin_dell').allowed()

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
