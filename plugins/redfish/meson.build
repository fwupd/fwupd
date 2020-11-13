cargs = ['-DG_LOG_DOMAIN="FuPluginRedfish"']

shared_module('fu_plugin_redfish',
  fu_hash,
  sources : [
    'fu-plugin-redfish.c',
    'fu-redfish-client.c',
    'fu-redfish-common.c',
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
    libjsonglib,
  ],
)

install_data(['redfish.conf'],
  install_dir:  join_paths(sysconfdir, 'fwupd')
)

if get_option('tests')
  e = executable(
    'redfish-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-redfish-client.c',
      'fu-redfish-common.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
      libcurl,
      libjsonglib,
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
