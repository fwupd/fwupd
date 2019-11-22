cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechHidPp"']

install_data([
  'logitech-hidpp.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)


shared_module('fu_plugin_logitech_hidpp',
  fu_hash,
  sources : [
    'fu-plugin-logitech-hidpp.c',
    'fu-logitech-hidpp-bootloader.c',
    'fu-logitech-hidpp-bootloader-nordic.c',
    'fu-logitech-hidpp-bootloader-texas.c',
    'fu-logitech-hidpp-common.c',
    'fu-logitech-hidpp-hidpp.c',
    'fu-logitech-hidpp-hidpp-msg.c',
    'fu-logitech-hidpp-peripheral.c',
    'fu-logitech-hidpp-runtime.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    libfwupdprivate,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)

if get_option('tests')
  e = executable(
    'logitech-hidpp-self-test',
    fu_hash,
    sources : [
      'fu-logitech-hidpp-self-test.c',
      'fu-logitech-hidpp-common.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      plugin_deps,
    ],
    link_with : [
      libfwupdprivate,
    ],
    c_args : cargs,
  )
  test('logitech-hidpp-self-test', e)
endif
