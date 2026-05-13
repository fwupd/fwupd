cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsPrometheus"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('synaptics-prometheus.quirk')
plugin_builtin_synaptics_prometheus = static_library('fu_plugin_synaptics_prometheus',
  rustgen.process(
    'fu-synaptics-prometheus.rs',           # fuzzing
  ),
  sources: [
    'fu-synaptics-prometheus-plugin.c',
    'fu-synaptics-prometheus-common.c',
    'fu-synaptics-prometheus-config.c',
    'fu-synaptics-prometheus-device.c',
    'fu-synaptics-prometheus-firmware.c',       # fuzzing
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_synaptics_prometheus

if get_option('tests')
  install_data(['tests/synaptics-prometheus.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'synaptics-prometheus-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_synaptics_prometheus,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('synaptics-prometheus-self-test', e, env: env)  # added to installed-tests
endif

device_tests += files('tests/synaptics-prometheus.json')
