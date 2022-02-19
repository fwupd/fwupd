if get_option('plugin_bcm57xx').require(gudev.found(),
    error_message: 'gudev is needed for plugin_bcm57xx').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginBcm57xx"']

install_data(['bcm57xx.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)
shared_module('fu_plugin_bcm57xx',
  fu_hash,
  sources : [
    'fu-plugin-bcm57xx.c',
    'fu-bcm57xx-common.c',          # fuzzing
    'fu-bcm57xx-device.c',
    'fu-bcm57xx-dict-image.c',      # fuzzing
    'fu-bcm57xx-firmware.c',        # fuzzing
    'fu-bcm57xx-recovery-device.c',
    'fu-bcm57xx-stage1-image.c',    # fuzzing
    'fu-bcm57xx-stage2-image.c',    # fuzzing
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
    valgrind,
  ],
)

if get_option('tests')
  install_data(['tests/bcm57xx.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'bcm57xx-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-bcm57xx-common.c',
      'fu-bcm57xx-dict-image.c',
      'fu-bcm57xx-firmware.c',
      'fu-bcm57xx-stage1-image.c',
      'fu-bcm57xx-stage2-image.c',
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
    install : true,
    install_dir : installed_test_bindir,
  )
  test('bcm57xx-self-test', e, env : env)
endif
endif
