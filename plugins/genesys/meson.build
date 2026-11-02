cargs = ['-DG_LOG_DOMAIN="FuPluginGenesys"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files(
  'genesys-usbhub-fwbank.quirk',
  'genesys.quirk',
)
plugin_builtin_genesys = static_library(
  'fu_plugin_genesys',
  rustgen.process(
    'fu-genesys-usbhub.rs' # fuzzing
  ),
  sources: [
    'fu-genesys-scaler-firmware.c', # fuzzing
    'fu-genesys-usbhub-firmware.c', # fuzzing
    'fu-genesys-usbhub-dev-firmware.c', # fuzzing
    'fu-genesys-usbhub-pd-firmware.c', # fuzzing
    'fu-genesys-usbhub-codesign-firmware.c', # fuzzing
    'fu-genesys-scaler-device.c',
    'fu-genesys-usbhub-device.c',
    'fu-genesys-hubhid-device.c',
    'fu-genesys-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_genesys

device_tests += files(
  'tests/genesys-evb-gl352510.json',
  'tests/genesys-evb-gl352530.json',
  #  'tests/genesys-evb-failure.json', # cannot emulate two devices(usb hub + child hid).
  'tests/genesys-servo-dock.json',
  'tests/genesys-zdn-chromebook.json',
)

if get_option('tests')
  e = executable(
    'genesys-self-test',
    sources: ['fu-self-test.c'],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [plugin_libs, plugin_builtin_genesys],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: ['-DSRCDIR="' + meson.current_source_dir() + '"'],
  )
  test(
    'genesys-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
