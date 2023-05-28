if get_option(
  'plugin_bcm57xx'
).require(gudev.found(), error_message: 'gudev is needed for plugin_bcm57xx').allowed()
  cargs = ['-DG_LOG_DOMAIN="FuPluginBcm57xx"']

  plugin_quirks += files('bcm57xx.quirk')
  plugin_builtin_bcm57xx = static_library(
    'fu_plugin_bcm57xx',
    sources: [
      'fu-bcm57xx-plugin.c',
      'fu-bcm57xx-common.c', # fuzzing
      'fu-bcm57xx-device.c',
      'fu-bcm57xx-dict-image.c', # fuzzing
      'fu-bcm57xx-firmware.c', # fuzzing
      'fu-bcm57xx-recovery-device.c',
      'fu-bcm57xx-stage1-image.c', # fuzzing
      'fu-bcm57xx-stage2-image.c', # fuzzing
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: [
      plugin_deps,
      valgrind,
    ],
  )
  plugin_builtins += plugin_builtin_bcm57xx

  if get_option('tests')
    install_data(
      ['tests/bcm57xx.builder.xml'],
      install_dir: join_paths(installed_test_datadir, 'tests'),
    )
    env = environment()
    env.set('G_TEST_SRCDIR', meson.current_source_dir())
    env.set('G_TEST_BUILDDIR', meson.current_build_dir())
    e = executable(
      'bcm57xx-self-test',
      sources: [
        'fu-self-test.c',
      ],
      include_directories: plugin_incdirs,
      dependencies: plugin_deps,
      link_with: [
        plugin_libs,
        plugin_builtin_bcm57xx,
      ],
      install: true,
      install_rpath: libdir_pkg,
      install_dir: installed_test_bindir,
      c_args: [
        '-DSRCDIR="' + meson.current_source_dir() + '"',
      ],
    )
    test('bcm57xx-self-test', e, env: env)
  endif
endif
