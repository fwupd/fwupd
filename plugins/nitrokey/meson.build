cargs = ['-DG_LOG_DOMAIN="FuPluginNitrokey"']

shared_module('fu_plugin_nitrokey',
  sources : [
    'fu-nitrokey-device.c',
    'fu-nitrokey-common.c',
    'fu-plugin-nitrokey.c',
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
    'nitrokey-self-test',
    sources : [
      'fu-nitrokey-common.c',
      'fu-self-test.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../src'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      plugin_deps,
      valgrind,
    ],
    link_with : [
      libfwupdprivate,
    ],
  )
  test('nitrokey-self-test', e)
endif
