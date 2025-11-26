cargs = ['-DG_LOG_DOMAIN="FuPluginJabraGnp"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('jabra-gnp.quirk')
plugin_builtins += static_library('fu_plugin_jabra_gnp',
  sources: [
    'fu-jabra-gnp-child-device.c',
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

device_tests += files(
  'tests/jabra-evolve2-75.json',
)
