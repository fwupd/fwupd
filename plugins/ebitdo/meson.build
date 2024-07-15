cargs = ['-DG_LOG_DOMAIN="FuPluginEbitdo"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('ebitdo.quirk')
plugin_builtins += static_library('fu_plugin_ebitdo',
  rustgen.process(
    'fu-ebitdo.rs',         # fuzzing
  ),
  sources: [
    'fu-ebitdo-plugin.c',
    'fu-ebitdo-device.c',
    'fu-ebitdo-firmware.c',     # fuzzing
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
