host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsMST"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('synaptics-mst.quirk')

plugin_builtin_synaptics_mst = static_library('fu_plugin_synaptics_mst',
  rustgen.process(
    'fu-synaptics-mst.rs', # fuzzing
  ),
  sources: [
    'fu-synaptics-mst-plugin.c',
    'fu-synaptics-mst-common.c',
    'fu-synaptics-mst-device.c',
    'fu-synaptics-mst-firmware.c',      # fuzzing
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_synaptics_mst

device_tests += files(
  'tests/synaptics-panamera.json',
)
enumeration_data += files(
  'tests/synaptics-panamera-setup.json',
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
    rustgen.process('fu-synaptics-mst.rs'),
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      valgrind,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
      plugin_builtin_synaptics_mst,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('synaptics-mst-self-test', e, env: env)
endif
