if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginEbitdo"']

plugin_quirks += files('ebitdo.quirk')
plugin_builtins += static_library('fu_plugin_ebitdo',
  structgen.process(
    'fu-ebitdo.struct',         # fuzzing
  ),
  sources: [
    'fu-ebitdo-plugin.c',
    'fu-ebitdo-common.c',
    'fu-ebitdo-device.c',
    'fu-ebitdo-firmware.c',     # fuzzing
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
