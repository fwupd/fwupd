cargs = ['-DG_LOG_DOMAIN="FuPluginUnifying"']

install_data([
  'unifying.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)


shared_module('fu_plugin_unifying',
  sources : [
    'fu-plugin-unifying.c',
    'fu-unifying-bootloader.c',
    'fu-unifying-bootloader-nordic.c',
    'fu-unifying-bootloader-texas.c',
    'fu-unifying-common.c',
    'fu-unifying-hidpp.c',
    'fu-unifying-hidpp-msg.c',
    'fu-unifying-peripheral.c',
    'fu-unifying-runtime.c',
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
  ],
)

if get_option('tests')
  e = executable(
    'unifying-self-test',
    sources : [
      'fu-unifying-self-test.c',
      'fu-unifying-common.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      plugin_deps,
    ],
    link_with : [
      fwupd,
    ],
    c_args : cargs,
  )
  test('unifying-self-test', e)
endif
