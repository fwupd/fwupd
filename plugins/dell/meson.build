cargs = ['-DG_LOG_DOMAIN="FuPluginDell"']

install_data(['dell.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_dell',
  fu_hash,
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
  link_with : [
    libfwupdprivate,
  ],
  c_args : [
    cargs,
  ],
  dependencies : [
    plugin_deps,
    efivar,
    libsmbios_c,
    tpm2tss,
  ],
)

if get_option('tests')
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  cargs += '-DPLUGINBUILDDIR="' + meson.current_build_dir() + '"'
  e = executable(
    'dell-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-test.c',
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
      libsmbios_c,
      valgrind,
      tpm2tss,
    ],
    link_with : [
      libfwupdprivate,
    ],
    c_args : [
      cargs,
    ],
  )
  test('dell-self-test', e)
endif
