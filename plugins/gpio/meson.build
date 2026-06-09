host_machine.system() == 'linux' or subdir_done()
cc.has_header_symbol('linux/gpio.h', 'GPIO_V2_LINE_FLAG_OUTPUT', required: false) or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginGpio"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('gpio.quirk')
plugin_builtin_gpio = static_library('fu_plugin_gpio',
  sources: [
    'fu-gpio-plugin.c',
    'fu-gpio-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_gpio

if get_option('tests')
  install_data(['tests/quirks.d/gpio.quirk'],
    install_dir: join_paths(installed_test_datadir, 'tests', 'quirks.d'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'gpio-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_gpio,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('gpio-self-test', e, env: env)
endif
