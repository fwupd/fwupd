if get_option('plugin_igsc').require(gudev.found(),
    error_message: 'gudev is needed for plugin_igsc').allowed()

cargs = ['-DG_LOG_DOMAIN="FuPluginIgsc"']

plugin_quirks += files('igsc.quirk')
plugin_builtins += static_library('fu_plugin_igsc',
  sources : [
    'fu-igsc-plugin.c',
    'fu-igsc-device.c',
    'fu-igsc-code-firmware.c',
    'fu-igsc-oprom-firmware.c',
    'fu-igsc-aux-device.c',
    'fu-igsc-aux-firmware.c',
    'fu-igsc-oprom-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
