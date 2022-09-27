if gudev.found() and (host_cpu == 'x86' or host_cpu == 'x86_64')
cargs = ['-DG_LOG_DOMAIN="FuPluginAmdPmc"']

plugin_quirks += files('amd-pmc.quirk')

shared_module('fu_plugin_amd_pmc',
  fu_hash,
  sources: [
    'fu-plugin-amd-pmc.c',
    'fu-amd-pmc-device.c',
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
