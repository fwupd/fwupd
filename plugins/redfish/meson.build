cargs = ['-DG_LOG_DOMAIN="FuPluginRedfish"']

shared_module('fu_plugin_redfish',
  fu_hash,
  sources : [
    'fu-plugin-redfish.c',
    'fu-redfish-client.c',
    'fu-redfish-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    libfwupdprivate,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
    efivar,
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
      'fu-test.c',
      'fu-redfish-client.c',
      'fu-redfish-common.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../src'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      plugin_deps,
      efivar,
      libjsonglib,
    ],
    link_with : [
      libfwupdprivate,
    ],
    c_args : cargs
  )
  test('redfish-self-test', e)
endif
