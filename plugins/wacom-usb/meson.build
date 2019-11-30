cargs = ['-DG_LOG_DOMAIN="FuPluginWacomUsb"']

install_data(['wacom-usb.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_wacom_usb',
  fu_hash,
  sources : [
    'fu-wac-common.c',
    'fu-wac-device.c',
    'fu-wac-firmware.c',
    'fu-wac-module.c',
    'fu-wac-module-bluetooth.c',
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
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
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
      libxmlb,
      gio,
      gusb,
      gudev,
      libm,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    c_args : cargs
  )
  test('wacom-usb-self-test', e)
endif
