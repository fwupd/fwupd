cargs = ['-DG_LOG_DOMAIN="FuPluginEgisMoc"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += join_paths(meson.current_source_dir(), 'egismoc.quirk')
plugin_builtins += static_library('fu_plugin_egismoc',
  sources: [
    'fu-egismoc-device.c',
    'fu-egismoc-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
