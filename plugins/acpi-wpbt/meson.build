cargs = ['-DG_LOG_DOMAIN="FuPluginAcpiWpbt"']

shared_module('fu_plugin_acpi_wpbt',
  fu_hash,
  sources : [
    'fu-plugin-acpi-wpbt.c',
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
