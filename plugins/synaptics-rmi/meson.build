cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsRmi"']

install_data(['synaptics-rmi.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_synaptics_rmi',
  fu_hash,
  sources : [
    'fu-plugin-synaptics-rmi.c',
    'fu-synaptics-rmi-common.c',      # fuzzing
    'fu-synaptics-rmi-device.c',
    'fu-synaptics-rmi-hid-device.c',
    'fu-synaptics-rmi-ps2-device.c',
    'fu-synaptics-rmi-v5-device.c',
    'fu-synaptics-rmi-v6-device.c',
    'fu-synaptics-rmi-v7-device.c',
    'fu-synaptics-rmi-firmware.c',    # fuzzing
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
    gnutls,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
)
