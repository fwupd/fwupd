cargs = ['-DG_LOG_DOMAIN="FuPluginRedfish"']

shared_module('fu_plugin_redfish',
  sources : [
    'fu-plugin-redfish.c',
    'redfish-client.c',
    'redfish-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
    efivar,
    json_glib,
  ],
)

executable(
  'redfish-tool',
  sources : [
    'redfish-tool.c',
    'redfish-client.c',
    'redfish-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../libfwupd'),
    include_directories('../../src'),
  ],
  dependencies : [
    plugin_deps,
    efivar,
    json_glib,
  ],
  link_with : [
    fwupd,
    libfwupdprivate,
  ],
  c_args : cargs,
)

if get_option('enable-tests')
  e = executable(
    'redfish-self-test',
    sources : [
      'fu-self-test.c',
      'redfish-client.c',
      'redfish-common.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../src'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      plugin_deps,
      efivar,
      json_glib,
    ],
    link_with : [
      fwupd,
      libfwupdprivate,
    ],
    c_args : cargs
  )
  test('redfish-self-test', e)
endif
