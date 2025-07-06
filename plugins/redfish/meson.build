if host_machine.system() != 'linux'
  subdir_done()
endif

cargs = ['-DG_LOG_DOMAIN="FuPluginRedfish"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('redfish.quirk')

ipmi_src = []
if cc.has_header('linux/ipmi.h')
  ipmi_src += 'fu-ipmi-device.c'
endif

plugin_builtin_redfish = static_library('fu_plugin_redfish',
  rustgen.process(
    'fu-redfish.rs',       # fuzzing
  ),
  sources: [
    'fu-redfish-plugin.c',
    'fu-redfish-backend.c',
    'fu-redfish-common.c',     # fuzzing
    'fu-redfish-device.c',
    'fu-redfish-smc-device.c',
    'fu-redfish-legacy-device.c',
    'fu-redfish-hpe-device.c',
    'fu-redfish-multipart-device.c',
    'fu-redfish-network.c',
    'fu-redfish-network-device.c',
    'fu-redfish-request.c',
    'fu-redfish-smbios.c',     # fuzzing
    ipmi_src,
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    libcurl,
  ],
)
plugin_builtins += plugin_builtin_redfish

if get_option('tests')
  install_data(['tests/redfish-smbios.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  install_data(['tests/fwupd.conf'],
    install_dir: join_paths(installed_test_datadir, 'tests'),
    install_mode: 'rw-r-----',
  )
  install_data(['tests/efi/efivars/RedfishIndications-16faa37e-4b6a-4891-9028-242de65a3b70'],
    install_dir: join_paths(installed_test_datadir, 'tests', 'efi', 'efivars'))
  install_data(['tests/efi/efivars/RedfishOSCredentials-16faa37e-4b6a-4891-9028-242de65a3b70'],
    install_dir: join_paths(installed_test_datadir, 'tests', 'efi', 'efivars'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  env.set('LSAN_OPTIONS', 'suppressions=@0@'.format(join_paths(meson.project_source_root(), 'data', 'tests', 'lsan-suppressions.txt')))
  e = executable(
    'redfish-self-test',
    rustgen.process('fu-redfish.rs'),
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      libcurl,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
      plugin_builtin_redfish,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('redfish-self-test', e, env: env)  # added to installed-tests
endif

