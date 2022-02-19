if get_option('plugin_synaptics_rmi').require(gnutls.found(),
    error_message: 'gnutls is needed for plugin_synaptics_rmi').allowed() and \
   get_option('plugin_synaptics_rmi').require(gudev.found(),
    error_message: 'gudev is needed for plugin_synaptics_rmi').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsRmi"']

install_data(['synaptics-rmi.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_synaptics_rmi',
  fu_hash,
  sources : [
    'fu-plugin-synaptics-rmi.c',
    'fu-synaptics-rmi-common.c',      # fuzzing
    'fu-synaptics-rmi-device.c',
    'fu-synaptics-rmi-hid-device.c',
    'fu-synaptics-rmi-ps2-device.c',
    'fu-synaptics-rmi-v5-device.c',
    'fu-synaptics-rmi-v6-device.c',
    'fu-synaptics-rmi-v7-device.c',
    'fu-synaptics-rmi-firmware.c',    # fuzzing
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
)

if get_option('tests')
  install_data(['tests/synaptics-rmi-0x.builder.xml','tests/synaptics-rmi-10.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'synaptics-rmi-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-synaptics-rmi-common.c',
      'fu-synaptics-rmi-firmware.c',
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
    install : true,
    install_dir : installed_test_bindir,
  )
  test('synaptics-rmi-self-test', e, env : env)
endif
endif
