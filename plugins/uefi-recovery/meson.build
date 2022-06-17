if get_option('plugin_uefi_capsule').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginUefiRecovery"']

plugin_quirks += join_paths(meson.current_source_dir(), 'uefi-recovery.quirk')

shared_module('fu_plugin_uefi_recovery',
  fu_hash,
  sources: [
    'fu-plugin-uefi-recovery.c',
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
  c_args: [
    cargs,
  ],
  dependencies: [
    plugin_deps,
  ],
)
endif
