cargs = ['-DG_LOG_DOMAIN="FuPluginFocaltechMoc"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('focaltech-moc.quirk')
plugin_builtins += static_library('fu_plugin_focaltech_moc',
  rustgen.process('fu-focaltech-moc.rs'),
  sources: [
    'fu-focaltech-moc-device.c',
    'fu-focaltech-moc-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
device_tests += files('tests/focaltech-moc.json')
