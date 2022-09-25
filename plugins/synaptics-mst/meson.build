if get_option('plugin_synaptics_mst').require(gudev.found(),
    error_message: 'gudev is needed for plugin_synaptics_mst').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsMST"']

plugin_quirks += join_paths(meson.current_source_dir(), 'synaptics-mst.quirk')

shared_module('fu_plugin_synaptics_mst',
  fu_hash,
  sources: [
    'fu-plugin-synaptics-mst.c',
    'fu-synaptics-mst-common.c',
    'fu-synaptics-mst-connection.c',
    'fu-synaptics-mst-device.c',
    'fu-synaptics-mst-firmware.c',      # fuzzing
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  c_args: [
    cargs,
  ],
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  dependencies: [
    plugin_deps,
  ],
)

if get_option('tests')
  install_data(['tests/synaptics-mst.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  env.set('FWUPD_DATADIR_QUIRKS', meson.current_source_dir())
  e = executable(
    'synaptics-mst-self-test',
    fu_hash,
    sources: [
      'fu-self-test.c',
      'fu-synaptics-mst-common.c',
      'fu-synaptics-mst-connection.c',
      'fu-synaptics-mst-device.c',
      'fu-synaptics-mst-firmware.c',
    ],
    include_directories: [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies: [
      plugin_deps,
      valgrind,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
    ],
    c_args: [
      cargs,
    ],
    install: true,
    install_dir: installed_test_bindir,
  )
  test('synaptics-mst-self-test', e, env: env)
endif
endif
