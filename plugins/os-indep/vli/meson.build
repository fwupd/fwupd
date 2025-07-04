plugins += {meson.current_source_dir().split('/')[-1]: true}
cargs = ['-DG_LOG_DOMAIN="FuPluginVliUsbhub"']

plugin_quirks += files([
  'vli-bizlink.quirk',
  'vli-cablematters.quirk',
  'vli-dell.quirk',
  'vli-fujitsu.quirk',
  'vli-goodway.quirk',
  'vli-hyper.quirk',
  'vli-lenovo.quirk',
  'vli-luxshare.quirk',
  'vli-samsung.quirk',
  ])
plugin_builtin_vli = static_library('fu_plugin_vli',
  rustgen.process('fu-vli.rs'),
  sources: [
    'fu-vli-plugin.c',
    'fu-vli-common.c',
    'fu-vli-device.c',
    'fu-vli-pd-common.c',
    'fu-vli-pd-device.c',
    'fu-vli-pd-firmware.c',
    'fu-vli-pd-parade-device.c',
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

device_tests += files(
  'tests/bizlink-no-sku-vli.json',
  'tests/hyper-no-sku-vli.json',
  'tests/lenovo-03x7168.json',
  'tests/lenovo-03x7605.json',
  'tests/lenovo-03x7608-vli.json',
  'tests/lenovo-40au0065-vli.json',
  'tests/lenovo-GX90T33021-vli.json',
)

if get_option('tests')
  e = executable(
    'vli-self-test',
    rustgen.process('fu-vli.rs'),
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_vli,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    c_args: cargs,
  )
  test('vli-self-test', e, env: env)  # added to installed-tests
endif
