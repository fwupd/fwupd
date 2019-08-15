cargs = ['-DG_LOG_DOMAIN="FuPluginAta"']

install_data([
  'ata.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

install_data(['ata.png'],
  install_dir : join_paths(datadir, 'icons', 'fwupd', 'plugins')
)

shared_module('fu_plugin_ata',
  fu_hash,
  sources : [
    'fu-plugin-ata.c',
    'fu-ata-device.c',
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
  link_with : [
    libfwupdprivate,
  ],
  dependencies : [
    plugin_deps,
  ],
)

if get_option('tests')
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  e = executable(
    'ata-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-ata-device.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
      include_directories('../../libfwupd'),
      include_directories('../../src'),
    ],
    dependencies : [
      plugin_deps,
    ],
    link_with : [
      libfwupdprivate,
    ],
    c_args : cargs
  )
  test('ata-self-test', e)
endif
