if gudev.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginSuperio"']

plugin_quirks += files('superio.quirk')
plugin_builtins += static_library('fu_plugin_superio',
  sources: [
    'fu-superio-plugin.c',
    'fu-superio-device.c',
    'fu-superio-it55-device.c',
    'fu-superio-it85-device.c',
    'fu-superio-it89-device.c',
    'fu-superio-common.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
