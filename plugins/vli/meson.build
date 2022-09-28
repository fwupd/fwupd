if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginVliUsbhub"']

plugin_quirks += files([
  'vli-bizlink.quirk',
  'vli-dell.quirk',
  'vli-goodway.quirk',
  'vli-hyper.quirk',
  'vli-lenovo.quirk',
  'vli-samsung.quirk',
  ])
plugin_builtin_vli = static_library('fu_plugin_vli',
  sources: [
    'fu-vli-plugin.c',
    'fu-vli-common.c',
    'fu-vli-device.c',
    'fu-vli-pd-common.c',
    'fu-vli-pd-device.c',
    'fu-vli-pd-firmware.c',
    'fu-vli-pd-parade-device.c',
    'fu-vli-usbhub-common.c',
    'fu-vli-usbhub-device.c',
    'fu-vli-usbhub-firmware.c',
    'fu-vli-usbhub-i2c-common.c',
    'fu-vli-usbhub-msp430-device.c',
    'fu-vli-usbhub-pd-device.c',
    'fu-vli-usbhub-rtd21xx-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_vli

if get_option('tests')
  e = executable(
    'vli-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_vli,
    ],
    c_args: cargs,
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
  )
  test('vli-self-test', e)  # added to installed-tests
endif
endif
