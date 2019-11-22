cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsMST"']

install_data(['synapticsmst.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_synapticsmst',
  fu_hash,
  sources : [
    'fu-plugin-synapticsmst.c',
    'fu-synapticsmst-common.c',
    'fu-synapticsmst-connection.c',
    'fu-synapticsmst-device.c',
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
  cargs += '-DPLUGINBUILDDIR="' + meson.current_build_dir() + '"'
  cargs += '-DSOURCEDIR="' + meson.current_source_dir() + '"'
  e = executable(
    'synapticsmst-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-synapticsmst-common.c',
      'fu-synapticsmst-connection.c',
      'fu-synapticsmst-device.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
      sqlite,
      valgrind,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    c_args : [
      cargs,
    ],
  )
  test('synapticsmst-self-test', e,
    env: ['FWUPD_LOCALSTATEDIR=/tmp/fwupd-self-test/var'])
endif
