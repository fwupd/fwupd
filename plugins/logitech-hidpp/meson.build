if gudev.found() and gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechHidPp"']

plugin_quirks += join_paths(meson.current_source_dir(), 'logitech-hidpp.quirk')

shared_module('fu_plugin_logitech_hidpp',
  fu_hash,
  sources: [
    'fu-plugin-logitech-hidpp.c',
    'fu-logitech-hidpp-bootloader.c',
    'fu-logitech-hidpp-bootloader-nordic.c',
    'fu-logitech-hidpp-bootloader-texas.c',
    'fu-logitech-hidpp-common.c',
    'fu-logitech-hidpp-hidpp.c',
    'fu-logitech-hidpp-device.c',
    'fu-logitech-hidpp-hidpp-msg.c',
    'fu-logitech-hidpp-runtime.c',
    'fu-logitech-hidpp-runtime-unifying.c',
    'fu-logitech-hidpp-runtime-bolt.c',
    'fu-logitech-hidpp-radio.c',
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
    'logitech-hidpp-self-test',
    fu_hash,
    sources: [
      'fu-logitech-hidpp-self-test.c',
      'fu-logitech-hidpp-common.c',
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
  )
  test('logitech-hidpp-self-test', e)
endif
endif
