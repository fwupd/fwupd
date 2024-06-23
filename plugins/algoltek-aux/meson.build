cargs = ['-DG_LOG_DOMAIN="FuPluginAlgoltekAux"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('algoltek-aux.quirk')
plugin_builtins += static_library('fu_plugin_algoltek_aux',
  rustgen.process('fu-algoltek-aux.rs'),
  sources: [
    'fu-algoltek-aux-device.c',
    'fu-algoltek-aux-firmware.c',
    'fu-algoltek-aux-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
