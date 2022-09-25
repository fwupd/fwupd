if get_option('plugin_ep963x').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginEp963x"']

plugin_quirks += join_paths(meson.current_source_dir(), 'ep963x.quirk')

shared_module('fu_plugin_ep963x',
  fu_hash,
  sources: [
    'fu-ep963x-common.c',
    'fu-ep963x-device.c',
    'fu-ep963x-firmware.c',
    'fu-plugin-ep963x.c',
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
