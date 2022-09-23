if hsi and \
   get_option('plugin_uefi_pk').require(gnutls.found(),
   error_message: 'gnutls is needed for plugin_uefi_pk').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginUefiPk"']

plugin_builtins += static_library('fu_plugin_uefi_pk',
  sources: [
    'fu-uefi-pk-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
