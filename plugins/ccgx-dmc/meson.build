if gusb.found()
  cargs = ['-DG_LOG_DOMAIN="FuPluginCcgxDmc"']

  plugin_quirks += files('ccgx-dmc.quirk')
  plugin_builtin_ccgx_dmc = static_library(
    'fu_plugin_ccgx_dmc',
    rustgen.process(
      'fu-ccgx-dmc.rs' # fuzzing
    ),
    sources: [
      'fu-ccgx-dmc-plugin.c',
      'fu-ccgx-dmc-device.c',
      'fu-ccgx-dmc-devx-device.c',
      'fu-ccgx-dmc-firmware.c', # fuzzing
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: [
      plugin_deps,
      gudev,
    ],
  )
  plugin_builtins += plugin_builtin_ccgx_dmc

  if get_option('tests')
    install_data(
      ['tests/ccgx-dmc.builder.xml'],
      install_dir: join_paths(installed_test_datadir, 'tests'),
    )
    env = environment()
    env.set('G_TEST_SRCDIR', meson.current_source_dir())
    env.set('G_TEST_BUILDDIR', meson.current_build_dir())
    e = executable(
      'ccgx-dmc-self-test',
      rustgen.process('fu-ccgx-dmc.rs'),
      sources: [
        'fu-self-test.c',
      ],
      include_directories: plugin_incdirs,
      dependencies: plugin_deps,
      link_with: [
        plugin_libs,
        plugin_builtin_ccgx_dmc,
      ],
      install: true,
      install_rpath: libdir_pkg,
      install_dir: installed_test_bindir,
      c_args: [
        '-DSRCDIR="' + meson.current_source_dir() + '"',
      ],
    )
    test('ccgx-dmc-self-test', e, env: env)
  endif
endif
