if get_option('plugin_synaptics_rmi').require(gnutls.found(),
    error_message: 'gnutls is needed for plugin_synaptics_rmi').allowed() and \
   get_option('plugin_synaptics_rmi').require(gudev.found(),
    error_message: 'gudev is needed for plugin_synaptics_rmi').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsRmi"']

plugin_quirks += files('synaptics-rmi.quirk')
plugin_builtin_synaptics_rmi = static_library('fu_plugin_synaptics_rmi',
  structgen.process('fu-synaptics-rmi.struct'),
  sources: [
    'fu-synaptics-rmi-plugin.c',
    'fu-synaptics-rmi-common.c',      # fuzzing
    'fu-synaptics-rmi-device.c',
    'fu-synaptics-rmi-hid-device.c',
    'fu-synaptics-rmi-ps2-device.c',
    'fu-synaptics-rmi-v5-device.c',
    'fu-synaptics-rmi-v6-device.c',
    'fu-synaptics-rmi-v7-device.c',
    'fu-synaptics-rmi-firmware.c',    # fuzzing
  ],
  include_directories: plugin_incdirs,
  c_args: cargs,
  dependencies: plugin_deps,
  link_with: plugin_libs,
)
plugin_builtins += plugin_builtin_synaptics_rmi

if get_option('tests')
  install_data(['tests/synaptics-rmi-0x.builder.xml','tests/synaptics-rmi-10.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'synaptics-rmi-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_synaptics_rmi,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('synaptics-rmi-self-test', e, env: env)
endif
endif
