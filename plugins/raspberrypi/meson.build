subdir('rpiupdate')

cargs = ['-DG_LOG_DOMAIN="FuPluginRpi"']

shared_module('fu_plugin_raspberrypi',
  sources : [
    'fu-plugin-raspberrypi.c',
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
  ],
)

if get_option('enable-tests')
  testdatadir_src = meson.current_source_dir()
  testdatadir_dst = meson.current_build_dir()
  e = executable(
    'raspberrypi-self-test',
    raspberrypi_firmware_bin,
    sources : [
      'fu-self-test.c',
      'fu-plugin-raspberrypi.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../src'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      plugin_deps,
      sqlite,
      valgrind,
    ],
    link_with : [
      fwupd,
      libfwupdprivate,
    ],
    c_args : [
      cargs,
      '-DTESTDATADIR="' + testdatadir_src + ':' + testdatadir_dst + '"',
      '-DPLUGINBUILDDIR="' + meson.current_build_dir() + '"',
      '-DLOCALSTATEDIR="/tmp/fwupd-self-test/var"',
      '-DFU_OFFLINE_DESTDIR="/tmp/fwupd-self-test"',
    ],
  )
  test('raspberrypi-self-test', e)
endif
