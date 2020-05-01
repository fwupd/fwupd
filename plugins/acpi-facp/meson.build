cargs = ['-DG_LOG_DOMAIN="FuPluginAcpiFacp"']

shared_module('fu_plugin_acpi_facp',
  fu_hash,
  sources : [
    'fu-plugin-acpi-facp.c',
    'fu-acpi-facp.c',
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
  ],
)

if get_option('tests')
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  e = executable(
    'acpi-facp-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-acpi-facp.c',
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
    c_args : cargs,
  )
  test('acpi-facp-self-test', e)
endif
