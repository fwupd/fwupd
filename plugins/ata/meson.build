if gudev.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginAta"']

plugin_quirks += join_paths(meson.current_source_dir(), 'ata.quirk')

shared_module('fu_plugin_ata',
  fu_hash,
  sources: [
    'fu-plugin-ata.c',
    'fu-ata-device.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  dependencies: [
    plugin_deps,
  ],
)

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_DATADIR_QUIRKS', meson.current_source_dir())
  e = executable(
    'ata-self-test',
    fu_hash,
    sources: [
      'fu-self-test.c',
      'fu-ata-device.c',
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
  test('ata-self-test', e, env: env)  # added to installed-tests
endif
endif
