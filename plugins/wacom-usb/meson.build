if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginWacomUsb"']

install_data(['wacom-usb.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_wacom_usb',
  fu_hash,
  sources : [
    'fu-wac-common.c',
    'fu-wac-android-device.c',
    'fu-wac-device.c',
    'fu-wac-firmware.c',          # fuzzing
    'fu-wac-module.c',
    'fu-wac-module-bluetooth.c',
    'fu-wac-module-bluetooth-id6.c',
    'fu-wac-module-touch.c',
    'fu-plugin-wacom-usb.c',
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
  install_data(['tests/wacom-usb.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'wacom-usb-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-wac-common.c',
      'fu-wac-firmware.c',
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
    c_args : cargs,
    install : true,
    install_dir : installed_test_bindir,
  )
  test('wacom-usb-self-test', e, env : env)  # added to installed-tests
endif
endif
