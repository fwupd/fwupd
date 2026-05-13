plugins += {meson.current_source_dir().split('/')[-1]: true}
cargs = ['-DG_LOG_DOMAIN="FuPluginWacomUsb"']

plugin_quirks += files('wacom-usb.quirk')
plugin_builtin_wac = static_library('fu_plugin_wacom_usb',
  rustgen.process(
    'fu-wacom-usb.rs',              # fuzzing
  ),
  sources: [
    'fu-wacom-usb-common.c',
    'fu-wacom-usb-android-device.c',
    'fu-wacom-usb-device.c',
    'fu-wacom-usb-firmware.c',          # fuzzing
    'fu-wacom-usb-module.c',
    'fu-wacom-usb-module-bluetooth.c',
    'fu-wacom-usb-module-bluetooth-id6.c',
    'fu-wacom-usb-module-bluetooth-id9.c',
    'fu-wacom-usb-module-scaler.c',
    'fu-wacom-usb-module-sub-cpu.c',
    'fu-wacom-usb-module-touch.c',
    'fu-wacom-usb-module-touch-id7.c',
    'fu-wacom-usb-plugin.c',
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
    rustgen.process('fu-wacom-usb.rs'),
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
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('wacom-usb-self-test', e, env: env)  # added to installed-tests
endif
