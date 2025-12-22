cargs = ['-DG_LOG_DOMAIN="FuPluginAlgoltekUsbcr"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('algoltek-usbcr.quirk')
plugin_builtin_algoltek_usbcr = static_library('fu_plugin_algoltek_usbcr',
  rustgen.process('fu-algoltek-usbcr.rs'),
  sources: [
    'fu-algoltek-usbcr-device.c',
    'fu-algoltek-usbcr-firmware.c',
    'fu-algoltek-usbcr-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_algoltek_usbcr

enumeration_data += files('tests/algoltek-au84612-setup.json')
device_tests += files('tests/algoltek-au84612.json')

if get_option('tests')
  install_data(['tests/algoltek-usbcr.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'algoltek-usbcr-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_algoltek_usbcr,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('algoltek-usbcr-self-test', e, env: env)
endif
