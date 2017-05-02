cargs = ['-DG_LOG_DOMAIN="FuPluginDell"']

shared_module('fu_plugin_dell',
  sources : [
    'fu-plugin-dell.c',
    'fu-dell-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : [
      cargs,
      '-DLOCALSTATEDIR="' + localstatedir + '"',
    ],
  dependencies : [
    plugin_deps,
    efivar,
    libsmbios_c,
    fwup
  ],
)

executable(
  'fu-dell-tool',
  sources : [
    'fu-dell-tool.c',
    'fu-dell-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  dependencies : [
    plugin_deps,
    efivar,
    libsmbios_c,
  ],
  link_with : [
    fwupd,
  ],
  c_args : cargs,
)

if get_option('enable-tests')
  cargs += '-DFU_OFFLINE_DESTDIR="/tmp/fwupd-self-test"'
  cargs += '-DPLUGINBUILDDIR="' + meson.current_build_dir() + '"'
  e = executable(
    'dell-self-test',
    sources : [
      'fu-self-test.c',
      'fu-dell-common.c',
      'fu-device.c',
      'fu-pending.c',
      'fu-plugin.c',
      'fu-plugin-dell.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../src'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      plugin_deps,
      efivar,
      fwup,
      sqlite,
      gudev,
      libsmbios_c,
      valgrind,
    ],
    link_with : [
      fwupd,
    ],
    c_args : [
      cargs,
      '-DLOCALSTATEDIR="/tmp/fwupd-self-test/var"',
    ],
  )
  test('dell-self-test', e)
endif
