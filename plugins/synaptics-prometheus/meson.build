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
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    libfwupdprivate,
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
      include_directories('../..'),
      include_directories('../../src'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      plugin_deps,
    ],
    link_with : [
      libfwupdprivate,
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
