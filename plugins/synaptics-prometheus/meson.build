if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsPrometheus"']

plugin_quirks += files('synaptics-prometheus.quirk')
plugin_builtin_synaprom = static_library('fu_plugin_synaprom',
  structgen.process(
    'fu-synaprom.struct',           # fuzzing
  ),
  sources: [
    'fu-synaprom-plugin.c',
    'fu-synaprom-common.c',
    'fu-synaprom-config.c',
    'fu-synaprom-device.c',
    'fu-synaprom-firmware.c',       # fuzzing
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_synaprom

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
      plugin_builtin_synaprom,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('synaptics-prometheus-self-test', e, env: env)  # added to installed-tests
endif
endif
