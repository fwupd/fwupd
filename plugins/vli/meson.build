cargs = ['-DG_LOG_DOMAIN="FuPluginVliUsbhub"']

install_data([
  'vli-pd.quirk',
  'vli-usbhub.quirk',
  'vli-usbhub-lenovo.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_vli',
  fu_hash,
  sources : [
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
    'fu-vli-usbhub-i2c-device.c',
    'fu-vli-usbhub-pd-device.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)

if get_option('tests')
  testdatadir = join_paths(meson.current_source_dir(), 'data')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  e = executable(
    'vli-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-vli-common.c',
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
    c_args : cargs
  )
  test('vli-self-test', e)
endif
