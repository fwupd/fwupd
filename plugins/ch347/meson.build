cargs = ['-DG_LOG_DOMAIN="FuPluginCh347"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('ch347.quirk')
plugin_builtins += static_library('fu_plugin_ch347',
  rustgen.process('fu-ch347.rs'),
  sources: [
    'fu-ch347-cfi-device.c',
    'fu-ch347-device.c',
    'fu-ch347-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
