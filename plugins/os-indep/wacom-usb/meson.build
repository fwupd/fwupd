plugins += {meson.current_source_dir().split('/')[-1]: true}
cargs = ['-DG_LOG_DOMAIN="FuPluginWacomUsb"']

plugin_quirks += files('wacom-usb.quirk')
plugin_builtin_wac = static_library('fu_plugin_wac',
  rustgen.process(
    'fu-wac.rs',              # fuzzing
  ),
  sources: [
    'fu-wac-common.c',
    'fu-wac-android-device.c',
    'fu-wac-device.c',
    'fu-wac-firmware.c',          # fuzzing
    'fu-wac-module.c',
    'fu-wac-module-bluetooth.c',
    'fu-wac-module-bluetooth-id6.c',
    'fu-wac-module-bluetooth-id9.c',
    'fu-wac-module-scaler.c',
    'fu-wac-module-sub-cpu.c',
    'fu-wac-module-touch.c',
    'fu-wac-module-touch-id7.c',
    'fu-wac-plugin.c',
  ],
  include_directories: plugin_incdirs,
  c_args: cargs,
  dependencies: plugin_deps,
  link_with: plugin_libs,
)
plugin_builtins += plugin_builtin_wac

enumeration_data += files(
  'tests/wacom-movink-13-setup.json',
)

device_tests += files(
  'tests/wacom-intuos-bt-m.json',
  'tests/wacom-intuos-bt-s.json',
  'tests/wacom-movink-13.json',
)

if get_option('tests')
  install_data(['tests/wacom-usb.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'wacom-usb-self-test',
    rustgen.process('fu-wac.rs'),
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_wac,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('wacom-usb-self-test', e, env: env)  # added to installed-tests
endif
