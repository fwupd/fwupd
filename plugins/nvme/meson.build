cargs = ['-DG_LOG_DOMAIN="FuPluginNvme"']

install_data([
  'nvme.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_nvme',
  fu_hash,
  sources : [
    'fu-plugin-nvme.c',
    'fu-nvme-common.c',
    'fu-nvme-device.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  dependencies : [
    plugin_deps,
  ],
)

if get_option('tests')
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  e = executable(
    'nvme-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-nvme-common.c',
      'fu-nvme-device.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    c_args : cargs
  )
  test('nvme-self-test', e)
endif
