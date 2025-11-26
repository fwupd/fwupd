cargs = ['-DG_LOG_DOMAIN="FuPluginGoodixMoc"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('goodix-moc.quirk')
plugin_builtins += static_library('fu_plugin_goodix_moc',
  sources: [
    'fu-goodix-moc-device.c',
    'fu-goodix-moc-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
