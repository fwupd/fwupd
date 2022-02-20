if gusb.found()
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
    'fu-synaprom-firmware.c',       # fuzzing
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
  install_data(['tests/synaptics-prometheus.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
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
    c_args : cargs,
    install : true,
    install_dir : installed_test_bindir,
  )
  test('synaptics-prometheus-self-test', e, env : env)  # added to installed-tests
endif
endif
