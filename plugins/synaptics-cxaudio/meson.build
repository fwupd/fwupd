cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsCxaudio"']

install_data(['synaptics-cxaudio.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_synaptics_cxaudio',
  fu_hash,
  sources : [
    'fu-plugin-synaptics-cxaudio.c',
    'fu-synaptics-cxaudio-device.c',
    'fu-synaptics-cxaudio-firmware.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)
