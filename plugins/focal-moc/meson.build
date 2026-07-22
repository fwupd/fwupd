cargs = ['-DG_LOG_DOMAIN="FuPluginFocalMoc"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('focal-moc.quirk')
plugin_builtins += static_library('fu_plugin_focal_moc',
  rustgen.process('fu-focal-moc.rs'),
  sources: [
    'fu-focal-moc-device.c',
    'fu-focal-moc-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
device_tests += files('tests/focal-moc.json')
