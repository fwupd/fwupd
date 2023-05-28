if get_option(
  'plugin_powerd'
).disable_auto_if(host_machine.system() != 'linux').require(
  gio.version().version_compare('>=2.58'),
  error_message: 'gio 2.58 or later needed for powerd',
).allowed()
  cargs = ['-DG_LOG_DOMAIN="FuPluginPowerd"']

  plugin_builtins += static_library(
    'fu_plugin_powerd',
    sources: [
      'fu-powerd-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )

endif
