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
    'fu-synaptics-rmi-firmware.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
  link_with : [
    libfwupdprivate,
  ],
)

if get_option('tests')
  # for fuzzing
  executable(
    'synaptics-rmi-dump',
    sources : [
      'fu-dump.c',
      'fu-synaptics-rmi-common.c',
      'fu-synaptics-rmi-firmware.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../src'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      gio,
    ],
    link_with : [
      libfwupdprivate,
    ],
    c_args : cargs
  )
endif
