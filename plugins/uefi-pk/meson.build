if get_option('hsi') and \
   get_option('plugin_uefi_pk').require(gnutls.found(),
   error_message: 'gnutls is needed for plugin_uefi_pk').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginUefiPk"']

shared_module('fu_plugin_uefi_pk',
  fu_hash,
  sources: [
    'fu-plugin-uefi-pk.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
)
endif
