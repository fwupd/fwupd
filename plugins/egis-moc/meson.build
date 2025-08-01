cargs = ['-DG_LOG_DOMAIN="FuPluginEgisMoc"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += join_paths(meson.current_source_dir(), 'egis-moc.quirk')
plugin_builtin_egis_moc = static_library('fu_plugin_egis_moc',
  rustgen.process('fu-egis-moc.rs'),
  sources: [
    'fu-egis-moc-common.c',
    'fu-egis-moc-device.c',
    'fu-egis-moc-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_egis_moc

device_tests += files(
  'tests/egis-moc.json',
)

if get_option('tests')
  e = executable(
    'egis-moc-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_egis_moc,
    ],
  )
  test('egis-moc-self-test', e)
endif
