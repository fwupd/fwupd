if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsCape"']

plugin_quirks += join_paths(meson.current_source_dir(), 'synaptics-cape.quirk')

shared_module('fu_plugin_synaptics_cape',
  fu_hash,
  sources: [
    'fu-plugin-synaptics-cape.c',
    'fu-synaptics-cape-device.c',
    'fu-synaptics-cape-firmware.c', # fuzzing
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
