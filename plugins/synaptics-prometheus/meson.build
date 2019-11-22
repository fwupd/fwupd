cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsPrometheus"']

install_data(['synaptics-prometheus.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_synaptics_prometheus',
  fu_hash,
  sources : [
    'fu-plugin-synaptics-prometheus.c',
    'fu-synaprom-common.c',
    'fu-synaprom-config.c',
    'fu-synaprom-device.c',
    'fu-synaprom-firmware.c',
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

if get_option('tests')
  testdatadir = join_paths(meson.current_source_dir(), 'data')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  e = executable(
    'synaptics-prometheus-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-synaprom-common.c',
      'fu-synaprom-config.c',
      'fu-synaprom-device.c',
      'fu-synaprom-firmware.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    c_args : cargs
  )
  test('synaptics-prometheus-self-test', e)

  # for fuzzing
  executable(
    'synaptics-prometheus-dump',
    sources : [
      'fu-dump.c',
      'fu-synaprom-firmware.c',
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
endif
