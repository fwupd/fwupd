cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsRmi"']

install_data(['synaptics-rmi.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_synaptics_rmi',
  fu_hash,
  sources : [
    'fu-plugin-synaptics-rmi.c',
    'fu-synaptics-rmi-common.c',
    'fu-synaptics-rmi-device.c',
    'fu-synaptics-rmi-v5-device.c',
    'fu-synaptics-rmi-v6-device.c',
    'fu-synaptics-rmi-v7-device.c',
    'fu-synaptics-rmi-firmware.c',
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
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
)

if get_option('tests')
  # for fuzzing
  synaptics_rmi_dump = executable(
    'synaptics-rmi-dump',
    sources : [
      'fu-dump.c',
      'fu-synaptics-rmi-common.c',
      'fu-synaptics-rmi-firmware.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      gio,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    c_args : cargs
  )
  subdir('fuzzing')
endif
