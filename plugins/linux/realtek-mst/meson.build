cargs = ['-DG_LOG_DOMAIN="FuPluginRealtekMst"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('realtek-mst.quirk')
plugin_builtins += static_library('fu_plugin_realtek_mst',
  rustgen.process(
    'fu-realtek-mst.rs',
  ),
  sources: [
    'fu-realtek-mst-device.c',
    'fu-realtek-mst-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  dependencies: plugin_deps,
)

