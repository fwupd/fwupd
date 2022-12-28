if get_option('plugin_kinetic_dp').require(gudev.found(),
    error_message: 'gudev is needed for plugin_kinetic_dp').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginKineticDp"']

plugin_quirks += files('kinetic-dp.quirk')

plugin_builtin_kinetic_dp = static_library('fu_plugin_kinetic_dp',
  sources: [
    'fu-kinetic-dp-plugin.c',
    'fu-kinetic-dp-common.c',
    'fu-kinetic-dp-connection.c',
    'fu-kinetic-dp-device.c',
    'fu-kinetic-dp-firmware.c',
    'fu-kinetic-dp-aux-dpcd.c',
    'fu-kinetic-dp-aux-isp.c',
    'fu-kinetic-dp-secure-aux-isp.c',
    'fu-kinetic-dp-puma-aux-isp.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_kinetic_dp
endif
