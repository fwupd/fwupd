cargs = ['-DG_LOG_DOMAIN="FuPluginIntelCvs"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('intel-cvs.quirk')
plugin_builtins += static_library('fu_plugin_intel_cvs',
  rustgen.process('fu-intel-cvs.rs'),
  sources: [
    'fu-intel-cvs-device.c',
    'fu-intel-cvs-firmware.c',
    'fu-intel-cvs-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
