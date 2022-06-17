if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginVliUsbhub"']

plugin_quirks += join_paths(meson.current_source_dir(), 'vli-bizlink.quirk')
plugin_quirks += join_paths(meson.current_source_dir(), 'vli-dell.quirk')
plugin_quirks += join_paths(meson.current_source_dir(), 'vli-goodway.quirk')
plugin_quirks += join_paths(meson.current_source_dir(), 'vli-hyper.quirk')
plugin_quirks += join_paths(meson.current_source_dir(), 'vli-lenovo.quirk')
plugin_quirks += join_paths(meson.current_source_dir(), 'vli-samsung.quirk')

shared_module('fu_plugin_vli',
  fu_hash,
  sources: [
    'fu-plugin-vli.c',
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
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
)

if get_option('tests')
  e = executable(
    'vli-self-test',
    fu_hash,
    sources: [
      'fu-self-test.c',
      'fu-vli-common.c',
    ],
    include_directories: [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies: [
      plugin_deps,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
    ],
    c_args: cargs,
    install: true,
    install_dir: installed_test_bindir,
  )
  test('vli-self-test', e)  # added to installed-tests
endif
endif
