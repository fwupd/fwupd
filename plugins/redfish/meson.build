if get_option('plugin_redfish')
if not get_option('curl')
  error('curl is required for plugin_redfish')
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginRedfish"']

install_data(['redfish.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_redfish',
  fu_hash,
  sources : [
    'fu-plugin-redfish.c',
    'fu-redfish-backend.c',
    'fu-redfish-common.c',     # fuzzing
    'fu-redfish-device.c',
    'fu-redfish-legacy-device.c',
    'fu-redfish-multipart-device.c',
    'fu-redfish-network.c',
    'fu-redfish-request.c',
    'fu-redfish-smbios.c',     # fuzzing
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
    libcurl,
  ],
)

install_data(['redfish.conf'],
  install_dir:  join_paths(sysconfdir, 'fwupd')
)

if get_option('tests')
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  testdatadirs = environment()
  testdatadirs.set('G_TEST_SRCDIR', meson.current_source_dir())
  testdatadirs.set('G_TEST_BUILDDIR', meson.current_build_dir())
  testdatadirs.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  cargs += '-DPLUGINBUILDDIR="' + meson.current_build_dir() + '"'
  e = executable(
    'redfish-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-redfish-backend.c',
      'fu-redfish-common.c',
      'fu-redfish-device.c',
      'fu-redfish-legacy-device.c',
      'fu-redfish-multipart-device.c',
      'fu-redfish-network.c',
      'fu-redfish-request.c',
      'fu-redfish-smbios.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
      libcurl,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    c_args : cargs,
    install : true,
    install_dir : installed_test_bindir,
  )
  test('redfish-self-test', e)  # added to installed-tests
endif
endif
