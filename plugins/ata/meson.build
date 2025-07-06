if host_machine.system() != 'linux'
  subdir_done()
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginAta"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('ata.quirk')
plugin_builtin_ata = static_library('fu_plugin_ata',
  sources: [
    'fu-ata-plugin.c',
    'fu-ata-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_ata

enumeration_data += files('tests/samsung-ssd-870-evo-setup.json')
device_tests += files('tests/samsung-ssd-870-evo.json')

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_DATADIR_QUIRKS', meson.current_source_dir())
  e = executable(
    'ata-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_ata,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('ata-self-test', e, env: env)  # added to installed-tests
endif
