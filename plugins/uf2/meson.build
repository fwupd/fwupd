
if get_option('plugin_uf2').require(gudev.found(),
    error_message: 'gudev is needed for plugin_uf2').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginUf2"']

plugin_quirks += join_paths(meson.current_source_dir(), 'uf2.quirk')

shared_module('fu_plugin_uf2',
  fu_hash,
  sources: [
    'fu-plugin-uf2.c',
    'fu-uf2-device.c',
    'fu-uf2-firmware.c',      # fuzzing
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
  link_with: [
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
    sources: [
      'fu-self-test.c',
      'fu-uf2-firmware.c',
    ],
    include_directories: [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies: [
      plugin_deps,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
    ],
    install: true,
    install_dir: installed_test_bindir,
  )
  test('uf2-self-test', e, env: env)
endif
endif
