if get_option('plugin_uf2')

# TODO when upgrading to meson 0.59 as baseline use require()
if not gudev.found()
  error('gudev is required for plugin_uf2')
endif
if host_machine.system() != 'linux'
  error('plugin_uf2 is only supported on Linux')
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginUf2"']

install_data(['uf2.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_uf2',
  fu_hash,
  sources : [
    'fu-plugin-uf2.c',
    'fu-uf2-device.c',
    'fu-uf2-firmware.c',      # fuzzing
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
  install_data(['tests/uf2.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'uf2-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-uf2-firmware.c',
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
  test('uf2-self-test', e, env : env)
endif
endif
