if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsCxaudio"']

plugin_quirks += join_paths(meson.current_source_dir(), 'synaptics-cxaudio.quirk')

shared_module('fu_plugin_synaptics_cxaudio',
  fu_hash,
  sources: [
    'fu-plugin-synaptics-cxaudio.c',
    'fu-synaptics-cxaudio-device.c',
    'fu-synaptics-cxaudio-firmware.c',
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
