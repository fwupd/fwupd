if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginWacomUsb"']

plugin_quirks += files('wacom-usb.quirk')
plugin_builtin_wac = static_library('fu_plugin_wac',
  sources: [
    'fu-wac-common.c',
    'fu-wac-android-device.c',
    'fu-wac-device.c',
    'fu-wac-firmware.c',          # fuzzing
    'fu-wac-module.c',
    'fu-wac-module-bluetooth.c',
    'fu-wac-module-bluetooth-id6.c',
    'fu-wac-module-scaler.c',
    'fu-wac-module-touch.c',
    'fu-wac-plugin.c',
  ],
  include_directories: plugin_incdirs,
  c_args: cargs,
  dependencies: plugin_deps,
  link_with: plugin_libs,
)
plugin_builtins += plugin_builtin_wac

if get_option('tests')
  install_data(['tests/wacom-usb.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'wacom-usb-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_wac,
    ],
    c_args: cargs,
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
  )
  test('wacom-usb-self-test', e, env: env)  # added to installed-tests
endif
endif
