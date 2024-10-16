cargs = ['-DG_LOG_DOMAIN="FuPluginRpPico"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_builtins += static_library('fu_plugin_rp_pico',
  rustgen.process('fu-rp-pico.rs'),
  sources: [
    'fu-rp-pico-device.c',
    'fu-rp-pico-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
