cargs = ['-DG_LOG_DOMAIN="FuPluginNvme"']

shared_module('fu_plugin_nvme',
  sources : [
    'fu-plugin-nvme.c',
    'fu-nvme-device.c',
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
    efivar,
  ],
)

if get_option('tests')
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  e = executable(
    'nvme-self-test',
    sources : [
      'fu-self-test.c',
      'fu-nvme-device.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
      include_directories('../../libfwupd'),
      include_directories('../../src'),
    ],
    dependencies : [
      plugin_deps,
      efivar,
    ],
    link_with : [
      libfwupdprivate,
    ],
    c_args : cargs
  )
  test('nvme-self-test', e)
endif
