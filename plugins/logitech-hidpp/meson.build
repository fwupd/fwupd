if gudev.found() and gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechHidPp"']

plugin_quirks += files('logitech-hidpp.quirk')
plugin_builtin_logitech_hidpp = static_library('fu_plugin_logitech_hidpp',
  sources: [
    'fu-logitech-hidpp-plugin.c',
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
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_logitech_hidpp

if get_option('tests')
  e = executable(
    'logitech-hidpp-self-test',
    sources: [
      'fu-logitech-hidpp-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_logitech_hidpp,
    ],
    c_args: cargs,
  )
  test('logitech-hidpp-self-test', e)
endif
endif
