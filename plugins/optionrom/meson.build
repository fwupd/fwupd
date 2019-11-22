cargs = ['-DG_LOG_DOMAIN="FuPluginOptionrom"']

install_data(['optionrom.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_optionrom',
  fu_hash,
  sources : [
    'fu-plugin-optionrom.c',
    'fu-optionrom-device.c',
    'fu-rom.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)

executable(
  'fu-rom-tool',
  fu_hash,
  sources : [
    'fu-rom-tool.c',
    'fu-rom.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  dependencies : [
    plugin_deps,
    libjsonglib,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
)

if get_option('tests')
  cargs += '-DPLUGINBUILDDIR="' + meson.current_build_dir() + '"'
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  e = executable(
    'optionrom-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-rom.c',
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
  test('optionrom-self-test', e)
endif
