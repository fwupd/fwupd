cargs = ['-DG_LOG_DOMAIN="FuPluginUnifying"']

shared_module('fu_plugin_unifying',
  sources : [
    'fu-plugin-unifying.c',
    'lu-common.c',
    'lu-context.c',
    'lu-device-bootloader.c',
    'lu-device-bootloader-nordic.c',
    'lu-device-bootloader-texas.c',
    'lu-device.c',
    'lu-device-peripheral.c',
    'lu-device-runtime.c',
    'lu-hidpp-msg.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
    gudev,
  ],
)

executable(
  'fu-unifying-util',
  sources : [
    'lu-tool.c',
    'lu-common.c',
    'lu-context.c',
    'lu-device-bootloader.c',
    'lu-device-bootloader-nordic.c',
    'lu-device-bootloader-texas.c',
    'lu-device.c',
    'lu-device-peripheral.c',
    'lu-device-runtime.c',
    'lu-hidpp-msg.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  dependencies : [
    plugin_deps,
    gudev,
  ],
  link_with : [
    fwupd,
    libfwupdprivate,
  ],
  c_args : cargs,
)

if get_option('enable-tests')
  e = executable(
    'unifying-self-test',
    sources : [
      'lu-self-test.c',
      'lu-common.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      plugin_deps,
      gudev,
    ],
    link_with : [
      fwupd,
    ],
    c_args : cargs,
  )
  test('unifying-self-test', e)
endif
