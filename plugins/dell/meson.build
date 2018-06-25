cargs = ['-DG_LOG_DOMAIN="FuPluginDell"']

install_data(['dell.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_dell',
  sources : [
    'fu-plugin-dell.c',
    'fu-dell-smi.c',
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
    ],
  dependencies : [
    plugin_deps,
    efivar,
    libsmbios_c,
  ],
)

if get_option('tests')
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  cargs += '-DFU_OFFLINE_DESTDIR="/tmp/fwupd-self-test"'
  cargs += '-DPLUGINBUILDDIR="' + meson.current_build_dir() + '"'
  e = executable(
    'dell-self-test',
    sources : [
      'fu-self-test.c',
      'fu-dell-smi.c',
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
      sqlite,
      gudev,
      libsmbios_c,
      valgrind,
    ],
    link_with : [
      fwupd,
      libfwupdprivate,
    ],
    c_args : [
      cargs,
    ],
  )
  test('dell-self-test', e)
endif
