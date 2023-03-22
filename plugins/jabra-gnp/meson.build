if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginJabraGnp"']

plugin_quirks += files('jabra-gnp.quirk')
plugin_builtins += static_library('fu_plugin_jabra_gnp',
  sources: [
    'fu-jabra-gnp-common.c',
    'fu-jabra-gnp-device.c',
    'fu-jabra-gnp-firmware.c',
    'fu-jabra-gnp-image.c',
    'fu-jabra-gnp-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
